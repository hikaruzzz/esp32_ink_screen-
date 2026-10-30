/*
 * gpio_probe — 墨水屏接线电平观察（只读，不驱动屏幕）
 *
 * 接线（用户提供，见 docs/screen_line.md）：
 *   DIN=GPIO7  CLK=GPIO6  CS=GPIO10  DC=GPIO3  RST=GPIO4  BUSY=GPIO5
 *
 * 本工程只把六个引脚配置为输入（禁用内部上拉/下拉），不发送任何屏幕指令。
 * 每秒做一次 500 ms 的高频采样，输出：
 *   - 报告时刻的六路电平
 *   - 采样窗口内每根线的边沿计数（用于发现毫秒级 SPI 突发）
 *   - 实际采样次数（用于判断采样率是否足够）
 */

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define SAMPLE_WINDOW_US 500000   /* 每轮高频采样时长 */
#define REPORT_PERIOD_US 1000000  /* 报告周期（采样 + 休眠） */

static const char *TAG = "gpio_probe";

typedef struct {
    const char *name;
    gpio_num_t gpio;
} probe_pin_t;

static const probe_pin_t k_pins[] = {
    {"DIN",  GPIO_NUM_7},
    {"CLK",  GPIO_NUM_6},
    {"CS",   GPIO_NUM_10},
    {"DC",   GPIO_NUM_3},
    {"RST",  GPIO_NUM_4},
    {"BUSY", GPIO_NUM_5},
};

#define PIN_COUNT (sizeof(k_pins) / sizeof(k_pins[0]))

/* bit i = k_pins[i] 的电平 */
static uint32_t read_levels(void)
{
    uint32_t levels = 0;
    for (size_t i = 0; i < PIN_COUNT; ++i) {
        if (gpio_get_level(k_pins[i].gpio) != 0) {
            levels |= 1u << i;
        }
    }
    return levels;
}

static void format_levels(uint32_t levels, char *buf, size_t buf_len)
{
    if (buf_len == 0) {
        return;
    }
    buf[0] = '\0';

    size_t used = 0;
    for (size_t i = 0; i < PIN_COUNT; ++i) {
        if (used >= buf_len) {
            break;
        }
        int n = snprintf(buf + used, buf_len - used, "%s%s(%d)=%d",
                         (i == 0) ? "" : " ",
                         k_pins[i].name,
                         (int)k_pins[i].gpio,
                         (int)((levels >> i) & 1u));
        if (n < 0) {
            break;
        }
        if ((size_t)n >= buf_len - used) {
            break; /* 截断，保留已写入内容 */
        }
        used += (size_t)n;
    }
}

static void format_counts(const uint32_t *counts, char *buf, size_t buf_len)
{
    if (buf_len == 0) {
        return;
    }
    buf[0] = '\0';

    size_t used = 0;
    for (size_t i = 0; i < PIN_COUNT; ++i) {
        if (used >= buf_len) {
            break;
        }
        int n = snprintf(buf + used, buf_len - used, "%s%s=%" PRIu32,
                         (i == 0) ? "" : " ", k_pins[i].name, counts[i]);
        if (n < 0) {
            break;
        }
        if ((size_t)n >= buf_len - used) {
            break;
        }
        used += (size_t)n;
    }
}

/* 在固定时长内密集采样，统计每根线的边沿次数 */
static int64_t sample_edges(uint32_t *edges, uint32_t *samples)
{
    memset(edges, 0, PIN_COUNT * sizeof(edges[0]));
    *samples = 0;

    uint32_t prev = read_levels();
    int64_t start = esp_timer_get_time();
    int64_t end = start + SAMPLE_WINDOW_US;

    uint32_t count = 0;
    while (esp_timer_get_time() < end) {
        uint32_t now = read_levels();
        uint32_t diff = now ^ prev;
        if (diff != 0) {
            for (size_t i = 0; i < PIN_COUNT; ++i) {
                if (diff & (1u << i)) {
                    edges[i]++;
                }
            }
        }
        prev = now;
        ++count;
    }

    *samples = count;
    return esp_timer_get_time() - start;
}

void app_main(void)
{
    ESP_LOGI(TAG, "gpio_probe: ESP-IDF %s, target %s",
             esp_get_idf_version(), CONFIG_IDF_TARGET);
    ESP_LOGI(TAG, "mode: INPUT only, pull-up/down disabled, %d ms edge-count window per second",
             (int)(SAMPLE_WINDOW_US / 1000));

    uint64_t pin_mask = 0;
    for (size_t i = 0; i < PIN_COUNT; ++i) {
        pin_mask |= 1ULL << k_pins[i].gpio;
        ESP_LOGI(TAG, "probe %-4s = GPIO%d", k_pins[i].name, (int)k_pins[i].gpio);
    }

    const gpio_config_t cfg = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    char line[160];
    char edge_line[160];

    uint32_t prev = read_levels();
    format_levels(prev, line, sizeof(line));
    ESP_LOGI(TAG, "t=0 s level=0x%02" PRIx32 " %s", prev, line);

    uint32_t seconds = 1;
    while (true) {
        uint32_t edges[PIN_COUNT];
        uint32_t samples = 0;

        int64_t elapsed_us = sample_edges(edges, &samples);
        uint32_t now = read_levels();

        format_levels(now, line, sizeof(line));
        format_counts(edges, edge_line, sizeof(edge_line));

        ESP_LOGI(TAG, "t=%" PRIu32 " s level=0x%02" PRIx32 " %s", seconds, now, line);
        ESP_LOGI(TAG, "  window=%" PRId64 " us samples=%" PRIu32 " edges(500ms): %s",
                 elapsed_us, samples, edge_line);

        if (now != prev) {
            ESP_LOGW(TAG, "level change 0x%02" PRIx32 " -> 0x%02" PRIx32, prev, now);
            prev = now;
        }

        int64_t sleep_us = REPORT_PERIOD_US - elapsed_us;
        if (sleep_us > 0) {
            vTaskDelay(pdMS_TO_TICKS((sleep_us + 999) / 1000));
        }
        ++seconds;
    }
}
