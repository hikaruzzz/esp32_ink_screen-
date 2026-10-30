/*
 * JPEG -> 四色墨水屏帧缓冲。
 *
 * C3 没有 PSRAM，768x552 的 RGB565 整帧要 830 KB，放不下。因此用 tjpgd
 * 的逐 MCU 回调：每解出一个 MCU，立即按 cover 映射量化成 2 bpp 写进
 * 已有的帧缓冲，内存占用只有一个约 4 KB 的工作池。
 *
 * tjpgd 的 JDEC.width/height 是源图尺寸，回调里的 rect 是缩小后的坐标
 * （x >> scale），映射时用缩小后的尺寸计算。
 */

#include "image_decode.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "tjpgd.h"

#include "epd_se0398nz07.h"

static const char *TAG = "jpeg";

#define JD_POOL_SIZE 4096

/* 面板四色，按 R/G/B 排列，用于最近色量化 */
static const int k_palette[4][3] = {
    {0, 0, 0},       /* EPD_BLACK  */
    {255, 255, 255}, /* EPD_WHITE  */
    {255, 200, 0},   /* EPD_YELLOW */
    {190, 25, 40},   /* EPD_RED    */
};

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
} jpeg_src_t;

/* jd_in / jd_out 通过 jd->device 拿不到私有参数之外的映射参数，
 * 这里用文件级静态变量保存（解码是单线程串行的）。 */
static jpeg_src_t s_src;

static int s_img_w;      /* 缩小后的源图宽 */
static int s_img_h;      /* 缩小后的源图高 */
static float s_scale;    /* 目标像素 / 源像素 */
static float s_inv_scale;
static float s_off_x;    /* 源图裁切起点 */
static float s_off_y;

static size_t jd_in(JDEC *jd, uint8_t *buf, size_t n)
{
    jpeg_src_t *s = (jpeg_src_t *)jd->device;
    const size_t avail = s->len - s->pos;
    if (n > avail) {
        n = avail;
    }
    if (buf != NULL && n > 0) {
        memcpy(buf, s->data + s->pos, n);
    }
    s->pos += n;
    return n;
}

static int clamp255(int v)
{
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

/* 最近色量化 + 4x4 Bayer 抖动，缓解照片渐变处的断层 */
static epd_color_t quantize(int r, int g, int b, int x, int y)
{
    static const int8_t k_bayer[4][4] = {
        {0, 8, 2, 10},
        {12, 4, 14, 6},
        {3, 11, 1, 9},
        {15, 7, 13, 5},
    };
    const int d = (k_bayer[y & 3][x & 3] - 8) * 5; /* ±40 */

    r = clamp255(r + d);
    g = clamp255(g + d);
    b = clamp255(b + d);

    int best = EPD_WHITE;
    int best_dist = INT_MAX;
    for (int i = 0; i < 4; ++i) {
        const int dr = r - k_palette[i][0];
        const int dg = g - k_palette[i][1];
        const int db = b - k_palette[i][2];
        const int dist = dr * dr + dg * dg + db * db;
        if (dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }
    return (epd_color_t)best;
}

static int jd_out(JDEC *jd, void *bitmap, JRECT *rect)
{
    const int rw = rect->right - rect->left + 1;
    const uint8_t *px = (const uint8_t *)bitmap;

    int dx0 = (int)floorf((rect->left - s_off_x) * s_scale);
    int dy0 = (int)floorf((rect->top - s_off_y) * s_scale);
    int dx1 = (int)ceilf((rect->right + 1 - s_off_x) * s_scale);
    int dy1 = (int)ceilf((rect->bottom + 1 - s_off_y) * s_scale);

    if (dx0 < 0) {
        dx0 = 0;
    }
    if (dy0 < 0) {
        dy0 = 0;
    }
    if (dx1 > EPD_WIDTH) {
        dx1 = EPD_WIDTH;
    }
    if (dy1 > EPD_HEIGHT) {
        dy1 = EPD_HEIGHT;
    }

    for (int dy = dy0; dy < dy1; ++dy) {
        const float sy = s_off_y + (dy + 0.5f) * s_inv_scale;
        if (sy < rect->top || sy > rect->bottom) {
            continue;
        }
        const int iy = (int)sy - rect->top;
        for (int dx = dx0; dx < dx1; ++dx) {
            const float sx = s_off_x + (dx + 0.5f) * s_inv_scale;
            if (sx < rect->left || sx > rect->right) {
                continue;
            }
            const int ix = (int)sx - rect->left;
            const uint8_t *p = px + (iy * rw + ix) * 3;
            epd_set_pixel(dx, dy, quantize(p[0], p[1], p[2], dx, dy));
        }
    }

    return 1; /* 继续解码；返回 0 表示中止 */
}

esp_err_t epd_image_render_jpeg(const uint8_t *jpeg, size_t len,
                                int *out_src_w, int *out_src_h)
{
    if (jpeg == NULL || len < 4) {
        return ESP_ERR_INVALID_ARG;
    }

    s_src.data = jpeg;
    s_src.len = len;
    s_src.pos = 0;

    void *pool = malloc(JD_POOL_SIZE);
    if (pool == NULL) {
        return ESP_ERR_NO_MEM;
    }

    JDEC jd;
    JRESULT res = jd_prepare(&jd, jd_in, pool, JD_POOL_SIZE, &s_src);
    if (res != JDR_OK) {
        ESP_LOGE(TAG, "jd_prepare 失败 %d（渐进式 JPEG 不支持）", (int)res);
        free(pool);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (out_src_w != NULL) {
        *out_src_w = jd.width;
    }
    if (out_src_h != NULL) {
        *out_src_h = jd.height;
    }
    ESP_LOGI(TAG, "源图 %ux%u，分量数 %u", jd.width, jd.height, jd.ncomp);

    /* 源图比面板大很多时先 1/2、1/4、1/8 缩小，省解码时间 */
    uint8_t scale = 0;
    while (scale < 3 &&
           ((jd.width >> scale) > 2 * EPD_WIDTH || (jd.height >> scale) > 2 * EPD_HEIGHT)) {
        ++scale;
    }

    s_img_w = jd.width >> scale;
    s_img_h = jd.height >> scale;

    const float sx = (float)EPD_WIDTH / (float)s_img_w;
    const float sy = (float)EPD_HEIGHT / (float)s_img_h;
    s_scale = (sx > sy) ? sx : sy; /* cover：取较大的比例保证铺满 */
    s_inv_scale = 1.0f / s_scale;

    s_off_x = ((float)s_img_w - (float)EPD_WIDTH * s_inv_scale) * 0.5f;
    s_off_y = ((float)s_img_h - (float)EPD_HEIGHT * s_inv_scale) * 0.5f;
    if (s_off_x < 0.0f) {
        s_off_x = 0.0f;
    }
    if (s_off_y < 0.0f) {
        s_off_y = 0.0f;
    }

    ESP_LOGI(TAG, "解码尺寸 %dx%d（scale=%u），cover 比例 %.3f，裁切起点 %.1f,%.1f",
             s_img_w, s_img_h, scale, s_scale, s_off_x, s_off_y);

    epd_fill(EPD_WHITE);
    res = jd_decomp(&jd, jd_out, scale);
    free(pool);

    if (res != JDR_OK) {
        ESP_LOGE(TAG, "jd_decomp 失败 %d", (int)res);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}
