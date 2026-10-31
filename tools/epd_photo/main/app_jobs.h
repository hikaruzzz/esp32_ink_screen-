#pragma once

#include <stddef.h>

#include "esp_err.h"

/*
 * 显示队列：面板和 SPI 只由一个任务（display）访问，所有需要“改画面”的操作
 * 都投递成作业，串行执行：
 *   - 重刷当前帧缓冲
 *   - 把 photo 分区里的照片解码刷屏（上传完成、关热点）
 *   - 打开/关闭 Wi-Fi 热点
 * BOOT 键、HTTP 上传、开机画面都走这里，避免并发写帧缓冲。
 */
esp_err_t app_jobs_init(void);

/* 重刷当前帧缓冲（若队列里已有更新，自动忽略） */
esp_err_t app_jobs_submit_refresh(void);

/* 解码 photo 分区里最后一张照片并刷屏；失败时状态字会给出原因。
 * 上传接口把 JPEG 流式写进分区后调用这里。 */
esp_err_t app_jobs_submit_decode(void);

/* 切换热点开关 */
esp_err_t app_jobs_submit_toggle_wifi(void);

/* 供 /status 使用；任意参数可为 NULL */
void app_jobs_get_status(char *state, size_t state_sz,
                         char *msg, size_t msg_sz,
                         int *src_w, int *src_h);
