/*
 * M1：本地输入输出。
 * - BOOT 按键（GPIO9，低有效）短按切换 LED 模式
 * - LED（GPIO8，低有效）按当前模式闪烁
 * - USB Serial/JTAG 输出按键事件、模式变化与 10 s 心跳
 *
 * 线程模型（单核 C3）：
 *   button_task 轮询采样并做去抖，仅它写 s_mode；
 *   led_task 读取 s_mode 并按周期翻转，通过任务通知快速响应模式变化；
 *   app_main 负责初始化与心跳日志。
 */

#include <stdbool.h>
#include <stdint.h>

#include "app_mode.h"
#include "board.h"
#include "sensor.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BUTTON_SAMPLE_MS    10   /* 采样周期 */
#define DEBOUNCE_SAMPLES    3    /* 连续 3 次（≥30 ms）稳定才确认变化 */
#define SHORT_PRESS_MIN_US  20000   /* 20 ms */
#define SHORT_PRESS_MAX_US  800000  /* 800 ms */
#define HEARTBEAT_MS        10000
#define SENSOR_SAMPLE_MS    2000

static const char *TAG = "app";

static volatile app_mode_t s_mode = APP_MODE_SLOW;
static volatile sensor_status_t s_sensor_last = SENSOR_ERR_NOT_INIT;
static volatile uint32_t s_sensor_errors = 0;
static TaskHandle_t s_led_task = NULL;

static void led_task(void *arg)
{
    (void)arg;
    bool on = false;

    while (true) {
        const app_mode_t mode = s_mode;
        const unsigned period = app_mode_period_ms(mode);

        if (period == 0) { /* 常亮 */
            on = true;
            board_led_set(true);
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        board_led_set(on);
        on = !on;
        /* 超时则翻转；被通知则立即按新模式重新开始 */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(period));
    }
}

static long milli_frac(long milli)
{
    const long frac = milli % 1000;
    return frac < 0 ? -frac : frac;
}

static void sensor_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();
    sensor_sample_t sample;

    while (true) {
        const sensor_status_t st = sensor_read(&sample);
        s_sensor_last = st;

        if (st == SENSOR_OK) {
            ESP_LOGI(TAG, "sensor: T=%ld.%03ld C, RH=%ld.%03ld %%, seq=%lu",
                     (long)(sample.temperature_milli_c / 1000), milli_frac(sample.temperature_milli_c),
                     (long)(sample.humidity_milli_pct / 1000), milli_frac(sample.humidity_milli_pct),
                     (unsigned long)sample.seq);
        } else {
            s_sensor_errors++;
            ESP_LOGW(TAG, "sensor: read failed: %s (errors=%lu)",
                     sensor_status_name(st), (unsigned long)s_sensor_errors);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SENSOR_SAMPLE_MS));
    }
}

static void button_task(void *arg)
{
    (void)arg;

    bool stable = board_button_pressed();
    bool candidate = stable;
    int candidate_cnt = 0;
    int last_raw = -1;
    int64_t press_start_us = 0;

    ESP_LOGI(TAG, "button: GPIO%d active-low, sample %d ms, debounce %d samples",
             BOARD_BUTTON_GPIO, BUTTON_SAMPLE_MS, DEBOUNCE_SAMPLES);

    while (true) {
        const bool raw = board_button_pressed();
        if ((int)raw != last_raw) {
            last_raw = raw;
            ESP_LOGI(TAG, "button: GPIO%d %s", BOARD_BUTTON_GPIO,
                     raw ? "LOW (pressed)" : "HIGH (released)");
        }

        if (raw == stable) {
            candidate = stable;
            candidate_cnt = 0;
        } else if (raw == candidate) {
            if (++candidate_cnt >= DEBOUNCE_SAMPLES) {
                stable = raw;
                candidate_cnt = 0;

                if (stable) {
                    press_start_us = esp_timer_get_time();
                    ESP_LOGI(TAG, "button: pressed");
                } else {
                    const int64_t dur_us = esp_timer_get_time() - press_start_us;
                    if (dur_us >= SHORT_PRESS_MIN_US && dur_us <= SHORT_PRESS_MAX_US) {
                        const app_mode_t next = app_mode_next(s_mode);
                        s_mode = next;
                        ESP_LOGI(TAG, "button: released after %lld ms -> short press, mode %s (%u ms)",
                                 (long long)(dur_us / 1000), app_mode_name(next),
                                 app_mode_period_ms(next));
                        xTaskNotifyGive(s_led_task);
                    } else {
                        ESP_LOGW(TAG, "button: released after %lld ms, ignored (%s)",
                                 (long long)(dur_us / 1000),
                                 dur_us < SHORT_PRESS_MIN_US ? "too short" : "too long");
                    }
                }
            }
        } else {
            candidate = raw;
            candidate_cnt = 1;
        }

        vTaskDelay(pdMS_TO_TICKS(BUTTON_SAMPLE_MS));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(board_init());

    ESP_LOGI(TAG, "boot: mode %s (%u ms toggle), LED GPIO%d, button GPIO%d",
             app_mode_name(s_mode), app_mode_period_ms(s_mode),
             BOARD_LED_GPIO, BOARD_BUTTON_GPIO);

    const sensor_status_t sensor_st = sensor_init();
    s_sensor_last = sensor_st;
    if (sensor_st == SENSOR_OK) {
        ESP_LOGI(TAG, "sensor: ready, sampling every %d ms", SENSOR_SAMPLE_MS);
        xTaskCreate(sensor_task, "sensor", 4096, NULL, 4, NULL);
    } else {
        ESP_LOGW(TAG, "sensor: init failed: %s; local functions continue",
                 sensor_status_name(sensor_st));
    }

    xTaskCreate(led_task, "led", 3072, NULL, 5, &s_led_task);
    xTaskCreate(button_task, "button", 4096, NULL, 6, NULL);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_MS));
        ESP_LOGI(TAG, "heartbeat: mode %s, sensor %s (errors %lu), free heap %u, min free heap %u",
                 app_mode_name(s_mode), sensor_status_name(s_sensor_last),
                 (unsigned long)s_sensor_errors, (unsigned)esp_get_free_heap_size(),
                 (unsigned)esp_get_minimum_free_heap_size());
    }
}
