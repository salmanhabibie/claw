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
    "reminders.c"
    "memory.c"
    "sdcard.c"
    "imu.c"
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
    # microSD (journal + TTS cache)
    esp_driver_sdspi
    sdmmc
    fatfs
    vfs
    # managed components (see idf_component.yml)
    lvgl
    esp_lvgl_port
    esp_lcd_spd2010
    esp_lcd_touch
    esp_lcd_touch_spd2010
    esp_io_expander
    esp_io_expander_tca9554
)

# BLE WiFi provisioning (used only when CONFIG_WANDA_BLE_PROV is set). These are
# required UNCONDITIONALLY: CONFIG_* values are not available during the early
# requirement-expansion phase, so `if(CONFIG_WANDA_BLE_PROV)` here would never be
# true. They are cheap when Bluetooth is disabled: `bt` registers as an empty
# component, and only wifi.c's `#if CONFIG_WANDA_BLE_PROV` code references them.
list(APPEND reqs wifi_provisioning bt)

# Wake word (esp-sr) is opt-in. It is NOT listed here: when enabled via
# idf_component.yml, esp-sr becomes a managed dependency of `main` and its
# headers/libs are added automatically. wake.c detects it with __has_include,
# so the default build (no esp-sr) stays small and always compiles.

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
    REQUIRES ${reqs}
)
