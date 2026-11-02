/*
 * 四色墨水屏图像管线（说明见 image_pipeline.h）。
 *
 * 与主机侧 tools/epd_preview.py 的算法逐项对应，改这里时要同步改那边，
 * 否则手机预览和实屏会对不上。
 */

#include "image_pipeline.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "pal_lut.h"

static const char *TAG = "img";

/*
 * 面板四色（sRGB 近似）。真实墨水屏的白不是 255、黑也不是 0：
 * 用理想色板做最近色匹配，抖动会持续追一个屏幕给不出的亮度，误差累积后
 * 整幅发灰发暗、噪点明显。
 *
 * 本组数值来自白天自然光实拍色卡照片采样（docs/calibration.md）：以同一张
 * 照片里的纸白为参考归一化，再乘纸白假设 (204,204,198)。
 * 2026-10-31 深夜那张（粉光 + 相机暗部抬升）的采样值不再采用：黄/红的蓝绿
 * 通道被环境光抬高、黑被抬到纸白的 0.27，用它标定会把整幅图渲染得发白、
 * 发土；白天照片给出的比值稳定在 黑≈0.16、黄 1.13/0.74/0.05、红 0.58/0.18/0.13。
 * 改这里必须同步网页 index.html 的 PAL 与 tools/epd_preview.py 的 PALETTE。
 */
static const uint8_t k_pal_srgb[4][3] = {
    { 32,  33,  35},  /* EPD_BLACK  */
    {204, 204, 198},  /* EPD_WHITE  */
    {231, 152,  10},  /* EPD_YELLOW */
    {117,  36,  26},  /* EPD_RED    */
};

/* 16x16 蓝噪声阈值表（Ulichney void-and-cluster，主机侧生成，0..255）。
 * 换成蓝噪声而不是 4x4 Bayer：Bayer 的规则格点在大片中间调里就是肉眼可见的格栅。 */
static const uint8_t s_bn[16][16] = {
    {22, 229, 58, 134, 30, 250, 61, 122, 24, 159, 206, 133, 32, 198, 63, 243},
    {87, 171, 101, 8, 209, 153, 218, 1, 179, 223, 92, 254, 176, 103, 215, 130},
    {18, 202, 43, 186, 74, 91, 49, 141, 70, 115, 46, 148, 73, 13, 40, 158},
    {79, 142, 249, 124, 166, 239, 192, 102, 232, 20, 200, 230, 125, 188, 113, 224},
    {33, 109, 64, 221, 26, 118, 17, 162, 35, 140, 62, 168, 28, 246, 59, 174},
    {241, 210, 154, 11, 190, 83, 54, 213, 86, 189, 108, 10, 97, 208, 129, 0},
    {88, 183, 51, 98, 138, 248, 173, 126, 6, 156, 42, 222, 78, 161, 48, 147},
    {69, 123, 233, 164, 67, 204, 38, 227, 72, 237, 196, 136, 181, 236, 104, 197},
    {25, 253, 207, 37, 7, 121, 94, 150, 23, 116, 90, 255, 31, 65, 15, 219},
    {139, 82, 146, 105, 180, 231, 169, 216, 251, 175, 53, 127, 214, 152, 120, 172},
    {5, 187, 52, 21, 85, 57, 44, 132, 76, 201, 2, 163, 80, 247, 41, 93},
    {225, 114, 240, 193, 157, 245, 199, 12, 107, 143, 56, 228, 106, 184, 203, 60},
    {36, 167, 66, 131, 29, 117, 89, 182, 34, 244, 95, 194, 27, 137, 16, 155},
    {128, 211, 96, 220, 4, 144, 226, 68, 165, 212, 50, 149, 252, 71, 100, 235},
    {77, 14, 47, 177, 75, 205, 39, 19, 135, 111, 9, 178, 119, 217, 45, 185},
    {110, 195, 151, 242, 112, 170, 99, 191, 238, 81, 55, 234, 84, 160, 3, 145},
};

/*
 * 逐像素的「投影到凸包边 + 选色」不再在运行时算：
 * C3 是 RV32IMC，没有硬件浮点，软浮点下一张图要 52 s（实板实测），
 * 还会饿死 IDLE 触发任务看门狗。现在改成查 pal_lut.h（主机生成，
 * 见 tools/gen_pal_lut.py），运行时只剩整数运算。
 */

