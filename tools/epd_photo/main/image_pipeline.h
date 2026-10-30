/*
 * 四色墨水屏图像管线：sRGB 像素 -> 2bpp 色号（0=黑 1=白 2=黄 3=红）。
 *
 * 设计要点（对照旧实现见 docs/current_state.md）：
 *   1. 用「面板实测色板」而不是理想 RGB，抖动才不会去追屏幕达不到的亮度；
 *   2. 抖动在线性光下做混合，纸面观感亮度才正确（否则中间调偏黑）；
 *   3. 把像素投影到四色凸包最合适的一条「边」上，得到两种色料的混合比例，
 *      再按蓝噪声阈值二选一 —— 没有 4x4 Bayer 那种可见格栅。
 */

#pragma once

#include <stdint.h>

#include "epd_se0398nz07.h"

typedef struct {
    float exposure;   /* 线性光曝光，1.0 = 不变；0.5 .. 2.5 */
    float gamma;      /* >1 提亮中间调；0.6 .. 2.0 */
    float contrast;   /* 绕中灰缩放；0.6 .. 1.6 */
    float saturation; /* 0 = 灰，1 = 原图；0 .. 2 */
} epd_img_params_t;

#define EPD_IMG_PARAMS_DEFAULT() \
    ((epd_img_params_t){.exposure = 1.35f, .gamma = 1.10f, \
                        .contrast = 1.05f, .saturation = 1.25f})

/* 取值范围收紧，越界值夹到边界（网页参数不可信） */
void epd_img_params_clamp(epd_img_params_t *p);

/* 建立色调 LUT，之后每像素直接查表 */
void epd_pipeline_init(const epd_img_params_t *p);

/* 单像素量化：输入 sRGB 8 位，输出色号；x/y 用于选取蓝噪声阈值 */
epd_color_t epd_pipeline_pixel(int r, int g, int b, int x, int y);
