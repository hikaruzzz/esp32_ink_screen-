#include "app_jobs.h"

#include <stdlib.h>
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
#include "screen_ui.h"
#include "wifi_ap.h"

static const char *TAG = "jobs";

typedef enum {
    JOB_REFRESH = 0,
    JOB_UPLOAD,
    JOB_TOGGLE_WIFI,
} job_type_t;

typedef struct {
    job_type_t type;
    uint8_t *data;
    size_t len;
    bool has_params;
    epd_img_params_t params;
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
        strlcpy(msg, s_msg, msg_sz);
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

    /* 队列只有 1 格且已被占用：丢弃还没开始处理的旧作业，换成新作业。
     * 对“重刷/切换”这类无数据作业，直接忽略即可（马上就会刷屏）。 */
    if (job.type != JOB_UPLOAD) {
        return ESP_ERR_INVALID_STATE;
    }
    job_t old;
    if (xQueueReceive(s_queue, &old, 0) == pdTRUE) {
        free(old.data);
    }
    return (xQueueSend(s_queue, &job, 0) == pdTRUE) ? ESP_OK : ESP_FAIL;
}

esp_err_t app_jobs_submit_refresh(void)
{
    if (s_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const job_t job = {.type = JOB_REFRESH, .data = NULL, .len = 0};
    return queue_push(job);
}

esp_err_t app_jobs_submit_upload(uint8_t *data, size_t len,
                                 const epd_img_params_t *params)
{
    if (s_queue == NULL || data == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    job_t job = {.type = JOB_UPLOAD, .data = data, .len = len};
    if (params != NULL) {
        job.params = *params;
        job.has_params = true;
    }
    return queue_push(job);
}

esp_err_t app_jobs_submit_toggle_wifi(void)
{
    if (s_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const job_t job = {.type = JOB_TOGGLE_WIFI, .data = NULL, .len = 0};
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

        case JOB_UPLOAD: {
            set_status("decoding", "正在解码图片");
            int w = 0, h = 0;
            const esp_err_t err = epd_image_render_jpeg(
                job.data, job.len, job.has_params ? &job.params : NULL, &w, &h);
            free(job.data);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "解码失败 %s", esp_err_to_name(err));
                set_status("error", "解码失败，请换一张普通 JPEG（不支持渐进式）");
                break;
            }
            set_src_size(w, h);
            refresh_with_status("完成");
            break;
        }

        case JOB_TOGGLE_WIFI: {
            const bool want_on = !wifi_ap_is_enabled();
            const esp_err_t err = wifi_ap_set_enabled(want_on);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "切换热点失败 %s", esp_err_to_name(err));
                set_status("error", "切换热点失败");
                break;
            }
            screen_draw_wifi_status(app_config_get(), want_on);
            refresh_with_status(want_on ? "热点已打开" : "热点已关闭");
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
