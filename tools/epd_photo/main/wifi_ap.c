#include "wifi_ap.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

static const char *TAG = "wifi_ap";
static bool s_ap_on;

esp_err_t wifi_ap_start(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (ap_netif == NULL) {
        return ESP_FAIL;
    }

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t ap_cfg = {0};
    strlcpy((char *)ap_cfg.ap.ssid, ssid, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = (uint8_t)strlen(ssid);
    ap_cfg.ap.max_connection = 2;
    ap_cfg.ap.channel = 6;

    const bool use_wpa2 = (password != NULL && strlen(password) >= 8);
    if (use_wpa2) {
        strlcpy((char *)ap_cfg.ap.password, password, sizeof(ap_cfg.ap.password));
        ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
    }

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    if (err != ESP_OK) {
        return err;
    }

    s_ap_on = true;

    esp_netif_ip_info_t ip = {0};
    esp_netif_get_ip_info(ap_netif, &ip);
    ESP_LOGI(TAG, "热点已启动 SSID=\"%s\" 加密方式=%s 地址=http://" IPSTR,
             ssid, use_wpa2 ? "WPA2" : "开放", IP2STR(&ip.ip));
    return ESP_OK;
}

bool wifi_ap_is_enabled(void)
{
    return s_ap_on;
}

esp_err_t wifi_ap_set_enabled(bool enabled)
{
    if (enabled == s_ap_on) {
        return ESP_OK;
    }

    /* 只停/启射频，不动 esp_netif 与已写入的 Wi-Fi 配置；
     * 重新打开后热点名与密码保持不变，手机需要重新连接。 */
    const esp_err_t err = enabled ? esp_wifi_start() : esp_wifi_stop();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s热点失败 %s", enabled ? "打开" : "关闭", esp_err_to_name(err));
        return err;
    }

    s_ap_on = enabled;
    ESP_LOGI(TAG, "热点已%s", enabled ? "打开" : "关闭");
    return ESP_OK;
}
