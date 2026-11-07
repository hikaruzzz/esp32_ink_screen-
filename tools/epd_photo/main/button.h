#pragma once

#include "esp_err.h"

typedef void (*button_cb_t)(void);

/*
 * BOOT 键（GPIO9，按下拉低）。引脚依据：docs/pinmap.md。
 *
 *   单击  —— 松开后 450 ms 内没有第二下：回调 on_click
 *   双击  —— 两下短按的松开间隔 ≤450 ms：只回调 on_double_click，
 *            单击回调不会再触发
 *   长按  —— 按下超过 2500 ms：忽略
 *
 * 单击要等这 450 ms 的窗口过去才触发，所以回调最多晚 0.45 s。
 */
esp_err_t button_init(button_cb_t on_click, button_cb_t on_double_click);
