/*
 * BOOT 键（GPIO9，按下拉低）单击/双击检测。
 * 引脚依据：docs/pinmap.md —— BOOT=GPIO9，实测按下为低。
 *
 * 单击要等第二下的窗口（DOUBLE_GAP_MS）过去才能确定，所以单击回调最多晚 0.45 s；
 * 双击在第二下松开时立刻回调。
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
#define DOUBLE_GAP_MS     450

static const char *TAG = "button";
static button_cb_t s_click_cb;
static button_cb_t s_double_cb;

/* 已松开、但可能还有第二下的那一下 */
static bool s_pending;
static int64_t s_pending_at;

static void fire(button_cb_t cb)
{
    if (cb != NULL) {
        cb();
    }
}

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

        /* 窗口过了、手也没按着 —— 这一下就是单击 */
        if (s_pending && !stable && (now - s_pending_at) > (DOUBLE_GAP_MS * 1000)) {
            s_pending = false;
            ESP_LOGI(TAG, "单击");
            fire(s_click_cb);
        }

        if (raw != stable && (now - stable_since) > (DEBOUNCE_MS * 1000)) {
            stable = raw;
            stable_since = now;
            if (stable) {
                press_started = now;
            } else {
                const int64_t held_ms = (now - press_started) / 1000;
                if (held_ms >= MIN_PRESS_MS && held_ms <= MAX_PRESS_MS) {
                    if (s_pending && (now - s_pending_at) <= (DOUBLE_GAP_MS * 1000)) {
                        s_pending = false;
                        ESP_LOGI(TAG, "双击（间隔 %lld ms）", (now - s_pending_at) / 1000);
                        fire(s_double_cb);
                    } else {
                        s_pending = true;
                        s_pending_at = now;
                        ESP_LOGI(TAG, "短按 %lld ms，等第二下", held_ms);
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

esp_err_t button_init(button_cb_t on_click, button_cb_t on_double_click)
{
    s_click_cb = on_click;
    s_double_cb = on_double_click;

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
