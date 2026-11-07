#include "screen_ui.h"

#include <stdio.h>
#include <string.h>

#include "epd_se0398nz07.h"
#include "font8x11.h"

#define MARGIN 24

static void draw_centered(const char *text, int y, int scale, epd_color_t color)
{
    const int w = (int)strlen(text) * FONT8X11_WIDTH * scale;
    epd_draw_text(text, (EPD_WIDTH - w) / 2, y, scale, color);
}

/* 长 SSID/密码自动缩小到能放下为止 */
static void draw_centered_fit(const char *text, int y, int max_scale, epd_color_t color)
{
    const int len = (int)strlen(text);
    int scale = max_scale;
    while (scale > 1 && len * FONT8X11_WIDTH * scale > EPD_WIDTH - 2 * MARGIN) {
        --scale;
    }
    draw_centered(text, y, scale, color);
}

/* 8x11 点阵只有 ASCII：中文等多字节字符统一显示成单个 '?' */
static void sanitize(const char *in, char *out, size_t out_sz)
{
    size_t n = 0;
    bool last_was_unknown = false;

    for (const unsigned char *p = (const unsigned char *)in; *p != '\0' && n + 1 < out_sz; ++p) {
        if (*p < 0x20 || *p > 0x7E) {
            if (!last_was_unknown) {
                out[n++] = '?';
                last_was_unknown = true;
            }
        } else {
            out[n++] = (char)*p;
            last_was_unknown = false;
        }
    }
    out[n] = '\0';
}

void screen_draw_wifi_status(const app_config_t *cfg, bool ap_on)
{
    char ssid[APP_CONFIG_SSID_MAX];
    char pass[APP_CONFIG_PASS_MAX];
    char line[128];

    sanitize(cfg->ap_ssid, ssid, sizeof(ssid));
    sanitize(cfg->ap_password, pass, sizeof(pass));

    epd_fill(EPD_WHITE);
    epd_fill_rect(0, 0, EPD_WIDTH, 12, ap_on ? EPD_YELLOW : EPD_RED);
    epd_fill_rect(0, EPD_HEIGHT - 12, EPD_WIDTH, 12, EPD_BLACK);

    draw_centered(ap_on ? "Wi-Fi ON" : "Wi-Fi OFF", 48, 5, EPD_BLACK);

    if (ap_on) {
        snprintf(line, sizeof(line), "SSID: %s", ssid[0] ? ssid : "-");
        draw_centered_fit(line, 150, 4, EPD_BLACK);

        if (pass[0] != '\0') {
            snprintf(line, sizeof(line), "PASS: %s", pass);
        } else {
            snprintf(line, sizeof(line), "PASS: (open)");
        }
        draw_centered_fit(line, 225, 4, EPD_RED);

        draw_centered("http://192.168.4.1", 320, 3, EPD_BLACK);
        draw_centered("Open it in your phone browser", 375, 2, EPD_BLACK);
        draw_centered("Double-click BOOT: back to photo", 440, 2, EPD_BLACK);
        draw_centered("Single click BOOT: Wi-Fi on/off", 480, 2, EPD_RED);
    } else {
        draw_centered("Wi-Fi is off", 180, 3, EPD_BLACK);
        draw_centered("Single click BOOT: turn Wi-Fi on", 240, 2, EPD_RED);
        draw_centered("Double-click BOOT: back to photo", 290, 2, EPD_BLACK);
        draw_centered("Photos still show without Wi-Fi", 380, 2, EPD_BLACK);
    }
}
