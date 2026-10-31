#include "photo_store.h"

#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"

static const char *TAG = "photo";

#define PHOTO_PART_LABEL "photo"
/* "EPD3"：增加了格式字段（JPEG / 手机端渲好的原始帧），旧格式一律无效。 */
#define PHOTO_MAGIC      0x33504445u
#define PHOTO_HDR_SIZE   32 /* magic + len + seq + fmt + 4 个 float */
#define PHOTO_SECTOR     4096
#define PHOTO_SLOTS      2

static const esp_partition_t *s_part;
static size_t s_slot_size;           /* 每个槽的字节数 */
static int s_wr_slot = -1;           /* 本次上传写哪个槽 */
static uint32_t s_wr_seq;            /* 本次上传的序号 */

typedef struct {
    uint32_t seq;
    uint32_t len;
    uint32_t fmt;
    esp_err_t err;
} hdr_info_t;

static esp_err_t read_hdr(int slot, hdr_info_t *out, epd_img_params_t *params)
{
    uint8_t hdr[PHOTO_HDR_SIZE];
    const esp_err_t err =
        esp_partition_read(s_part, (size_t)slot * s_slot_size, hdr, sizeof(hdr));
    if (err != ESP_OK) {
        out->seq = 0;
        out->len = 0;
        out->fmt = 0;
        out->err = err;
        return err;
    }

    uint32_t magic = 0;
    memcpy(&magic, hdr + 0, 4);
    memcpy(&out->len, hdr + 4, 4);
    memcpy(&out->seq, hdr + 8, 4);
    memcpy(&out->fmt, hdr + 12, 4);

    const bool valid = (magic == PHOTO_MAGIC) && out->len > 0 &&
                       out->len <= s_slot_size - PHOTO_HDR_SIZE &&
                       out->fmt <= PHOTO_FMT_RAW2BPP;
    if (!valid) {
        out->err = ESP_ERR_NOT_FOUND;
        out->seq = 0;
        out->len = 0;
        out->fmt = 0;
        return ESP_ERR_NOT_FOUND;
    }

    if (params != NULL) {
        memcpy(&params->exposure, hdr + 16, 4);
        memcpy(&params->gamma, hdr + 20, 4);
        memcpy(&params->contrast, hdr + 24, 4);
        memcpy(&params->saturation, hdr + 28, 4);
        epd_img_params_clamp(params);
    }
    out->err = ESP_OK;
    return ESP_OK;
}

esp_err_t photo_store_init(void)
{
    /* 0x40 = data 类型的自定义子类型，与 partitions.csv 对应 */
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40,
                                      PHOTO_PART_LABEL);
    if (s_part == NULL) {
        ESP_LOGW(TAG, "没找到 photo 分区，上传和关热点还原都不可用");
        return ESP_ERR_NOT_FOUND;
    }
    s_slot_size = (s_part->size / PHOTO_SLOTS) & ~(size_t)(PHOTO_SECTOR - 1);
    ESP_LOGI(TAG, "photo 分区 %" PRIu32 " B @0x%" PRIx32 "，%d 个槽，每槽 %u B",
             s_part->size, s_part->address, PHOTO_SLOTS, (unsigned)s_slot_size);
    return (s_slot_size > PHOTO_HDR_SIZE + 1024) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

size_t photo_store_capacity(void)
{
    return (s_slot_size > PHOTO_HDR_SIZE) ? (s_slot_size - PHOTO_HDR_SIZE) : 0;
}

esp_err_t photo_store_begin(size_t total_len)
{
    if (s_part == NULL || total_len == 0 || total_len > photo_store_capacity()) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 选一个槽：优先写“较旧/无效”的那个，另一个留着当回退 */
    hdr_info_t h0, h1;
    read_hdr(0, &h0, NULL);
    read_hdr(1, &h1, NULL);
    s_wr_slot = (h0.seq <= h1.seq) ? 0 : 1;
    s_wr_seq = ((h0.seq > h1.seq) ? h0.seq : h1.seq) + 1;

    /* 只擦要用的扇区；擦完 magic 是 0xFF，commit 前都算无效照片 */
    const size_t need = total_len + PHOTO_HDR_SIZE;
    const size_t erase = ((need + PHOTO_SECTOR - 1) / PHOTO_SECTOR) * PHOTO_SECTOR;
    const esp_err_t err =
        esp_partition_erase_range(s_part, (size_t)s_wr_slot * s_slot_size, erase);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "槽 %d 擦除失败 %s", s_wr_slot, esp_err_to_name(err));
        s_wr_slot = -1;
    }
    return err;
}

