/*
 * 墨水屏相框：上电默认热点关闭，屏幕直接显示上次的照片（像画框一样）。
 * BOOT 键（GPIO9，引脚见 docs/pinmap.md）短按 = 打开/关闭热点；
 * 热点打开时手机连上后打开 http://192.168.4.1 上传照片。
 * 还没有照片时，上电显示一张“按 BOOT 打开热点”的提示页。
 *
 * 任务划分：
 *   main      —— 初始化、画开机页
 *   display   —— 独占面板与 SPI：重刷/解码刷屏/切换热点都串行走这里（app_jobs.c）
 *   httpd     —— 收图、配置
 *   button    —— BOOT 键短按检测
 *
 * 串口控制台是 USB Serial/JTAG（COM4）。
 */

#include "app_config.h"
#include "app_jobs.h"
#include "button.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "epd_se0398nz07.h"
#include "photo_store.h"
#include "screen_ui.h"
#include "web_server.h"
#include "wifi_ap.h"

static const char *TAG = "epd_photo";

static void on_button_press(void)
{
    ESP_LOGI(TAG, "BOOT 键：切换热点");
    app_jobs_submit_toggle_wifi();
}

void app_main(void)
{
    ESP_LOGI(TAG, "启动，剩余堆 %u B", (unsigned)esp_get_free_heap_size());

    ESP_ERROR_CHECK(app_config_init());
    const app_config_t *cfg = app_config_get();

    ESP_ERROR_CHECK(epd_init());

    /* 照片分区缺失只影响“关热点后还原照片”，不该阻止启动 */
    photo_store_init();

    /* 热点只配置不启动：上电默认关着，按 BOOT 才开（此时显示任务还没创建，
     * 不存在并发）。网页服务照常启动，热点一开就能访问。 */
    ESP_ERROR_CHECK(wifi_ap_init(cfg->ap_ssid, cfg->ap_password));
    ESP_ERROR_CHECK(app_jobs_init());

    size_t photo_len = 0;
    if (photo_store_info(&photo_len, NULL, NULL, NULL) == ESP_OK) {
        ESP_LOGI(TAG, "上电显示分区里的照片（%u B）", (unsigned)photo_len);
        ESP_ERROR_CHECK(app_jobs_submit_decode());
    } else {
        /* 还没有照片：画一张“按 BOOT 打开热点”的提示页 */
        screen_draw_wifi_status(cfg, false);
        ESP_ERROR_CHECK(app_jobs_submit_refresh());
    }

    ESP_ERROR_CHECK(web_server_start());
    ESP_ERROR_CHECK(button_init(on_button_press));

    ESP_LOGI(TAG, "就绪，剩余堆 %u B", (unsigned)esp_get_free_heap_size());
}
