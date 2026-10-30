#include "web_server.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_jobs.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_ap.h"

static const char *TAG = "web";

/* 上传上限：开机后（Wi-Fi + httpd 已占用）实测剩余堆约 82 KB，
 * 这里保守取 48 KB，网页端会压到 40 KB 以内再发。 */
#define UPLOAD_MAX_BYTES (48 * 1024)
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

/* 简单判断是否像一张 JPEG（SOI 开头，结尾附近有 EOI） */
static bool looks_like_jpeg(const uint8_t *buf, size_t len)
{
    if (len < 4 || buf[0] != 0xFF || buf[1] != 0xD8) {
        return false;
    }
    const size_t tail = (len >= 16) ? 16 : len;
    for (size_t i = 0; i + 1 < tail; ++i) {
        const size_t p = len - tail + i;
        if (buf[p] == 0xFF && buf[p + 1] == 0xD9) {
            return true;
        }
    }
    return false;
}

static esp_err_t upload_post(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json; charset=utf-8");

    if (req->content_len == 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"msg\":\"空请求\"}");
    }
    if (req->content_len > UPLOAD_MAX_BYTES) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"msg\":\"图片太大，请压缩到 40KB 以内\"}");
    }

    uint8_t *buf = heap_caps_malloc(req->content_len, MALLOC_CAP_8BIT);
    if (buf == NULL) {
        httpd_resp_set_status(req, "507 Insufficient Storage");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"msg\":\"内存不足\"}");
    }

    size_t got = 0;
    while (got < req->content_len) {
        const int r = httpd_req_recv(req, (char *)buf + got, req->content_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            free(buf);
            httpd_resp_set_status(req, "408 Request Timeout");
            return httpd_resp_sendstr(req, "{\"ok\":false,\"msg\":\"接收中断\"}");
        }
        got += (size_t)r;
    }

    ESP_LOGI(TAG, "收到 %u B，堆剩余 %u B", (unsigned)got,
             (unsigned)esp_get_free_heap_size());

    if (!looks_like_jpeg(buf, got)) {
        free(buf);
        httpd_resp_set_status(req, "415 Unsupported Media Type");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"msg\":\"不是有效的 JPEG\"}");
    }

    if (app_jobs_submit_upload(buf, got) != ESP_OK) {
        free(buf);
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"msg\":\"设备忙\"}");
    }

    return httpd_resp_sendstr(req, "{\"ok\":true,\"msg\":\"已收到，开始刷屏\"}");
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
