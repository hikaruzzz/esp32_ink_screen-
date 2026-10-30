#pragma once

#include <stdbool.h>

#include "app_config.h"

/*
 * 在帧缓冲里画“热点状态页”：热点名称、密码、访问地址。
 * 只改帧缓冲，不刷屏（由调用方决定何时 epd_display()）。
 */
void screen_draw_wifi_status(const app_config_t *cfg, bool ap_on);
