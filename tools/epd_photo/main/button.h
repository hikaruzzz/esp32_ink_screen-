#pragma once

#include "esp_err.h"

typedef void (*button_cb_t)(void);

/* BOOT 键（GPIO9，按下为低）：短按在松开时触发一次回调 */
esp_err_t button_init(button_cb_t on_short_press);
