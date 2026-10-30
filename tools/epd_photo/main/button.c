/*
 * BOOT 键（GPIO9，按下拉低）短按检测。
 * 引脚依据：docs/pinmap.md —— BOOT=GPIO9，实测按下为低。
 */

#include "button.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BUTTON_GPIO       9
#define POLL_MS           10
#define DEBOUNCE_MS       30
#define MIN_PRESS_MS      40
#define MAX_PRESS_MS      2500

static const char *TAG = "button";
static button_cb_t s_cb;

static void button_task(void *arg)
{
    (void)arg;

    bool stable = gpio_get_level(BUTTON_GPIO) == 0; /* 复位瞬间按住的情况 */
    int64_t stable_since = esp_timer_get_time();
    int64_t press_started = stable ? stable_since : 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));

        const bool raw = gpio_get_level(BUTTON_GPIO) == 0;
        const int64_t now = esp_timer_get_time();

        if (raw != stable && (now - stable_since) > (DEBOUNCE_MS * 1000)) {
            stable = raw;
            stable_since = now;
            if (stable) {
                press_started = now;
            } else {
                const int64_t held_ms = (now - press_started) / 1000;
                if (held_ms >= MIN_PRESS_MS && held_ms <= MAX_PRESS_MS) {
                    ESP_LOGI(TAG, "短按 %lld ms", held_ms);
                    if (s_cb != NULL) {
                        s_cb();
                    }
                } else {
                    ESP_LOGI(TAG, "按键 %lld ms，忽略", held_ms);
                }
            }
        } else if (raw == stable) {
            stable_since = now; /* 电平稳定，重置抖动计时 */
        }
    }
}

esp_err_t button_init(button_cb_t on_short_press)
{
    s_cb = on_short_press;

    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }

    return xTaskCreate(button_task, "button", 3072, NULL, 4, NULL) == pdPASS
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}
