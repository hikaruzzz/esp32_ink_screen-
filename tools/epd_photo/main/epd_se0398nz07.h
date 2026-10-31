/*
 * SE0398NZ07-FNG 3.98" 四色墨水屏（黑/白/黄/红，2 bpp）ESP-IDF 驱动
 *
 * 引脚（用户接线，docs/screen_line.md）：
 *   DIN=GPIO7  CLK=GPIO6  CS=GPIO10  DC=GPIO3  RST=GPIO4  BUSY=GPIO5
 *
 * 协议与初始化序列来自社区反汇编驱动 SE0398NZ07_MicroPython/epd3in98.py
 * （作者 ZGQ Inc.，见 https://blog.zgqinc.gq/posts/e398nz/）。
 * BUSY：低=忙，高=就绪。
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"

#define EPD_WIDTH  768
#define EPD_HEIGHT 552

/* 帧缓冲：2 bpp，每字节 4 像素（左侧像素在高位），按显示行 p=0..551 排列。
 * 整帧 105,984 B —— 与网页上传的原始 2bpp 数据布局完全相同。 */
#define EPD_ROW_BYTES (EPD_WIDTH / 4)
#define EPD_FB_BYTES  (EPD_ROW_BYTES * EPD_HEIGHT)

#define EPD_PIN_DIN  7
#define EPD_PIN_CLK  6
#define EPD_PIN_CS   10
#define EPD_PIN_DC   3
#define EPD_PIN_RST  4
#define EPD_PIN_BUSY 5

/*
 * 面板批次：SE0398NZ07-FNG-A0 / A1 的栅极寻址不同。
 * 当前实现按 A0 面板实测标定（物理行 p<=299 → y=2p；p>=300 → y=2p-599），
 * 标定证据见 evidence/M1/epd_hello_upperram.log。A1 批次未验证。
 */
#define EPD_PANEL_IS_A1 0

typedef enum {
    EPD_BLACK  = 0,
    EPD_WHITE  = 1,
    EPD_YELLOW = 2,
    EPD_RED    = 3,
} epd_color_t;

/* 初始化 GPIO/SPI 并做一次面板复位与上电 */
esp_err_t epd_init(void);

/* 帧缓冲操作（2 bpp，每字节 4 像素，左侧像素在高位） */
/* 直接访问整帧缓冲，便于把手机端渲染好的 2bpp 数据整块拷进来 */
uint8_t *epd_framebuffer(void);

void epd_fill(epd_color_t color);
void epd_set_pixel(int x, int y, epd_color_t color);
void epd_fill_rect(int x, int y, int w, int h, epd_color_t color);
/* ASCII 8x11 点阵：字符步进 8*scale 像素、高 11*scale；非 ASCII 显示为 ? */
void epd_draw_text(const char *text, int x, int y, int scale, epd_color_t color);

/* 发送整帧并触发全屏刷新（约 15~16 s，阻塞） */
esp_err_t epd_display(void);

/* 关断升压并进入深度睡眠（需复位唤醒） */
void epd_sleep(void);
