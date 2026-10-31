#include "web_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "app_jobs.h"
#include "epd_se0398nz07.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "photo_store.h"
#include "wifi_ap.h"

static const char *TAG = "web";

/* 上传分块大小：只占这么多内存，JPEG 边收边写进 photo 分区，
 * 因此上限由分区大小决定（photo_store_capacity()，约 256 KB/槽）。 */
#define UPLOAD_CHUNK     4096
#define CONFIG_BODY_MAX  256

/* IDF v6.1 的 EMBED_TXTFILES 按文件名生成符号（不保留 web/ 目录前缀），
 * 且 end 标记在结尾的 NUL 之后，所以发送长度要减 1。 */
extern const uint8_t web_index_html_start[] asm("_binary_index_html_start");
extern const uint8_t web_index_html_end[] asm("_binary_index_html_end");

static httpd_handle_t s_server;

/* ---------------- 页面 ---------------- */

static esp_err_t root_get(httpd_req_t *req)
{
    const size_t len = (size_t)(web_index_html_end - web_index_html_start);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)web_index_html_start, (ssize_t)(len - 1));
}

static esp_err_t favicon_get(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t status_get(httpd_req_t *req)
{
    char json[384];
    char state[16];
    char msg[96];
    int src_w = 0;
    int src_h = 0;

    app_jobs_get_status(state, sizeof(state), msg, sizeof(msg), &src_w, &src_h);

    snprintf(json, sizeof(json),
             "{\"state\":\"%s\",\"msg\":\"%s\",\"src_w\":%d,\"src_h\":%d,"
             "\"heap\":%u,\"wifi\":%s,\"ssid\":\"%s\"}",
             state, msg, src_w, src_h, (unsigned)esp_get_free_heap_size(),
             wifi_ap_is_enabled() ? "true" : "false",
             app_config_get()->ap_ssid);

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

/* 结尾这一小段里有没有 EOI（FFD9）。分块接收，只能事后看最后 16 字节。 */
static bool has_eoi(const uint8_t *tail, size_t len)
{
    for (size_t i = 0; i + 1 < len; ++i) {
        if (tail[i] == 0xFF && tail[i + 1] == 0xD9) {
            return true;
        }
    }
    return false;
}

/* 把请求体流式写进 photo 分区当前槽。成功返回 ESP_OK，*got_out 为实收字节数。 */
static esp_err_t recv_body_to_flash(httpd_req_t *req, uint8_t *chunk,
                                    size_t *got_out)
{
    size_t got = 0;
    while (got < (size_t)req->content_len) {
        const int r = httpd_req_recv(req, (char *)chunk, UPLOAD_CHUNK);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r < 0) {
            return ESP_ERR_TIMEOUT;
        }
        if (photo_store_write(got, chunk, (size_t)r) != ESP_OK) {
            return ESP_FAIL;
        }
        got += (size_t)r;
    }
    *got_out = got;
    return ESP_OK;
}

/* 从查询串里取一个浮点参数，缺省或解析失败时保留原值 */
static void query_float(httpd_req_t *req, const char *key, float *out)
{
    char q[160];
    char val[24];

    if (httpd_req_get_url_query_len(req) == 0) {
        return;
    }
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) {
        return;
    }
    if (httpd_query_key_value(q, key, val, sizeof(val)) != ESP_OK) {
        return;
    }
    char *end = NULL;
    const float v = strtof(val, &end);
    if (end != val) {
        *out = v;
    }
}

/*
 * 上传：边收边写进 photo 分区，内存只占一个 4 KB 的块。
 *
 * 先 begin（擦目标槽、清掉 magic）→ 逐块 write → 全部收完且像 JPEG
 * 才 commit（写 magic 和参数）。中途断网/掉电时这个槽保持无效，
 * 另一个槽里的上一张照片还在，开机/关热点仍能还原。
 */
