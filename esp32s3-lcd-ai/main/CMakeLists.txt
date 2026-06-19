set(srcs
    "main.c"
    "wifi.c"
    "claude_client.c"
    "board.c"
    "display.c"
    "chat_ui.c"
    "audio.c"
    "tts.c"
    "stt.c"
    "tools.c"
    "wake.c"
)

set(reqs
    # networking + Claude API
    esp_wifi
    nvs_flash
    esp_netif
    esp_event
    esp_http_client
    esp-tls
    mbedtls
    json
    # peripherals
    driver
    esp_driver_i2c
    esp_driver_spi
    esp_driver_gpio
    esp_driver_i2s
    esp_driver_ledc
    esp_lcd
    # managed components (see idf_component.yml)
    lvgl
    esp_lvgl_port
    esp_lcd_spd2010
    esp_lcd_touch
    esp_lcd_touch_spd2010
    esp_io_expander
    esp_io_expander_tca9554
)

# Wake word (esp-sr) is opt-in: only required when enabled, so the default
# build doesn't pull the large esp-sr component. Also uncomment it in
# idf_component.yml when turning this on.
if(CONFIG_VEE_WAKE_WORD)
    list(APPEND reqs esp-sr)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
    REQUIRES ${reqs}
)
