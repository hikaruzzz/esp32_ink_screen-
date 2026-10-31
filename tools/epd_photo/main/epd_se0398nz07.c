/*
 * SE0398NZ07-FNG 3.98" 四色墨水屏 ESP-IDF 驱动
 *
 * 命令序列与 A0/A1 栅极映射来自社区驱动 epd3in98.py（ZGQ Inc.）：
 *   0x00 PSR=0x0B, 0x61 TRES=768x600, 0x04 Power On
 *   逐行 0x83(Set Partial RAM Area) + 0x10(Write RAM Data, 192 B/行)
 *   0x12 0x01 触发全屏刷新（约 15~16 s），0x02 0x00 Power Off
 */

#include "epd_se0398nz07.h"

#include <inttypes.h>
#include <string.h>

#include "font8x11.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define EPD_SPI_HZ    (8 * 1000 * 1000)
#define EPD_BUSY_INIT_TIMEOUT_MS   2000
#define EPD_REFRESH_TIMEOUT_MS     40000
#define EPD_REFRESH_PASSES         1
#define EPD_POWEROFF_TIMEOUT_MS    5000

static const char *TAG = "epd";

static uint8_t s_fb[EPD_FB_BYTES];
static spi_device_handle_t s_spi;

/* ---------- 字模：8x11 ASCII（font8x11.c），bit7 = 最左列 ---------- */

/* ---------- GPIO / SPI ---------- */

static void epd_gpio_init(void)
{
    const gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << EPD_PIN_DC) | (1ULL << EPD_PIN_RST) | (1ULL << EPD_PIN_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&out_cfg));

    const gpio_config_t busy_cfg = {
        .pin_bit_mask = (1ULL << EPD_PIN_BUSY),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, /* 兜底：面板 BUSY 若为开漏也能读 */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&busy_cfg));

    gpio_set_level(EPD_PIN_CS, 1);
    gpio_set_level(EPD_PIN_DC, 0);
    gpio_set_level(EPD_PIN_RST, 1);
}

static void epd_spi_init(void)
{
    const spi_bus_config_t bus_cfg = {
        .mosi_io_num = EPD_PIN_DIN,
        .miso_io_num = -1,
        .sclk_io_num = EPD_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = EPD_ROW_BYTES,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    const spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = EPD_SPI_HZ,
        .mode = 0,
        .spics_io_num = -1, /* CS 由 GPIO 手动控制 */
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev_cfg, &s_spi));
}

static void epd_spi_write(const uint8_t *data, size_t len)
{
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    t.tx_buffer = data;
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
}

static void epd_write_cmd(uint8_t cmd)
{
    gpio_set_level(EPD_PIN_DC, 0);
    gpio_set_level(EPD_PIN_CS, 0);
    epd_spi_write(&cmd, 1);
    gpio_set_level(EPD_PIN_CS, 1);
}

static void epd_write_data(const uint8_t *data, size_t len)
{
    gpio_set_level(EPD_PIN_DC, 1);
    gpio_set_level(EPD_PIN_CS, 0);
    epd_spi_write(data, len);
    gpio_set_level(EPD_PIN_CS, 1);
}