static esp_err_t upload_post(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json; charset=utf-8");

    if (req->content_len == 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"msg\":\"空请求\"}");
    }

    const size_t cap = photo_store_capacity();
    if (req->content_len > cap) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        return httpd_resp_send(req,
                               "{\"ok\":false,\"msg\":\"图片太大，请减小尺寸或降低质量\"}",
                               HTTPD_RESP_USE_STRLEN);
    }

    uint8_t *chunk = malloc(UPLOAD_CHUNK);
    if (chunk == NULL) {
        httpd_resp_set_status(req, "507 Insufficient Storage");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"msg\":\"内存不足\"}");
    }

    esp_err_t err = photo_store_begin(req->content_len);
    if (err != ESP_OK) {
        free(chunk);
        httpd_resp_set_status(req, "507 Insufficient Storage");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"msg\":\"闪存写入失败\"}");
    }

    uint8_t tail[16] = {0}; /* 最后一段，用来找 EOI */
    size_t got = 0;
    bool is_jpeg = true;

    while (got < (size_t)req->content_len) {
        const int r = httpd_req_recv(req, (char *)chunk, UPLOAD_CHUNK);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r < 0) {
            free(chunk);
            httpd_resp_set_status(req, "408 Request Timeout");
            return httpd_resp_sendstr(req, "{\"ok\":false,\"msg\":\"接收中断\"}");
        }
        if (got == 0 && (r < 2 || chunk[0] != 0xFF || chunk[1] != 0xD8)) {
            is_jpeg = false;
            break;
        }
        if (photo_store_write(got, chunk, (size_t)r) != ESP_OK) {
            free(chunk);
            httpd_resp_set_status(req, "507 Insufficient Storage");
            return httpd_resp_sendstr(req, "{\"ok\":false,\"msg\":\"闪存写入失败\"}");
        }
        got += (size_t)r;

        const size_t keep = ((size_t)r >= sizeof(tail)) ? sizeof(tail) : (size_t)r;
        memcpy(tail, chunk + (size_t)r - keep, keep);
        if (keep < sizeof(tail)) {
            memset(tail + keep, 0, sizeof(tail) - keep);
        }
    }
    free(chunk);

    if (!is_jpeg) {
        httpd_resp_set_status(req, "415 Unsupported Media Type");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"msg\":\"不是有效的 JPEG\"}");
    }
    if (got != (size_t)req->content_len || !has_eoi(tail, sizeof(tail))) {
        httpd_resp_set_status(req, "415 Unsupported Media Type");
        return httpd_resp_send(req,
                               "{\"ok\":false,\"msg\":\"文件不完整，请重试\"}",
                               HTTPD_RESP_USE_STRLEN);
    }

    epd_img_params_t params = EPD_IMG_PARAMS_DEFAULT();
    query_float(req, "expo", &params.exposure);
    query_float(req, "gamma", &params.gamma);
    query_float(req, "con", &params.contrast);
    query_float(req, "sat", &params.saturation);
    epd_img_params_clamp(&params);
    ESP_LOGI(TAG, "收到 %u B（上限 %u B），堆剩余 %u B；画质 %.2f/%.2f/%.2f/%.2f",
             (unsigned)got, (unsigned)cap, (unsigned)esp_get_free_heap_size(),
             params.exposure, params.gamma, params.contrast, params.saturation);

    /* 先回包再排作业：刷屏要 15 s，别让手机等在这上面 */
    httpd_resp_sendstr(req, "{\"ok\":true,\"msg\":\"已收到，开始刷屏\"}");

    err = photo_store_commit(got, PHOTO_FMT_JPEG, &params);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "提交失败 %s", esp_err_to_name(err));
        return err;
    }
    if (app_jobs_submit_decode() != ESP_OK) {
        ESP_LOGW(TAG, "显示任务忙，照片已保存，稍后可关/开热点重刷");
    }
    return ESP_OK;
}

/*
 * 原始帧上传：手机网页已经完成裁切、调色和四色抖动，这里收到的就是
 * 面板帧缓冲的 2bpp 数据（105,984 B）。开发板只负责存进 flash 再刷屏，
 * 不做任何图像计算 —— 所见即所得，也省掉 JPEG 解码的十几秒。
 */
static esp_err_t raw_upload_post(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json; charset=utf-8");

    if (req->content_len != EPD_FB_BYTES) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(
            req, "{\"ok\":false,\"msg\":\"原始帧长度必须是 105984 字节\"}");
    }

    uint8_t *chunk = malloc(UPLOAD_CHUNK);
    if (chunk == NULL) {
        httpd_resp_set_status(req, "507 Insufficient Storage");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"msg\":\"内存不足\"}");
    }

    esp_err_t err = photo_store_begin(EPD_FB_BYTES);
    if (err != ESP_OK) {
        free(chunk);
        httpd_resp_set_status(req, "507 Insufficient Storage");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"msg\":\"闪存写入失败\"}");
    }

    size_t got = 0;
    err = recv_body_to_flash(req, chunk, &got);
    free(chunk);

    if (err != ESP_OK || got != EPD_FB_BYTES) {
        ESP_LOGE(TAG, "原始帧接收失败：%s，收到 %u B", esp_err_to_name(err),
                 (unsigned)got);
        httpd_resp_set_status(req, "408 Request Timeout");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"msg\":\"接收中断\"}");
    }

    ESP_LOGI(TAG, "收到原始帧 %u B，堆剩余 %u B", (unsigned)got,
             (unsigned)esp_get_free_heap_size());
    httpd_resp_sendstr(req, "{\"ok\":true,\"msg\":\"已收到，开始刷屏\"}");

    err = photo_store_commit(got, PHOTO_FMT_RAW2BPP, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "提交失败 %s", esp_err_to_name(err));
        return err;
    }
    if (app_jobs_submit_decode() != ESP_OK) {
        ESP_LOGW(TAG, "显示任务忙，画面已保存，稍后可关/开热点重刷");
    }
    return ESP_OK;
}

