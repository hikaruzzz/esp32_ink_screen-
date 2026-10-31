#include "app_jobs.h"

#include <stdbool.h>
#include <string.h>

#include "app_config.h"
#include "epd_se0398nz07.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "image_decode.h"
#include "image_pipeline.h"
#include "photo_store.h"
#include "screen_ui.h"
#include "wifi_ap.h"

static const char *TAG = "jobs";

typedef enum {
    JOB_REFRESH = 0,
    JOB_DECODE,      /* 解码 photo 分区里的照片 */
    JOB_TOGGLE_WIFI,
} job_type_t;

typedef struct {
    job_type_t type;
} job_t;

static QueueHandle_t s_queue;
static SemaphoreHandle_t s_mtx;
static char s_state[16];
static char s_msg[96];
static int s_src_w;
static int s_src_h;

static void set_status(const char *state, const char *msg)
{
    if (xSemaphoreTake(s_mtx, portMAX_DELAY) == pdTRUE) {
        strlcpy(s_state, state, sizeof(s_state));
        strlcpy(s_msg, msg, sizeof(s_msg));
        xSemaphoreGive(s_mtx);
    }
}

static void set_src_size(int w, int h)
{
    if (xSemaphoreTake(s_mtx, portMAX_DELAY) == pdTRUE) {
        s_src_w = w;
        s_src_h = h;
        xSemaphoreGive(s_mtx);
    }
}

void app_jobs_get_status(char *state, size_t state_sz,
                         char *msg, size_t msg_sz,
                         int *src_w, int *src_h)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (state != NULL && state_sz > 0) {
        strlcpy(state, s_state, state_sz);
    }
    if (msg != NULL && msg_sz > 0) {
        strlcpy(s_msg, s_msg, msg_sz);
    }
    if (src_w != NULL) {
        *src_w = s_src_w;
    }
    if (src_h != NULL) {
        *src_h = s_src_h;
    }
    xSemaphoreGive(s_mtx);
}

static esp_err_t queue_push(job_t job)
{
    if (xQueueSend(s_queue, &job, 0) == pdTRUE) {
        return ESP_OK;
    }

    /* 队列只有 1 格且已被占用：解码作业可以被后来的替换掉
     * （反正后一张才是用户要的）；其它作业直接失败，调用者稍后重试即可。 */
    if (job.type != JOB_DECODE) {
        return ESP_ERR_INVALID_STATE;
    }
    job_t old;
    if (xQueueReceive(s_queue, &old, 0) == pdTRUE) {
        (void)old;
    }
    return (xQueueSend(s_queue, &job, 0) == pdTRUE) ? ESP_OK : ESP_FAIL;
}

esp_err_t app_jobs_submit_refresh(void)
{
    if (s_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const job_t job = {.type = JOB_REFRESH};
    return queue_push(job);
}

esp_err_t app_jobs_submit_decode(void)
{
    if (s_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const job_t job = {.type = JOB_DECODE};
    return queue_push(job);
}

esp_err_t app_jobs_submit_toggle_wifi(void)
{
    if (s_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const job_t job = {.type = JOB_TOGGLE_WIFI};
    return queue_push(job);
}

/* 刷屏 + 状态字，三处复用 */
static void refresh_with_status(const char *ok_msg)
{
    set_status("refreshing", "正在刷屏，约 15 秒，请勿断电");
    const esp_err_t err = epd_display();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "刷屏失败 %s", esp_err_to_name(err));
        set_status("error", "刷屏失败");
    } else {
        ESP_LOGI(TAG, "刷屏完成，剩余堆 %u B", (unsigned)esp_get_free_heap_size());
        set_status("idle", ok_msg);
    }
}

/* 手机网页渲染好的 2bpp 原始帧：整块读进帧缓冲，不做任何解码。 */
static esp_err_t load_raw_photo(int slot, size_t len)
{
    if (len != EPD_FB_BYTES) {
        ESP_LOGE(TAG, "原始帧长度不对：%u B，应为 %u B", (unsigned)len,
                 (unsigned)EPD_FB_BYTES);
        return ESP_ERR_INVALID_SIZE;
    }

    photo_src_t src;
    photo_store_src_init(&src, slot, len);
    size_t got = 0;
    while (got < len) {
        const size_t n = photo_store_read(&src, epd_framebuffer() + got, len - got);
        if (n == 0) {
            return ESP_FAIL;
        }
        got += n;
    }
    set_src_size(EPD_WIDTH, EPD_HEIGHT);
    return ESP_OK;
}

/* 把 photo 分区里的画面装进帧缓冲（不刷屏）。成功返回 ESP_OK。 */
static esp_err_t decode_stored_photo(void)
{
    size_t len = 0;
    int slot = -1;
    photo_fmt_t fmt = PHOTO_FMT_JPEG;
    epd_img_params_t params = EPD_IMG_PARAMS_DEFAULT();
    esp_err_t err = photo_store_info(&len, &slot, &fmt, &params);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "没有可用照片 %s", esp_err_to_name(err));
        return err;
    }

    if (fmt == PHOTO_FMT_RAW2BPP) {
        set_status("decoding", "正在装载画面");
        err = load_raw_photo(slot, len);
        if (err != ESP_OK) {
            set_status("error", "画面数据损坏，请重新上传");
        }
        return err;
    }

    set_status("decoding", "正在解码图片");
    int w = 0, h = 0;
    err = epd_image_render_jpeg_flash(len, slot, &params, &w, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "解码失败 %s", esp_err_to_name(err));
        set_status("error", "解码失败，请换一张普通 JPEG（不支持渐进式）");
        return err;
    }
    set_src_size(w, h);
    return ESP_OK;
}

static void display_task(void *arg)
{
    (void)arg;
    job_t job;

    for (;;) {
        if (xQueueReceive(s_queue, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (job.type) {
        case JOB_REFRESH:
            refresh_with_status("完成");
            break;

        case JOB_DECODE:
            if (decode_stored_photo() == ESP_OK) {
                refresh_with_status("完成");
            }
            break;

        case JOB_TOGGLE_WIFI: {
            const bool want_on = !wifi_ap_is_enabled();
            const esp_err_t err = wifi_ap_set_enabled(want_on);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "切换热点失败 %s", esp_err_to_name(err));
                set_status("error", "切换热点失败");
                break;
            }

            if (want_on) {
                screen_draw_wifi_status(app_config_get(), true);
                refresh_with_status("热点已打开");
                break;
            }

            /* 关热点 = 回到相框模式：把分区里的照片重新解码刷屏。
             * 帧缓冲已被状态页覆盖，不能直接重刷。 */
            if (decode_stored_photo() == ESP_OK) {
                refresh_with_status("热点已关闭，显示照片");
                break;
            }

            /* 没有照片（或读不出来）：退回原来的状态页兜底 */
            screen_draw_wifi_status(app_config_get(), false);
            refresh_with_status("还没有照片，请先上传一张");
            break;
        }

        default:
            break;
        }
    }
}

esp_err_t app_jobs_init(void)
{
    s_queue = xQueueCreate(1, sizeof(job_t));
    s_mtx = xSemaphoreCreateMutex();
    if (s_queue == NULL || s_mtx == NULL) {
        return ESP_ERR_NO_MEM;
    }

    strlcpy(s_state, "idle", sizeof(s_state));
    strlcpy(s_msg, "等待上传", sizeof(s_msg));

    if (xTaskCreate(display_task, "display", 6144, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
