/*
 * 运行配置：热点 SSID / 密码。
 *
 * 优先级：NVS > app_config_local.h 中的编译期默认值。
 * 首次启动（NVS 为空）时把默认值写入 NVS；之后以 NVS 为准。
 */
#pragma once

#include "esp_err.h"

#define APP_CONFIG_SSID_MAX 33
#define APP_CONFIG_PASS_MAX 65

typedef struct {
    char ap_ssid[APP_CONFIG_SSID_MAX];
    char ap_password[APP_CONFIG_PASS_MAX]; /* 少于 8 位时热点为开放模式 */
} app_config_t;

/* 初始化 NVS 并载入配置（首次启动写入默认值） */
esp_err_t app_config_init(void);

/* 当前生效配置（只读） */
const app_config_t *app_config_get(void);

/* 写入 NVS 并立即生效；下次启动仍生效 */
esp_err_t app_config_set(const char *ssid, const char *password);

/* 清除 NVS 配置，恢复 app_config_local.h 默认值 */
esp_err_t app_config_reset(void);
