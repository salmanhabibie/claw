idf_component_register(
    SRCS
        "main.c"
        "wifi.c"
        "claude_client.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        nvs_flash
        esp_netif
        esp_event
        esp_http_client
        esp-tls
        mbedtls
        json
        driver
)
