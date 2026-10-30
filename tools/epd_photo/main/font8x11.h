#pragma once

#include <stdint.h>

#define FONT8X11_WIDTH  8
#define FONT8X11_HEIGHT 11

/*
 * 8x11 ASCII 点阵，覆盖可打印字符 0x20~0x7E。
 * 返回指向 11 字节行数据的指针：每行 1 字节，bit7 = 最左像素。
 * 非 ASCII（如中文 SSID）返回空格字形。
 */
const uint8_t *font8x11_rows(char c);
