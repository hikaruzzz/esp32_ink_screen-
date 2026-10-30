/*
 * gpio_probe — 墨水屏接线电平观察（只读，不驱动屏幕）
 *
 * 接线（用户提供，见 docs/screen_line.md）：
 *   DIN=GPIO7  CLK=GPIO6  CS=GPIO10  DC=GPIO3  RST=GPIO4  BUSY=GPIO5
 *
 * 本工程只把六个引脚配置为输入（禁用内部上拉/下拉），每秒打印一次
 * 运行计数与六路电平，用于确认：
 *   - 屏板是否供电（BUSY 等输出脚是否有确定电平）
 *   - 是否有引脚被屏板原电路持续驱动
 *   - 上电/复位后各线的静态状态
 * 不发送任何屏幕初始化或刷新指令。
 */

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define PROBE_INTERVAL_MS 1000

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

void app_main(void)
{
    ESP_LOGI(TAG, "gpio_probe: ESP-IDF %s, target %s",
             esp_get_idf_version(), CONFIG_IDF_TARGET);
    ESP_LOGI(TAG, "mode: INPUT only, internal pull-up/down disabled, no panel commands");

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
    uint32_t prev = read_levels();
    format_levels(prev, line, sizeof(line));
    ESP_LOGI(TAG, "t=0 s levels=0x%02" PRIx32 " %s", prev, line);

    uint32_t seconds = 1;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(PROBE_INTERVAL_MS));

        uint32_t now = read_levels();
        format_levels(now, line, sizeof(line));
        ESP_LOGI(TAG, "t=%" PRIu32 " s levels=0x%02" PRIx32 " %s", seconds, now, line);

        if (now != prev) {
            ESP_LOGW(TAG, "level change 0x%02" PRIx32 " -> 0x%02" PRIx32, prev, now);
            prev = now;
        }
        ++seconds;
    }
}
