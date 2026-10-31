#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "image_pipeline.h"

/*
 * 最后一次上传的画面存在 flash 的 photo 分区里。
 *
 * 两个用途：
 *   1. 关热点后把照片重新画回屏幕（状态页会覆盖整个帧缓冲）
 *   2. 上传时流式写入，不需要把整张数据塞进内存
 *
 * 支持两种格式：
 *   PHOTO_FMT_RAW2BPP —— 手机网页算好的 768x552 2bpp 原始帧（105,984 B），
 *                        开发板不解码、不量化，直接拷进帧缓冲刷屏
 *   PHOTO_FMT_JPEG    —— 传统 JPEG，开发板上解码（保留给脚本/兼容）
 *
 * 分区均分两个槽轮流写（A/B）：上传失败/掉电时旧画面还在，不会把相框写没。
 * 每个槽：magic "EPD3" + uint32 len + uint32 seq + uint32 fmt + 4 个 float
 * （曝光/gamma/对比度/饱和，仅 JPEG 用），之后是数据。
 * 新槽的 magic 在 commit 时才写，之前一直是擦除后的 0xFF。
 */
typedef enum {
    PHOTO_FMT_JPEG = 0,
    PHOTO_FMT_RAW2BPP = 1,
} photo_fmt_t;

esp_err_t photo_store_init(void);

/* 单个槽能放多少字节数据（已扣掉文件头） */
size_t photo_store_capacity(void);

/* 流式保存：begin 擦除目标槽 → write 可多次 → commit 写文件头 */
esp_err_t photo_store_begin(size_t total_len);
esp_err_t photo_store_write(size_t off, const void *data, size_t len);
esp_err_t photo_store_commit(size_t len, photo_fmt_t fmt,
                             const epd_img_params_t *params);

/* 校验文件头并取长度/槽号/格式/参数；没有有效照片返回 ESP_ERR_NOT_FOUND */
esp_err_t photo_store_info(size_t *out_len, int *out_slot, photo_fmt_t *out_fmt,
                           epd_img_params_t *params);

/* 给 tjpgd 的顺序读取器（单线程串行使用） */
typedef struct {
    int slot;
    size_t pos;
    size_t len;
} photo_src_t;

void photo_store_src_init(photo_src_t *src, int slot, size_t len);
size_t photo_store_read(photo_src_t *src, uint8_t *buf, size_t n);