/* 等待 BUSY 变高（就绪）；返回 false 表示超时 */
static bool epd_wait_busy(uint32_t timeout_ms)
{
    vTaskDelay(pdMS_TO_TICKS(20));
    const int64_t start = esp_timer_get_time();
    while (gpio_get_level(EPD_PIN_BUSY) == 0) {
        if ((esp_timer_get_time() - start) > (int64_t)timeout_ms * 1000) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return true;
}

static bool epd_hw_init(void)
{
    gpio_set_level(EPD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(40));
    gpio_set_level(EPD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    const bool ready_reset = epd_wait_busy(EPD_BUSY_INIT_TIMEOUT_MS);
    vTaskDelay(pdMS_TO_TICKS(30));

    epd_write_cmd(0x00);
    const uint8_t psr = 0x0B;
    epd_write_data(&psr, 1);

    epd_write_cmd(0x61);
    const uint8_t tres[4] = {0x03, 0x00, 0x02, 0x58}; /* 768 x 600 */
    epd_write_data(tres, sizeof(tres));

    epd_write_cmd(0x04); /* Power On */
    const bool ready_power = epd_wait_busy(EPD_BUSY_INIT_TIMEOUT_MS);

    ESP_LOGI(TAG, "hw init: ready(reset)=%d ready(power-on)=%d",
             ready_reset, ready_power);
    return ready_power;
}

/* ---------- 帧缓冲 ---------- */

uint8_t *epd_framebuffer(void)
{
    return s_fb;
}

void epd_fill(epd_color_t color)
{
    const uint8_t pattern = (uint8_t)((color & 0x3) * 0x55);
    memset(s_fb, pattern, sizeof(s_fb));
}

void epd_set_pixel(int x, int y, epd_color_t color)
{
    if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) {
        return;
    }
    const int shift = 6 - 2 * (x & 3);
    uint8_t *p = &s_fb[y * EPD_ROW_BYTES + (x >> 2)];
    *p = (uint8_t)((*p & ~(0x3u << shift)) | ((color & 0x3u) << shift));
}

void epd_fill_rect(int x, int y, int w, int h, epd_color_t color)
{
    for (int row = y; row < y + h; ++row) {
        for (int col = x; col < x + w; ++col) {
            epd_set_pixel(col, row, color);
        }
    }
}

void epd_draw_text(const char *text, int x, int y, int scale, epd_color_t color)
{
    if (scale < 1) {
        scale = 1;
    }
    int cx = x;
    for (const char *p = text; *p != '\0'; ++p) {
        const uint8_t *rows = font8x11_rows(*p);
        for (int r = 0; r < FONT8X11_HEIGHT; ++r) {
            for (int c = 0; c < FONT8X11_WIDTH; ++c) {
                if (rows[r] & (1u << (7 - c))) {
                    epd_fill_rect(cx + c * scale, y + r * scale, scale, scale, color);
                }
            }
        }
        cx += FONT8X11_WIDTH * scale;
    }
}

/* ---------- 刷新 ---------- */

esp_err_t epd_init(void)
{
    epd_gpio_init();
    epd_spi_init();
    memset(s_fb, 0x55, sizeof(s_fb)); /* 白色 */
    epd_hw_init();
    ESP_LOGI(TAG, "init done: %dx%d, panel=%s, spi=%d MHz",
             EPD_WIDTH, EPD_HEIGHT, EPD_PANEL_IS_A1 ? "A1" : "A0", EPD_SPI_HZ / 1000000);
    return ESP_OK;
}

esp_err_t epd_display(void)
{
    epd_hw_init();

    uint8_t area[9] = {0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x01};

    /* 实测映射（evidence/M1/epd_hello_upperram.log + 实拍照片）：
     *   物理行 p <= 299：偶数 RAM 行 y = 2p（偶数区覆盖物理 0~299）
     *   物理行 p >= 300：奇数 RAM 行 y = 2p-599（奇数区覆盖物理 300~599，552~599 不可见）
     * 两半区都不做水平镜像；控制器 RAM 共 600 行，本驱动使用 0~599 全部地址。
     */
    for (int i = 0; i < EPD_HEIGHT; ++i) {
        const int p = i;
        const uint8_t *src = &s_fb[i * EPD_ROW_BYTES];
        const int y = (p <= 299) ? (2 * p) : (2 * p - 599);

        area[4] = (uint8_t)((y >> 8) & 0xFF);
        area[5] = (uint8_t)(y & 0xFF);
        area[6] = area[4];
        area[7] = area[5];

        epd_write_cmd(0x83); /* Set Partial RAM Area */
        epd_write_data(area, sizeof(area));
        epd_write_cmd(0x10); /* Write RAM Data */
        epd_write_data(src, EPD_ROW_BYTES);

        if ((i % 100) == 0) {
            ESP_LOGI(TAG, "row %d/%d -> y=%d", i, EPD_HEIGHT, y);
        }
    }

    /* 恢复默认扫描范围 (0,767,0,599) */
    const uint8_t restore[9] = {0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x02, 0x57, 0x01};
    epd_write_cmd(0x83);
    epd_write_data(restore, sizeof(restore));

    /*
     * 连刷两遍：首遍刷新后竖直中线（A0 栅极映射换算法处，物理第 275/276 行）
     * 会残留一条未完成转换的金色细线；第二遍波形可将其补刷到位。
     */
    const uint8_t on = 0x01;
    bool refreshed = false;
    for (int pass = 0; pass < EPD_REFRESH_PASSES; ++pass) {
        epd_write_cmd(0x12);
        epd_write_data(&on, 1);

        const int64_t t0 = esp_timer_get_time();
        refreshed = epd_wait_busy(EPD_REFRESH_TIMEOUT_MS);
        const int64_t elapsed_ms = (esp_timer_get_time() - t0) / 1000;
        if (refreshed) {
            ESP_LOGI(TAG, "refresh pass %d/%d done in %" PRId64 " ms",
                     pass + 1, EPD_REFRESH_PASSES, elapsed_ms);
        } else {
            ESP_LOGW(TAG, "refresh pass %d/%d BUSY timeout after %" PRId64 " ms",
                     pass + 1, EPD_REFRESH_PASSES, elapsed_ms);
            break;
        }
    }

    const uint8_t off = 0x00;
    epd_write_cmd(0x02); /* Power Off */
    epd_write_data(&off, 1);
    epd_wait_busy(EPD_POWEROFF_TIMEOUT_MS);

    return refreshed ? ESP_OK : ESP_ERR_TIMEOUT;
}

void epd_sleep(void)
{
    const uint8_t off = 0x00;
    epd_write_cmd(0x02);
    epd_write_data(&off, 1);
    epd_wait_busy(3000);

    epd_write_cmd(0x07);
    const uint8_t magic = 0xA5;
    epd_write_data(&magic, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
}
