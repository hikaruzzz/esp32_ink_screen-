#pragma once

#include <stddef.h>

#include "esp_err.h"

/*
 * 把内存中的 JPEG 解码成四色（黑/白/黄/红）画面并写入墨水屏帧缓冲。
 * 采用 cover 方式居中裁切：铺满 768x552，多余部分裁掉，不拉伸变形。
 * 调用成功后帧缓冲即为最终画面，由调用者再调 epd_display() 刷屏。
 *
 * out_src_w / out_src_h（可为 NULL）返回源图原始宽高，便于日志与网页提示。
 */
esp_err_t epd_image_render_jpeg(const uint8_t *jpeg, size_t len,
                                int *out_src_w, int *out_src_h);
