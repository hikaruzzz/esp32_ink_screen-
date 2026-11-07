#pragma once

#include <stddef.h>

#include "esp_err.h"

/*
 * 显示队列：面板和 SPI 只由一个任务（display）访问，所有需要“改画面”的操作
 * 都投递成作业，串行执行：
 *   - 重刷当前帧缓冲
 *   - 把 photo 分区里的照片解码刷屏（上传完成）
 *   - 打开/关闭 Wi-Fi 热点（画面只在信息页上才跟着变）
 *   - 在照片和热点信息页之间切换（BOOT 键双击）
 *   - 强制显示热点信息页（开机时还没有照片）
 * BOOT 键、HTTP 上传、开机画面都走这里，避免并发写帧缓冲。
 */
esp_err_t app_jobs_init(void);

/* 重刷当前帧缓冲（若队列里已有更新，自动忽略） */
esp_err_t app_jobs_submit_refresh(void);

/* 解码 photo 分区里最后一张照片并刷屏；失败时状态字会给出原因。
 * 上传接口把 JPEG 流式写进分区后调用这里。 */
esp_err_t app_jobs_submit_decode(void);

/* BOOT 单击：切换热点开关。屏幕上是照片时画面不动，是信息页时重绘信息页。 */
esp_err_t app_jobs_submit_toggle_wifi(void);

/* BOOT 双击：照片 ↔ 热点信息页。没有照片时留在信息页。 */
esp_err_t app_jobs_submit_toggle_info(void);

/* 开机时没有照片：直接显示热点信息页 */
esp_err_t app_jobs_submit_show_info(void);

/* 供 /status 使用；任意参数可为 NULL */
void app_jobs_get_status(char *state, size_t state_sz,
                         char *msg, size_t msg_sz,
                         int *src_w, int *src_h);
