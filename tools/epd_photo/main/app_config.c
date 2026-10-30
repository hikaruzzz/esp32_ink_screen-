#include "app_config.h"

#include <string.h>

#include "app_config_local.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NVS_NAMESPACE "cfg"
#define KEY_SSID      "ap_ssid"
#define KEY_PASS      "ap_pass"

static const char *TAG = "config";
static app_config_t s_cfg;

static void config_seed_defaults(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    strlcpy(s_cfg.ap_ssid, APP_CONFIG_DEFAULT_AP_SSID, sizeof(s_cfg.ap_ssid));
    strlcpy(s_cfg.ap_password, APP_CONFIG_DEFAULT_AP_PASSWORD, sizeof(s_cfg.ap_password));
}

static esp_err_t config_save(const app_config_t *cfg)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, KEY_SSID, cfg->ap_ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(h, KEY_PASS, cfg->ap_password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t app_config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 需要重建 (%s)", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t h;
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    size_t len = sizeof(s_cfg.ap_ssid);
    const esp_err_t e1 = nvs_get_str(h, KEY_SSID, s_cfg.ap_ssid, &len);
    len = sizeof(s_cfg.ap_password);
    const esp_err_t e2 = nvs_get_str(h, KEY_PASS, s_cfg.ap_password, &len);
    nvs_close(h);

    if (e1 != ESP_OK || e2 != ESP_OK) {
        config_seed_defaults();
        ESP_LOGI(TAG, "NVS 无配置，写入默认值 SSID=\"%s\"", s_cfg.ap_ssid);
        return config_save(&s_cfg);
    }

    ESP_LOGI(TAG, "载入 NVS 配置 SSID=\"%s\"", s_cfg.ap_ssid);
    return ESP_OK;
}

const app_config_t *app_config_get(void)
{
    return &s_cfg;
}

esp_err_t app_config_set(const char *ssid, const char *password)
{
    if (ssid == NULL || password == NULL ||
        strlen(ssid) >= sizeof(s_cfg.ap_ssid) ||
        strlen(password) >= sizeof(s_cfg.ap_password)) {
        return ESP_ERR_INVALID_ARG;
    }

    app_config_t next;
    memset(&next, 0, sizeof(next));
    strlcpy(next.ap_ssid, ssid, sizeof(next.ap_ssid));
    strlcpy(next.ap_password, password, sizeof(next.ap_password));

    const esp_err_t err = config_save(&next);
    if (err == ESP_OK) {
        s_cfg = next;
        ESP_LOGI(TAG, "配置已更新 SSID=\"%s\"（下次启动生效）", s_cfg.ap_ssid);
    }
    return err;
}

esp_err_t app_config_reset(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);

    config_seed_defaults();
    ESP_LOGI(TAG, "配置已复位为默认值 SSID=\"%s\"", s_cfg.ap_ssid);
    return config_save(&s_cfg);
}