/* ---------------- 配置 ---------------- */

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* 从 application/x-www-form-urlencoded 主体里取一个字段并做百分号解码 */
static bool form_get(const char *body, const char *key, char *out, size_t out_sz)
{
    const size_t klen = strlen(key);
    const char *p = body;

    while (p != NULL && *p != '\0') {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            const char *v = p + klen + 1;
            size_t n = 0;
            while (*v != '\0' && *v != '&' && n + 1 < out_sz) {
                char ch = *v++;
                if (ch == '+') {
                    ch = ' ';
                } else if (ch == '%' && v[0] != '\0' && v[1] != '\0') {
                    const int hi = hex_val(v[0]);
                    const int lo = hex_val(v[1]);
                    if (hi >= 0 && lo >= 0) {
                        ch = (char)(hi * 16 + lo);
                        v += 2;
                    }
                }
                out[n++] = ch;
            }
            out[n] = '\0';
            return true;
        }
        p = strchr(p, '&');
        if (p != NULL) {
            ++p;
        }
    }
    return false;
}

static esp_err_t config_post(httpd_req_t *req)
{
    char body[CONFIG_BODY_MAX];
    size_t got = 0;

    httpd_resp_set_type(req, "application/json; charset=utf-8");

    if (req->content_len == 0 || req->content_len >= sizeof(body)) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"msg\":\"内容为空或过长\"}");
    }
    while (got < req->content_len) {
        const int r = httpd_req_recv(req, body + got, req->content_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            httpd_resp_set_status(req, "408 Request Timeout");
            return httpd_resp_sendstr(req, "{\"ok\":false}");
        }
        got += (size_t)r;
    }
    body[got] = '\0';

    char ssid[APP_CONFIG_SSID_MAX] = {0};
    char pass[APP_CONFIG_PASS_MAX] = {0};
    const bool has_ssid = form_get(body, "ssid", ssid, sizeof(ssid));
    const bool has_pass = form_get(body, "password", pass, sizeof(pass));

    if (!has_ssid || ssid[0] == '\0') {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"msg\":\"热点名称不能为空\"}");
    }
    if (has_pass && pass[0] != '\0' && strlen(pass) < 8) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req,
                                  "{\"ok\":false,\"msg\":\"密码需 8 位以上；留空则为开放热点\"}");
    }

    const esp_err_t err = app_config_set(ssid, has_pass ? pass : "");
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"msg\":\"配置过长或写入失败\"}");
    }

    return httpd_resp_sendstr(req, "{\"ok\":true,\"msg\":\"已保存，重启后生效\"}");
}

static esp_err_t config_reset_post(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    const esp_err_t err = app_config_reset();
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "{\"ok\":false}");
    }
    return httpd_resp_sendstr(req, "{\"ok\":true,\"msg\":\"已恢复默认，重启后生效\"}");
}

static void reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(600));
    esp_restart();
}

static esp_err_t reboot_post(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    const esp_err_t err = httpd_resp_sendstr(req, "{\"ok\":true,\"msg\":\"正在重启\"}");
    xTaskCreate(reboot_task, "reboot", 2048, NULL, 5, NULL);
    return err;
}

/* ---------------- 启动 ---------------- */

esp_err_t web_server_start(void)
{
    const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = root_get},
        {.uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_get},
        {.uri = "/status", .method = HTTP_GET, .handler = status_get},
        {.uri = "/upload", .method = HTTP_POST, .handler = upload_post},
        {.uri = "/upload/raw", .method = HTTP_POST, .handler = raw_upload_post},
        {.uri = "/config", .method = HTTP_POST, .handler = config_post},
        {.uri = "/config/reset", .method = HTTP_POST, .handler = config_reset_post},
        {.uri = "/reboot", .method = HTTP_POST, .handler = reboot_post},
    };

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = sizeof(uris) / sizeof(uris[0]);
    cfg.stack_size = 6144;
    cfg.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start 失败 %s", esp_err_to_name(err));
        return err;
    }

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); ++i) {
        err = httpd_register_uri_handler(s_server, &uris[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "注册 %s 失败 %s", uris[i].uri, esp_err_to_name(err));
            return err;
        }
    }

    ESP_LOGI(TAG, "网页服务已启动，端口 %d", cfg.server_port);
    return ESP_OK;
}
