/*
 * 板级硬件接口：当前只覆盖 M1 用到的 LED 与 BOOT 按键。
 * 引脚以 docs/pinmap.md 为准。
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#define BOARD_LED_GPIO    8  /* 低电平点亮 */
#define BOARD_BUTTON_GPIO 9  /* 低电平按下（启动绑带引脚，运行时可用） */

/* I2C 引脚：待确认（见 docs/pinmap.md）。-1 表示未配置，真实驱动会拒绝初始化。 */
#define BOARD_I2C_SDA_GPIO (-1)
#define BOARD_I2C_SCL_GPIO (-1)

/* 初始化 LED 输出与按键输入（按键启用内部上拉）。失败返回错误码。 */
esp_err_t board_init(void);

/* 逻辑开关：true = 点亮（内部按低有效转换为电平 0）。 */
void board_led_set(bool on);

/* true = 按键当前处于按下状态。不含去抖，去抖由调用方负责。 */
bool board_button_pressed(void);
