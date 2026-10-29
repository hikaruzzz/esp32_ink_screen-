/*
 * M0 功能验证：GPIO8 板载 LED 每 500 ms 翻转一次，并通过 USB Serial/JTAG 输出状态日志。
 *
 * 板卡：HW-466AB ESP32-C3 SuperMini
 * LED：GPIO8，低电平点亮（用户提供，本次实机验证）
 * 注意：GPIO8 是 ESP32-C3 启动绑带引脚；LED 熄灭（GPIO8=高）对应正常启动电平。
 */

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BLINK_GPIO      GPIO_NUM_8
#define BLINK_PERIOD_MS 500

static const char *TAG = "led_blink";

void app_main(void)
{
    ESP_LOGI(TAG, "init: GPIO%d active-low, toggle every %d ms", BLINK_GPIO, BLINK_PERIOD_MS);

    const gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BLINK_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    /* 低电平点亮：上电先熄灭，避免影响启动绑带电平 */
    ESP_ERROR_CHECK(gpio_set_level(BLINK_GPIO, 1));
    ESP_LOGI(TAG, "LED OFF (GPIO%d=1)", BLINK_GPIO);

    bool on = false;
    while (true) {
        on = !on;
        ESP_ERROR_CHECK(gpio_set_level(BLINK_GPIO, on ? 0 : 1));
        ESP_LOGI(TAG, "LED %s (GPIO%d=%d)", on ? "ON" : "OFF", BLINK_GPIO, on ? 0 : 1);
        vTaskDelay(pdMS_TO_TICKS(BLINK_PERIOD_MS));
    }
}
