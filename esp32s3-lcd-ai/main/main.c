#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_io_expander.h"

#include "wifi.h"
#include "claude_client.h"
#include "board.h"
#include "display.h"
#include "chat_ui.h"
#include "audio.h"
#include "tts.h"

static const char *TAG = "app";

/* TLS handshake + audio buffers need a generous stack. */
#define VOICE_TASK_STACK (1024 * 24)

static SemaphoreHandle_t s_talk_sem;

/* Stop here without rebooting, logging why (keeps the USB console alive). */
static void halt(const char *why)
{
    while (1) {
        ESP_LOGE(TAG, "HALT (init failed): %s", why);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

/* Wakes when the TALK button is tapped. Step 1: just speak a phrase, which
 * validates both touch and the speaker. Mic recording + STT + Claude come
 * in the next steps. */
static void voice_task(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_talk_sem, portMAX_DELAY);
        chat_ui_set_status("Speaking...");
        tts_say("Halo! Tombol bicara berfungsi. Fitur rekam suara sedang disiapkan.");
        chat_ui_set_status("Tap TALK to speak");
    }
}

/* One-shot speaker check on boot, in its own task for the TLS stack. */
static void speaker_test_task(void *arg)
{
    (void)arg;
    chat_ui_set_status("Speaking test...");
    tts_say("Halo! Ini tes suara dari asisten Claude.");
    chat_ui_set_status("Tap TALK to speak");
    vTaskDelete(NULL);
}

void app_main(void)
{
    /* Let the native USB Serial/JTAG re-enumerate so the monitor can reattach. */
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "=== ESP32-S3-Touch-LCD-1.46B AI assistant starting ===");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

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

    /* Speaker (PCM5101). Non-fatal: the UI still works without audio. */
    if (audio_init() != ESP_OK) {
        ESP_LOGW(TAG, "audio init failed; voice output disabled");
    } else {
        /* Local diagnostic beep (no network) to test the speaker path. */
        audio_play_test_tone();
    }

    s_talk_sem = xSemaphoreCreateBinary();
    chat_ui_init(s_talk_sem);

    chat_ui_set_status("Connecting to WiFi...");
    if (wifi_connect() != ESP_OK) {
        chat_ui_set_status("WiFi failed - check menuconfig");
    } else {
        chat_ui_set_status("Tap TALK to speak");
    }

    /* Boot speaker check + the TALK-button worker. */
    if (strlen(CONFIG_ELEVENLABS_API_KEY) > 0) {
        xTaskCreate(speaker_test_task, "spktest", VOICE_TASK_STACK, NULL, 5, NULL);
    }
    xTaskCreate(voice_task, "voice", VOICE_TASK_STACK, NULL, 5, NULL);
    ESP_LOGI(TAG, "ready");
}