static int s_sat_q;           /* 饱和度，Q8 */
static uint8_t s_tone[256];   /* sRGB 8 位 -> 调色后 sRGB 8 位 */
static uint8_t s_bn4[16][16]; /* 蓝噪声阈值 0..15（配合 LUT 里的 4 位 w） */

static float srgb_to_lin(float v)
{
    return (v <= 0.04045f) ? (v / 12.92f) : powf((v + 0.055f) / 1.055f, 2.4f);
}

static float lin_to_srgb(float v)
{
    if (v <= 0.0f) {
        return 0.0f;
    }
    if (v >= 1.0f) {
        return 1.0f;
    }
    return (v <= 0.0031308f) ? (v * 12.92f) : (1.055f * powf(v, 1.0f / 2.4f) - 0.055f);
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void epd_img_params_clamp(epd_img_params_t *p)
{
    if (p == NULL) {
        return;
    }
    p->exposure = clampf(p->exposure, 0.5f, 2.5f);
    p->gamma = clampf(p->gamma, 0.6f, 2.0f);
    p->contrast = clampf(p->contrast, 0.6f, 1.6f);
    p->saturation = clampf(p->saturation, 0.0f, 2.0f);
}

void epd_pipeline_init(const epd_img_params_t *params)
{
    epd_img_params_t p = EPD_IMG_PARAMS_DEFAULT();
    if (params != NULL) {
        p = *params;
        epd_img_params_clamp(&p);
    }
    s_sat_q = (int)(p.saturation * 256.0f + 0.5f);

    for (int i = 0; i < 256; ++i) {
        const float v = (float)i / 255.0f;

        float lin = srgb_to_lin(v) * p.exposure;
        float s = lin_to_srgb(lin);
        s = clampf(0.5f + (s - 0.5f) * p.contrast, 0.0f, 1.0f);
        s = powf(s, 1.0f / p.gamma);
        s_tone[i] = (uint8_t)(s * 255.0f + 0.5f);
    }

    /* 蓝噪声 0..255 -> 0..15，配合 LUT 里 4 位的 w 直接做整数比较 */
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            s_bn4[y][x] = (uint8_t)((s_bn[y][x] * 15 + 127) / 255);
        }
    }

    ESP_LOGI(TAG, "管线就绪：曝光 %.2f gamma %.2f 对比度 %.2f 饱和 %.2f",
             p.exposure, p.gamma, p.contrast, p.saturation);
    ESP_LOGI(TAG, "色板 黑%d,%d,%d 白%d,%d,%d 黄%d,%d,%d 红%d,%d,%d",
             k_pal_srgb[0][0], k_pal_srgb[0][1], k_pal_srgb[0][2],
             k_pal_srgb[1][0], k_pal_srgb[1][1], k_pal_srgb[1][2],
             k_pal_srgb[2][0], k_pal_srgb[2][1], k_pal_srgb[2][2],
             k_pal_srgb[3][0], k_pal_srgb[3][1], k_pal_srgb[3][2]);
}

epd_color_t epd_pipeline_pixel(int r, int g, int b, int x, int y)
{
    const int luma = (299 * r + 587 * g + 114 * b + 500) / 1000;

    /* 饱和度（Q8 定点）：绕亮度拉开/压回色差（在 sRGB 域做，和主机预览一致） */
    int ri = luma + (((r - luma) * s_sat_q + 128) >> 8);
    int gi = luma + (((g - luma) * s_sat_q + 128) >> 8);
    int bi = luma + (((b - luma) * s_sat_q + 128) >> 8);
    ri = ri < 0 ? 0 : (ri > 255 ? 255 : ri);
    gi = gi < 0 ? 0 : (gi > 255 ? 255 : gi);
    bi = bi < 0 ? 0 : (bi > 255 ? 255 : bi);

    /* 色调曲线 LUT → 每通道 5 位 → 预生成的「选哪两色 + 混合比例」表 */
    const int idx = ((s_tone[ri] >> 3) << 10) |
                    ((s_tone[gi] >> 3) << 5) |
                    (s_tone[bi] >> 3);
    const uint8_t packed = k_pal_lut[idx];

    /* code 是 a 端覆盖率 0..15，蓝噪声阈值二选一（线性光混合，亮度才对） */
    return ((packed >> 4) > s_bn4[y & 15][x & 15])
               ? (epd_color_t)(packed & 3)
               : (epd_color_t)((packed >> 2) & 3);
}
