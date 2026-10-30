/*
 * 验证工程：在 SE0398NZ07 3.98" 四色墨水屏上显示 Hello World
 *
 * 画面：白底 + 黄色边框 + 黑色 Hello World + 红色色条（验证四色映射）。
 * 一次全屏刷新约 15~16 s，属墨水屏正常时序。
 */

#include <inttypes.h>

#include "esp_err.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "epd_se0398nz07.h"

static const char *TAG = "epd_hello";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP-IDF %s, target %s", esp_get_idf_version(), CONFIG_IDF_TARGET);
    ESP_LOGI(TAG, "free heap at start: %" PRIu32, esp_get_free_heap_size());

    ESP_ERROR_CHECK(epd_init());

    epd_fill(EPD_WHITE);
    epd_fill_rect(12, 12, EPD_WIDTH - 24, EPD_HEIGHT - 24, EPD_YELLOW);
    epd_fill_rect(20, 20, EPD_WIDTH - 40, EPD_HEIGHT - 40, EPD_WHITE);

    const int text_w = 11 * 6 * 8 - 8; /* 11 字符，5x7 字模，scale=8 */
    epd_draw_text("Hello World", (EPD_WIDTH - text_w) / 2, 240, 8, EPD_BLACK);
    epd_fill_rect((EPD_WIDTH - 240) / 2, 330, 240, 24, EPD_RED);

    ESP_LOGI(TAG, "free heap after render: %" PRIu32, esp_get_free_heap_size());
    ESP_LOGI(TAG, "start refresh (expect ~15-16 s)");

    const int64_t t0 = esp_timer_get_time();
    const esp_err_t err = epd_display();
    const int64_t dt_ms = (esp_timer_get_time() - t0) / 1000;

    ESP_LOGI(TAG, "display result=%s total=%" PRId64 " ms, free heap=%" PRIu32,
             esp_err_to_name(err), dt_ms, esp_get_free_heap_size());
    ESP_LOGI(TAG, "done; image is retained by the panel after power off");
}
