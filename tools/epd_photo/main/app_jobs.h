#pragma once

#include <stddef.h>

#include "esp_err.h"

#include "image_pipeline.h"

/*
 * 显示队列：面板和 SPI 只由一个任务（display）访问，所有需要“改画面”的操作
 * 都投递成作业，串行执行：
 *   - 重刷当前帧缓冲
 *   - 解码一张新图片并刷屏
 *   - 打开/关闭 Wi-Fi 热点并刷新状态页
 * BOOT 键、HTTP 上传、开机画面都走这里，避免并发写帧缓冲。
 */
esp_err_t app_jobs_init(void);

/* 重刷当前帧缓冲（若队列里已有更新，自动忽略） */
esp_err_t app_jobs_submit_refresh(void);

/* 提交一张 JPEG：成功后接管 data 所有权（内部 free），失败时调用者需自行释放。
 * params 为 NULL 时用 EPD_IMG_PARAMS_DEFAULT()。 */
esp_err_t app_jobs_submit_upload(uint8_t *data, size_t len,
                                 const epd_img_params_t *params);

/* 切换热点开关 */
esp_err_t app_jobs_submit_toggle_wifi(void);

/* 供 /status 使用；任意参数可为 NULL */
void app_jobs_get_status(char *state, size_t state_sz,
                         char *msg, size_t msg_sz,
                         int *src_w, int *src_h);
