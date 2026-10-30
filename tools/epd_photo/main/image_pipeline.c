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

static const char *TAG = "img";

/*
 * 面板四色（sRGB 近似）。真实墨水屏的白不是 255、黑也不是 0：
 * 用理想色板做最近色匹配，抖动会持续追一个屏幕给不出的亮度，误差累积后
 * 整幅发灰发暗、噪点明显。数值待实板标定（docs/current_state.md）。
 */
static const uint8_t k_pal_srgb[4][3] = {
    {32, 32, 32},     /* EPD_BLACK  */
    {204, 204, 198},  /* EPD_WHITE  */
    {222, 190, 74},   /* EPD_YELLOW */
    {166, 62, 52},    /* EPD_RED    */
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

typedef struct {
    float pj[3];    /* 边的一端（权重 0 的那端） */
    float d[3];     /* 另一端 - 这一端 */
    float inv_den;  /* 1 / |d|^2 */
} edge_t;

static float s_sat;

static uint8_t s_tone[256];   /* sRGB 8 位 -> 调色后 sRGB 8 位 */
static float s_lin[256];      /* sRGB 8 位 -> 线性光 0..1 */

static edge_t s_edge[6];
static uint8_t s_edge_a[6];   /* d 方向那端的色号 */
static uint8_t s_edge_b[6];

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
    s_sat = p.saturation;

    for (int i = 0; i < 256; ++i) {
        const float v = (float)i / 255.0f;
        s_lin[i] = srgb_to_lin(v);

        float lin = srgb_to_lin(v) * p.exposure;
        float s = lin_to_srgb(lin);
        s = clampf(0.5f + (s - 0.5f) * p.contrast, 0.0f, 1.0f);
        s = powf(s, 1.0f / p.gamma);
        s_tone[i] = (uint8_t)(s * 255.0f + 0.5f);
    }

    float pal[4][3];
    for (int i = 0; i < 4; ++i) {
        for (int c = 0; c < 3; ++c) {
            pal[i][c] = srgb_to_lin((float)k_pal_srgb[i][c] / 255.0f);
        }
    }

    int n = 0;
    for (int i = 0; i < 4; ++i) {
        for (int j = i + 1; j < 4; ++j) {
            float den = 0.0f;
            for (int c = 0; c < 3; ++c) {
                s_edge[n].pj[c] = pal[j][c];
                s_edge[n].d[c] = pal[i][c] - pal[j][c];
                den += s_edge[n].d[c] * s_edge[n].d[c];
            }
            s_edge[n].inv_den = den > 1e-9f ? (1.0f / den) : 0.0f;
            s_edge_a[n] = (uint8_t)i;
            s_edge_b[n] = (uint8_t)j;
            ++n;
        }
    }

    ESP_LOGI(TAG, "管线就绪：曝光 %.2f gamma %.2f 对比度 %.2f 饱和 %.2f",
             p.exposure, p.gamma, p.contrast, p.saturation);
}

epd_color_t epd_pipeline_pixel(int r, int g, int b, int x, int y)
{
    const int luma = (299 * r + 587 * g + 114 * b + 500) / 1000;

    /* 饱和度：绕亮度拉开/压回色差（在 sRGB 域做，和主机预览一致） */
    int ri = luma + (int)(((float)(r - luma)) * s_sat + (r >= luma ? 0.5f : -0.5f));
    int gi = luma + (int)(((float)(g - luma)) * s_sat + (g >= luma ? 0.5f : -0.5f));
    int bi = luma + (int)(((float)(b - luma)) * s_sat + (b >= luma ? 0.5f : -0.5f));
    ri = ri < 0 ? 0 : (ri > 255 ? 255 : ri);
    gi = gi < 0 ? 0 : (gi > 255 ? 255 : gi);
    bi = bi < 0 ? 0 : (bi > 255 ? 255 : bi);

    /* 色调曲线（曝光/对比度/gamma）走 LUT */
    const float tr = s_lin[s_tone[ri]];
    const float tg = s_lin[s_tone[gi]];
    const float tb = s_lin[s_tone[bi]];

    /* 找四色凸包上最贴合的一条边，得到两色混合比例 w */
    float best_err = 1e30f;
    float best_w = 0.0f;
    int best = 0;
    for (int e = 0; e < 6; ++e) {
        const edge_t *ed = &s_edge[e];
        const float w = clampf(((tr - ed->pj[0]) * ed->d[0] +
                                (tg - ed->pj[1]) * ed->d[1] +
                                (tb - ed->pj[2]) * ed->d[2]) * ed->inv_den,
                               0.0f, 1.0f);
        const float dr = tr - (ed->pj[0] + w * ed->d[0]);
        const float dg = tg - (ed->pj[1] + w * ed->d[1]);
        const float db = tb - (ed->pj[2] + w * ed->d[2]);
        const float err = dr * dr + dg * dg + db * db;
        if (err < best_err) {
            best_err = err;
            best_w = w;
            best = e;
        }
    }

    /* 蓝噪声阈值二选一：w 是「a 端」的覆盖率，线性光下混合，亮度才对 */
    const float thr = (float)s_bn[y & 15][x & 15] / 255.0f;
    return (thr < best_w) ? (epd_color_t)s_edge_a[best] : (epd_color_t)s_edge_b[best];
}
