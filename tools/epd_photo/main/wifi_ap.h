#pragma once

#include <stdbool.h>

#include "esp_err.h"

/* 以 AP 模式初始化并配置热点，但射频保持关闭（上电默认不开热点）。
 * 密码少于 8 位时使用开放网络。 */
esp_err_t wifi_ap_init(const char *ssid, const char *password);

/* 运行中开关热点（不动已配置的 SSID/密码，重新打开后客户端需重连） */
esp_err_t wifi_ap_set_enabled(bool enabled);

bool wifi_ap_is_enabled(void);
