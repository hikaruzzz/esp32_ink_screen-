#pragma once

#include <stdbool.h>

#include "esp_err.h"

/* 以 AP 模式初始化并配置热点，射频暂不开（由调用方 wifi_ap_set_enabled 打开）。
 * 密码少于 8 位时使用开放网络。 */
esp_err_t wifi_ap_init(const char *ssid, const char *password);

/* 运行中开关热点（不动已配置的 SSID/密码，重新打开后客户端需重连） */
esp_err_t wifi_ap_set_enabled(bool enabled);

bool wifi_ap_is_enabled(void);
