/*
 * 墨水屏相框：上电默认热点打开，屏幕直接显示上次的照片（像画框一样）。
 * BOOT 键（GPIO9，引脚见 docs/pinmap.md）：
 *   单击 —— 打开/关闭热点，画面保持照片不变
 *   双击 —— 照片 ↔ 热点信息页（看 SSID/密码/网址，再双击回到照片）
 * 也就是说，除了双击那一下，屏幕永远是照片，热点开没开都不影响画面。
 * 手机连上热点后打开 http://192.168.4.1 上传照片。
 * 还没有照片时，上电显示热点信息页。
 *
 * 任务划分：
 *   main      —— 初始化、开机投递第一个显示作业
 *   display   —— 独占面板与 SPI：重刷/解码刷屏/切热点/切信息页都串行走这里（app_jobs.c）
 *   httpd     —— 收图、配置
 *   button    —— BOOT 键单击/双击检测
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
#include "web_server.h"
#include "wifi_ap.h"

static const char *TAG = "epd_photo";

static void on_button_click(void)
{
    ESP_LOGI(TAG, "BOOT 键单击：切换热点");
    app_jobs_submit_toggle_wifi();
}

static void on_button_double_click(void)
{
    ESP_LOGI(TAG, "BOOT 键双击：切换热点信息页");
    app_jobs_submit_toggle_info();
}

void app_main(void)
{
    ESP_LOGI(TAG, "启动，剩余堆 %u B", (unsigned)esp_get_free_heap_size());

    ESP_ERROR_CHECK(app_config_init());
    const app_config_t *cfg = app_config_get();

    ESP_ERROR_CHECK(epd_init());

    /* 照片分区缺失只影响“能否显示照片”，不该阻止启动 */
    photo_store_init();

    /* 热点上电即打开：手机连上 192.168.4.1 就能传图，不用先按键。
     * 此时显示任务还没创建，不存在并发。 */
    ESP_ERROR_CHECK(wifi_ap_init(cfg->ap_ssid, cfg->ap_password));
    const esp_err_t ap_err = wifi_ap_set_enabled(true);
    if (ap_err != ESP_OK) {
        /* 热点起不来不该拖垮相框：照样显示照片，按键还能再试 */
        ESP_LOGE(TAG, "开机打开热点失败 %s", esp_err_to_name(ap_err));
    }

    ESP_ERROR_CHECK(app_jobs_init());

    size_t photo_len = 0;
    if (photo_store_info(&photo_len, NULL, NULL, NULL) == ESP_OK) {
        ESP_LOGI(TAG, "上电显示分区里的照片（%u B）", (unsigned)photo_len);
        ESP_ERROR_CHECK(app_jobs_submit_decode());
    } else {
        /* 还没有照片：显示热点信息页，方便照着连手机 */
        ESP_ERROR_CHECK(app_jobs_submit_show_info());
    }

    ESP_ERROR_CHECK(web_server_start());
    ESP_ERROR_CHECK(button_init(on_button_click, on_button_double_click));

    ESP_LOGI(TAG, "就绪，剩余堆 %u B", (unsigned)esp_get_free_heap_size());
}
