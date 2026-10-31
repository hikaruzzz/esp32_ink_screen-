#pragma once

#include <stddef.h>

#include "esp_err.h"

#include "image_pipeline.h"

/*
 * 把 JPEG 解码成四色（黑/白/黄/红）画面并写入墨水屏帧缓冲。
 * 采用 cover 方式居中裁切：铺满 768x552，多余部分裁掉，不拉伸变形。
 * 调用成功后帧缓冲即为最终画面，由调用者再调 epd_display() 刷屏。
 *
 * 两种来源：
 *   epd_image_render_jpeg()       —— 内存里的 JPEG（小图、测试用）
 *   epd_image_render_jpeg_flash() —— photo 分区里的 JPEG（上传后/关热点还原，
 *                                    顺序读 flash，不占内存，可解几百 KB 的图）
 *                                    slot 由 photo_store_info() 给出（A/B 双槽）
 *
 * params 传 NULL 用默认画质参数；out_src_w / out_src_h（可为 NULL）
 * 返回源图原始宽高，便于日志与网页提示。
 */
esp_err_t epd_image_render_jpeg(const uint8_t *jpeg, size_t len,
                                const epd_img_params_t *params,
                                int *out_src_w, int *out_src_h);

esp_err_t epd_image_render_jpeg_flash(size_t len, int slot,
                                      const epd_img_params_t *params,
                                      int *out_src_w, int *out_src_h);
