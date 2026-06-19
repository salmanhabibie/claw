#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "esp_io_expander.h"
#include "esp_netif_sntp.h"

#include "wifi.h"
#include "claude_client.h"
#include "board.h"
#include "display.h"
#include "chat_ui.h"
#include "audio.h"
#include "tts.h"
#include "stt.h"

static const char *TAG = "app";

/* TLS handshake + audio buffers need a generous stack. */
#define VOICE_TASK_STACK (1024 * 32)

/* Upper bound for one recording; voice-activity detection usually stops sooner. */
#define RECORD_SECONDS 8
/* Shorter window when auto-listening for a follow-up (silence ends the chat). */
#define FOLLOWUP_SECONDS 6

static SemaphoreHandle_t s_talk_sem;

/* Stop here without rebooting, logging why (keeps the USB console alive). */
static void halt(const char *why)
{
    while (1) {
        ESP_LOGE(TAG, "HALT (init failed): %s", why);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

/* One full voice turn, triggered by the TALK button:
 *   record mic -> ElevenLabs STT -> Claude -> ElevenLabs TTS (spoken reply). */
static void voice_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "voice task started (free internal heap=%u)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    /* Spoken greeting once, on this task's large (TLS-capable) stack. */
    if (strlen(CONFIG_ELEVENLABS_API_KEY) > 0) {
        chat_ui_set_status("Tes suara...");
        chat_ui_set_state(UI_SPEAKING);
        tts_say("Halo! Ini asisten Claude. Tekan tombol bicara, "
                "lalu ngomong setelah muncul tulisan mendengarkan.");
    }
    chat_ui_set_status("Tap untuk bicara");
    chat_ui_set_state(UI_IDLE);

    for (;;) {
        /* Wait for a tap to begin a conversation. */
        xSemaphoreTake(s_talk_sem, portMAX_DELAY);

        /* Conversation loop: after each spoken reply we listen again for a
         * follow-up. Staying silent (empty transcript) ends the conversation. */
        bool first = true;
        for (;;) {
            int max_rec = first ? RECORD_SECONDS : FOLLOWUP_SECONDS;
            first = false;

            int16_t *pcm = heap_caps_malloc(
                (size_t)RECORD_SECONDS * MIC_SAMPLE_RATE * sizeof(int16_t),
                MALLOC_CAP_SPIRAM);
            if (pcm == NULL) {
                chat_ui_set_status("Memori penuh");
                break;
            }

            chat_ui_set_status("Mendengarkan... (diam untuk berhenti)");
            chat_ui_set_state(UI_LISTENING);
            size_t n = mic_record(pcm, max_rec);

            chat_ui_set_status("Memproses suara...");
            chat_ui_set_state(UI_THINKING);
            char *text = stt_transcribe(pcm, n);
            free(pcm);

            if (text == NULL || text[0] == '\0') {
                free(text);
                break;   /* no speech -> end the conversation */
            }
            chat_ui_set_response(text);

            chat_ui_set_status("Berpikir...");
            char *reply = claude_ask(text);
            free(text);
            if (reply == NULL) {
                chat_ui_set_status("Gagal menghubungi Claude");
                break;
            }
            chat_ui_set_response(reply);

            chat_ui_set_status("Berbicara...");
            chat_ui_set_state(UI_SPEAKING);
            tts_say(reply);
            free(reply);

            /* Brief settle so the mic doesn't catch the speaker's tail. */
            vTaskDelay(pdMS_TO_TICKS(300));
        }

        chat_ui_set_status("Tap untuk bicara");
        chat_ui_set_state(UI_IDLE);
        xSemaphoreTake(s_talk_sem, 0);   /* drop taps that arrived mid-chat */
    }
}

void app_main(void)
{
    /* Latch board power / enable the speaker amplifier first (GPIO7 HIGH). */
    bsp_power_on();

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
        /* Local 440 Hz beep: no network needed. If you hear this, the speaker
         * hardware path (I2S -> DAC -> amp) works, independent of WiFi/TTS. */
        ESP_LOGI(TAG, "playing local speaker test tone");
        audio_play_test_tone();
    }

    /* Microphone (I2S RX). Non-fatal: text still works without voice input. */
    if (mic_init() != ESP_OK) {
        ESP_LOGW(TAG, "mic init failed; voice input disabled");
    }

    /* Restore saved volume & brightness (defaults if never set). */
    audio_set_volume(bsp_nvs_get_u8("vol", 80));
    bsp_backlight_set(bsp_nvs_get_u8("bri", 100));

    s_talk_sem = xSemaphoreCreateBinary();
    chat_ui_init(s_talk_sem);

    chat_ui_set_status("Menyambung WiFi...");
    if (wifi_connect() != ESP_OK) {
        chat_ui_set_status("WiFi gagal - cek menuconfig");
    } else {
        chat_ui_set_status("Tap untuk bicara");
        /* Sync the clock over NTP (WIB, UTC+7). Non-blocking; the UI shows
         * "--:--" until the first sync arrives. */
        setenv("TZ", "WIB-7", 1);
        tzset();
        esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        esp_netif_sntp_init(&sntp_cfg);
    }

    /* Single worker: speaks the greeting, then handles each TALK turn. Using
     * one big-stack task (not two) keeps internal DRAM from being exhausted. */
    BaseType_t ok = xTaskCreate(voice_task, "voice", VOICE_TASK_STACK, NULL, 5, NULL);
    ESP_LOGI(TAG, "voice task create: %s (free internal heap=%u)",
             (ok == pdPASS) ? "ok" : "FAILED - reduce VOICE_TASK_STACK",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "ready");
}
