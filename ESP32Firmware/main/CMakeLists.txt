idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_server esp_http_client esp_wifi nvs_flash esp_netif esp_driver_gpio esp_rom esp-tls
)