esp_err_t photo_store_write(size_t off, const void *data, size_t len)
{
    if (s_part == NULL || s_wr_slot < 0 || data == NULL || len == 0 ||
        off + len > photo_store_capacity()) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_partition_write(
        s_part, (size_t)s_wr_slot * s_slot_size + PHOTO_HDR_SIZE + off, data, len);
}

esp_err_t photo_store_commit(size_t len, photo_fmt_t fmt,
                             const epd_img_params_t *params)
{
    if (s_part == NULL || s_wr_slot < 0 || len == 0 ||
        len > photo_store_capacity()) {
        return ESP_ERR_INVALID_STATE;
    }

    epd_img_params_t p = EPD_IMG_PARAMS_DEFAULT();
    if (params != NULL) {
        p = *params;
    }

    uint8_t hdr[PHOTO_HDR_SIZE];
    const uint32_t magic = PHOTO_MAGIC;
    const uint32_t len32 = (uint32_t)len;
    const uint32_t fmt32 = (uint32_t)fmt;
    memcpy(hdr + 0, &magic, 4);
    memcpy(hdr + 4, &len32, 4);
    memcpy(hdr + 8, &s_wr_seq, 4);
    memcpy(hdr + 12, &fmt32, 4);
    memcpy(hdr + 16, &p.exposure, 4);
    memcpy(hdr + 20, &p.gamma, 4);
    memcpy(hdr + 24, &p.contrast, 4);
    memcpy(hdr + 28, &p.saturation, 4);

    const esp_err_t err = esp_partition_write(
        s_part, (size_t)s_wr_slot * s_slot_size, hdr, sizeof(hdr));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "写文件头失败 %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "已保存 %u B 到槽 %d（序号 %u，格式 %s）", (unsigned)len,
             s_wr_slot, (unsigned)s_wr_seq,
             fmt == PHOTO_FMT_RAW2BPP ? "2bpp 原始帧" : "JPEG");
    s_wr_slot = -1;
    return ESP_OK;
}

esp_err_t photo_store_info(size_t *out_len, int *out_slot, photo_fmt_t *out_fmt,
                           epd_img_params_t *params)
{
    if (s_part == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (out_slot != NULL) {
        *out_slot = -1;
    }
    if (out_fmt != NULL) {
        *out_fmt = PHOTO_FMT_JPEG;
    }

    hdr_info_t h0, h1;
    const esp_err_t e0 = read_hdr(0, &h0, NULL);
    const esp_err_t e1 = read_hdr(1, &h1, NULL);
    if (e0 != ESP_OK && e1 != ESP_OK) {
        return (e0 != ESP_OK) ? e0 : e1;
    }

    const int slot = (e1 != ESP_OK || h0.seq > h1.seq) ? 0 : 1;
    hdr_info_t h;
    epd_img_params_t p = EPD_IMG_PARAMS_DEFAULT();
    if (read_hdr(slot, &h, &p) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    if (params != NULL) {
        *params = p;
    }
    if (out_len != NULL) {
        *out_len = h.len;
    }
    if (out_slot != NULL) {
        *out_slot = slot;
    }
    if (out_fmt != NULL) {
        *out_fmt = (photo_fmt_t)h.fmt;
    }
    return ESP_OK;
}

void photo_store_src_init(photo_src_t *src, int slot, size_t len)
{
    if (src != NULL) {
        src->slot = slot;
        src->pos = 0;
        src->len = len;
    }
}

size_t photo_store_read(photo_src_t *src, uint8_t *buf, size_t n)
{
    if (s_part == NULL || src == NULL || n == 0 || src->pos >= src->len) {
        return 0;
    }
    if (n > src->len - src->pos) {
        n = src->len - src->pos;
    }
    const esp_err_t err = esp_partition_read(
        s_part, (size_t)src->slot * s_slot_size + PHOTO_HDR_SIZE + src->pos,
        buf, n);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "读分区失败 %s", esp_err_to_name(err));
        return 0;
    }
    src->pos += n;
    return n;
}
