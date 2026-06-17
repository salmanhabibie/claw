#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_io_expander.h"

#include "wifi.h"
#include "claude_client.h"
#include "board.h"
#include "display.h"
#include "chat_ui.h"

static const char *TAG = "app";

/* TLS handshake + cJSON need a generous stack. */
#define CLAUDE_TASK_STACK (1024 * 24)

static QueueHandle_t s_prompt_q;

/* Stop here without rebooting, logging why. A graceful halt keeps the USB
 * Serial/JTAG console alive so the error stays readable in the monitor. */
static void halt(const char *why)
{
    while (1) {
        ESP_LOGE(TAG, "HALT (init failed): %s", why);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

/* Pulls typed prompts off the queue, calls Claude (blocking HTTPS), and pushes
 * the reply back to the UI. Kept off the LVGL task so the screen stays live. */
static void claude_worker(void *arg)
{
    (void)arg;
    char *prompt = NULL;
    for (;;) {
        if (xQueueReceive(s_prompt_q, &prompt, portMAX_DELAY) == pdTRUE) {
            char *reply = claude_ask(prompt);
            free(prompt);
            chat_ui_add_assistant(reply);
            free(reply);
        }
    }
}

void app_main(void)
{
    /* Give the native USB Serial/JTAG a moment to re-enumerate after the
     * post-flash reset so the monitor can reattach and catch the logs below. */
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "=== ESP32-S3-Touch-LCD-1.46B AI assistant starting ===");

    /* NVS is required by the WiFi stack. */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* I2C bus -> expander -> display + touch + LVGL. */
    ESP_LOGI(TAG, "init I2C bus");
    if (bsp_i2c_init() != ESP_OK) {
        halt("I2C");
    }
    ESP_LOGI(TAG, "init TCA9554 expander");
    esp_io_expander_handle_t expander;
    if (bsp_expander_init(&expander) != ESP_OK) {
        halt("expander (check TCA9554 address / wiring)");
    }
    ESP_LOGI(TAG, "init display");
    if (bsp_display_init(expander) == NULL) {
        halt("display (see the 'display' log line above for the failing step)");
    }
    ESP_LOGI(TAG, "display ready");

    s_prompt_q = xQueueCreate(4, sizeof(char *));
    chat_ui_init(s_prompt_q);

    if (strlen(CONFIG_CLAUDE_API_KEY) == 0) {
        chat_ui_set_status("No API key - set it in menuconfig");
        ESP_LOGE(TAG, "no API key configured");
        return;
    }

    chat_ui_set_status("Connecting to WiFi...");
    if (wifi_connect() != ESP_OK) {
        chat_ui_set_status("WiFi failed - check menuconfig");
        return;
    }
    chat_ui_set_status(CONFIG_CLAUDE_MODEL);

    xTaskCreate(claude_worker, "claude", CLAUDE_TASK_STACK, NULL, 5, NULL);
    ESP_LOGI(TAG, "ready");
}
