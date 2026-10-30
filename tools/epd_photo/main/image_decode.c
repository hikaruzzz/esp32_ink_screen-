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
#include "image_pipeline.h"

static const char *TAG = "jpeg";

#define JD_POOL_SIZE 4096

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

static int jd_out(JDEC *jd, void *bitmap, JRECT *rect)
{
    const int rw = rect->right - rect->left + 1;
    const uint8_t *px = (const uint8_t *)bitmap;

    /* MCU 块覆盖的是连续区间 [left, right+1) x [top, bottom+1)。
     * 注意不能写成 `sy > rect->bottom`：像素中心是 bottom+0.5，会被判成块外，
     * 于是每个 16x16 块的最后一行/列都被丢掉，帧缓冲预填白 →
     * 全屏 16 像素周期的白色格栅（2026-10-31 实板现象）。 */
    const float left = (float)rect->left;
    const float right1 = (float)rect->right + 1.0f;
    const float top = (float)rect->top;
    const float bottom1 = (float)rect->bottom + 1.0f;

    int dx0 = (int)floorf((left - s_off_x) * s_scale);
    int dy0 = (int)floorf((top - s_off_y) * s_scale);
    int dx1 = (int)ceilf((right1 - s_off_x) * s_scale);
    int dy1 = (int)ceilf((bottom1 - s_off_y) * s_scale);

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
        const float sy = s_off_y + ((float)dy + 0.5f) * s_inv_scale;
        if (sy < top - 1e-3f || sy >= bottom1 + 1e-3f) {
            continue;
        }
        int iy = (int)(sy - top);
        if (iy < 0) {
            iy = 0;
        } else if (iy > rect->bottom - rect->top) {
            iy = rect->bottom - rect->top;
        }
        for (int dx = dx0; dx < dx1; ++dx) {
            const float sx = s_off_x + ((float)dx + 0.5f) * s_inv_scale;
            if (sx < left - 1e-3f || sx >= right1 + 1e-3f) {
                continue;
            }
            int ix = (int)(sx - left);
            if (ix < 0) {
                ix = 0;
            } else if (ix >= rw) {
                ix = rw - 1;
            }
            const uint8_t *p = px + (iy * rw + ix) * 3;
            epd_set_pixel(dx, dy, epd_pipeline_pixel(p[0], p[1], p[2], dx, dy));
        }
    }

    return 1; /* 继续解码；返回 0 表示中止 */
}

esp_err_t epd_image_render_jpeg(const uint8_t *jpeg, size_t len,
                                const epd_img_params_t *params,
                                int *out_src_w, int *out_src_h)
{
    if (jpeg == NULL || len < 4) {
        return ESP_ERR_INVALID_ARG;
    }

    epd_pipeline_init(params);

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
