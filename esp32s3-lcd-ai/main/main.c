#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/i2c_master.h"
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
    /* NVS is required by the WiFi stack. */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* I2C bus -> expander -> display + touch + LVGL. */
    i2c_master_bus_handle_t i2c_bus;
    ESP_ERROR_CHECK(bsp_i2c_init(&i2c_bus));
    esp_io_expander_handle_t expander;
    ESP_ERROR_CHECK(bsp_expander_init(i2c_bus, &expander));

    if (bsp_display_init(i2c_bus, expander) == NULL) {
        ESP_LOGE(TAG, "display init failed; nothing to show");
        return;
    }

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
