#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "wifi.h"
#include "claude_client.h"

/* This board can talk over the native USB Serial/JTAG port or a UART bridge,
 * depending on the "console output channel" picked in menuconfig. Initialize
 * whichever one is selected so the characters you type in the monitor actually
 * reach getchar(). The ESP32-S3-Touch-LCD-1.46B exposes a single native USB
 * port, so it must use USB Serial/JTAG. */
#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
#  include "driver/usb_serial_jtag.h"
#  if __has_include("driver/usb_serial_jtag_vfs.h")
#    include "driver/usb_serial_jtag_vfs.h"
#    define USJ_USE_DRIVER usb_serial_jtag_vfs_use_driver
#  else
#    include "esp_vfs_dev.h"
#    define USJ_USE_DRIVER esp_vfs_usb_serial_jtag_use_driver
#  endif
#else
#  include "driver/uart.h"
#  include "esp_vfs_dev.h"
#endif

static const char *TAG = "app";

/* Install the console driver so getchar() blocks until input arrives. */
static void console_init(void)
{
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    usb_serial_jtag_driver_config_t jtag_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&jtag_cfg);
    USJ_USE_DRIVER();
#else
    uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0);
    esp_vfs_dev_uart_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
#endif
}

/* Read one line from the console with local echo. Returns its length. */
static int read_line(char *buf, size_t maxlen)
{
    size_t i = 0;
    while (i < maxlen - 1) {
        int c = getchar();
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (c == '\r' || c == '\n') {
            putchar('\n');
            break;
        }
        if (c == 0x7f || c == 0x08) {  /* backspace / delete */
            if (i > 0) {
                i--;
                printf("\b \b");
            }
            continue;
        }
        buf[i++] = (char)c;
        putchar(c);  /* echo so the user sees what they type */
    }
    buf[i] = '\0';
    return (int)i;
}

void app_main(void)
{
    /* NVS is required by the WiFi stack. */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    console_init();

    printf("\n");
    printf("==================================\n");
    printf("   ESP32-S3 AI Assistant (Claude) \n");
    printf("==================================\n\n");

    if (strlen(CONFIG_CLAUDE_API_KEY) == 0) {
        printf("No API key configured.\n");
        printf("Run `idf.py menuconfig` -> \"AI Assistant Configuration\"\n");
        printf("and set your Anthropic API key, then reflash.\n");
        return;
    }

    printf("Connecting to WiFi \"%s\" ...\n", CONFIG_WIFI_SSID);
    if (wifi_connect() != ESP_OK) {
        printf("WiFi connection failed. Check SSID/password in menuconfig.\n");
        return;
    }
    printf("WiFi connected. Model: %s\n", CONFIG_CLAUDE_MODEL);
    printf("Type a question and press Enter.\n");

    char line[512];
    while (1) {
        printf("\nYou: ");
        int n = read_line(line, sizeof(line));
        if (n <= 0) {
            continue;
        }

        printf("(thinking...)\n");
        char *reply = claude_ask(line);
        if (reply != NULL) {
            printf("\nClaude: %s\n", reply);
            free(reply);
        } else {
            printf("\n[error] No response. Check the serial log, your API key, "
                   "and network.\n");
        }
        ESP_LOGD(TAG, "free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    }
}
