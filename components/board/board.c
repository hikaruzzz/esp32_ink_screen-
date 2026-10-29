#include "board.h"

#include "driver/gpio.h"

esp_err_t board_init(void)
{
    const gpio_config_t led_conf = {
        .pin_bit_mask = 1ULL << BOARD_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&led_conf);
    if (err != ESP_OK) {
        return err;
    }
    /* 低电平点亮：上电先熄灭，保持 GPIO8 启动绑带电平为高 */
    gpio_set_level(BOARD_LED_GPIO, 1);

    const gpio_config_t button_conf = {
        .pin_bit_mask = 1ULL << BOARD_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&button_conf);
}

void board_led_set(bool on)
{
    gpio_set_level(BOARD_LED_GPIO, on ? 0 : 1);
}

bool board_button_pressed(void)
{
    return gpio_get_level(BOARD_BUTTON_GPIO) == 0;
}
