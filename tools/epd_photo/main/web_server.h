#pragma once

#include "esp_err.h"

/*
 * 热点内的网页服务：
 *   GET  /             上传页面（内嵌在固件里）
 *   GET  /status       当前状态 JSON（含热点开关状态）
 *   POST /upload       请求体为原始 JPEG，收到后交给显示队列解码刷屏
 *   POST /config       表单 ssid=...&password=...，写入 NVS（重启生效）
 *   POST /config/reset 配置复位
 *   POST /reboot       重启设备
 *
 * 需要先 app_jobs_init()；热点关闭时服务仍监听，但无人能连。
 */
esp_err_t web_server_start(void);
