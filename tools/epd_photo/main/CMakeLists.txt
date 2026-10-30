idf_component_register(
    SRCS
        "epd_photo.c"
        "epd_se0398nz07.c"
        "font8x11.c"
        "screen_ui.c"
        "app_jobs.c"
        "app_config.c"
        "wifi_ap.c"
        "button.c"
        "image_decode.c"
        "image_pipeline.c"
        "web_server.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "web/index.html"
    REQUIRES
        esp_wifi
        esp_netif
        esp_http_server
        nvs_flash
        esp_driver_gpio
        esp_driver_spi
        esp_timer
        espressif__esp_jpeg
)
