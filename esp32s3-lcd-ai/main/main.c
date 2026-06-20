#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <stdlib.h>
#include <ctype.h>
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
#include "wake.h"
#include "reminders.h"
#include "memory.h"

static const char *TAG = "app";

/* TLS handshake + audio buffers need a generous stack. */
#define VOICE_TASK_STACK (1024 * 32)

/* Upper bound for one recording; voice-activity detection usually stops sooner. */
#define RECORD_SECONDS 12
/* Shorter window when auto-listening for a follow-up (silence ends the chat). */
#define FOLLOWUP_SECONDS 8
/* Trailing silence (ms) that ends a capture once you've started talking. Long so
 * a pause mid-sentence doesn't cut you off. */
#define CONV_TRAIL_MS 2500
/* Window for one STT wake-listen capture ("Wanda" plus an optional command). */
#define WAKE_LISTEN_SECONDS 4

static SemaphoreHandle_t s_talk_sem;
static bool s_wake_ok;          /* true if a wake word model loaded */

/* Stop here without rebooting, logging why (keeps the USB console alive). */
static void halt(const char *why)
{
    while (1) {
        ESP_LOGE(TAG, "HALT (init failed): %s", why);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

/* True if a short utterance is a "stop talking" command, so the user can end
 * the conversation by voice. Only short phrases match, to avoid false hits in
 * normal sentences that happen to contain one of these words. */
static bool is_stop_command(const char *text)
{
    if (text == NULL) return false;
    size_t len = strlen(text);
    if (len == 0 || len > 28) return false;

    char low[32];
    size_t j = 0;
    for (size_t i = 0; i < len && j < sizeof(low) - 1; i++) {
        unsigned char c = (unsigned char)text[i];
        low[j++] = isalpha(c) ? (char)tolower(c) : ' ';
    }
    low[j] = '\0';

    static const char *kw[] = {
        "diam", "stop", "berhenti", "cukup", "selesai", "udahan", NULL
    };
    for (int k = 0; kw[k] != NULL; k++) {
        if (strstr(low, kw[k]) != NULL) return true;
    }
    return false;
}

/* Case-insensitive substring search. */
static const char *ci_strstr(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);
    for (; *hay; hay++) {
        if (strncasecmp(hay, needle, nl) == 0) return hay;
    }
    return NULL;
}

/* If `transcript` contains the wake word ("Wanda" + a few mishears), return
 * true. If a command was spoken after it (e.g. "Wanda, nyalakan lampu"), set
 * *cmd to a heap copy of that command so it can be acted on immediately. */
static bool wake_word_heard(const char *transcript, char **cmd)
{
    if (cmd) *cmd = NULL;
    if (transcript == NULL) return false;

    static const char *kw[] = {
        "wanda", "wonda", "wandah", "wanto", "wandi", "wandy", "uanda", NULL
    };
    const char *hit = NULL;
    size_t hitlen = 0;
    for (int i = 0; kw[i] != NULL; i++) {
        const char *p = ci_strstr(transcript, kw[i]);
        if (p != NULL && (hit == NULL || p < hit)) {
            hit = p;
            hitlen = strlen(kw[i]);
        }
    }
    if (hit == NULL) return false;

    const char *after = hit + hitlen;
    while (*after != '\0' && !isalpha((unsigned char)*after)) after++;
    if (cmd != NULL && strlen(after) >= 3) {
        *cmd = strdup(after);   /* command spoken in the same breath */
    }
    return true;
}

/* Wait until a conversation should start OR a reminder comes due. Returns true
 * to start a conversation (with *out_initial set to a command captured with the
 * wake word, or NULL); returns false when a reminder is due so the caller can
 * announce it. Trigger order: esp-sr wake, STT "Wanda" wake, screen tap. */
static bool wait_for_trigger(char **out_initial)
{
    *out_initial = NULL;

    /* 1. Offline esp-sr wake word, if a model is loaded. */
    if (s_wake_ok) {
        chat_ui_set_response("Ucapkan kata pemicu atau tap");
        int n = wake_chunk_samples();
        int16_t *buf = (n > 0)
            ? heap_caps_malloc((size_t)n * sizeof(int16_t), MALLOC_CAP_INTERNAL)
            : NULL;
        if (buf != NULL && mic_stream_start() == ESP_OK) {
            bool start = true;
            for (;;) {
                if (reminders_any_due(time(NULL))) { start = false; break; }
                if (xSemaphoreTake(s_talk_sem, 0) == pdTRUE) break;
                if (mic_read(buf, (size_t)n) != (size_t)n) continue;
                if (wake_detect(buf)) { ESP_LOGI(TAG, "wake word detected"); break; }
            }
            mic_stream_stop();
            free(buf);
            return start;
        }
        free(buf);
    }

    /* 2. STT-based "Wanda" wake (costs an STT call per spoken utterance). */
    else if (bsp_nvs_get_u8("wake", 1)) {
        chat_ui_set_response("Panggil \"Wanda\" atau tap");
        int16_t *pcm = heap_caps_malloc(
            (size_t)WAKE_LISTEN_SECONDS * MIC_SAMPLE_RATE * sizeof(int16_t),
            MALLOC_CAP_SPIRAM);
        if (pcm != NULL) {
            char *cmd = NULL;
            bool start = true;
            for (;;) {
                if (reminders_any_due(time(NULL))) { start = false; break; }
                if (xSemaphoreTake(s_talk_sem, 0) == pdTRUE) break;   /* tapped */
                bool speech = false;
                /* Short trail: react quickly to a "Wanda" call. */
                size_t n = mic_record_window(pcm, WAKE_LISTEN_SECONDS, 800, &speech);
                if (!speech) continue;            /* silence: skip the STT call */
                char *t = stt_transcribe(pcm, n);
                if (t != NULL) ESP_LOGI(TAG, "wake-listen heard: '%s'", t);
                if (wake_word_heard(t, &cmd)) { free(t); break; }
                free(t);
            }
            free(pcm);
            *out_initial = cmd;
            return start;
        }
    }

    /* 3. Tap only (timed wait so reminders can still fire). */
    chat_ui_set_response("Tap untuk bicara");
    for (;;) {
        if (xSemaphoreTake(s_talk_sem, pdMS_TO_TICKS(1000)) == pdTRUE) return true;
        if (reminders_any_due(time(NULL))) return false;
    }
}

/* Speak (and show) every reminder that is currently due. Runs on the voice
 * task so it never overlaps a conversation's audio. */
static void announce_due_reminders(void)
{
    char msg[96];
    while (reminders_pop_due(time(NULL), msg, sizeof(msg))) {
        ESP_LOGI(TAG, "reminder fired: %s", msg);
        chat_ui_set_status("Pengingat!");
        chat_ui_set_response(msg);
        chat_ui_set_state(UI_SPEAKING);
        audio_play_chime();
        char say[160];
        snprintf(say, sizeof(say), "Pengingat. %s", msg);
        tts_say(say);
        chat_ui_set_status("Tap untuk bicara");
        chat_ui_set_state(UI_IDLE);
    }
}

/* One full voice turn, triggered by the TALK button:
 *   record mic -> ElevenLabs STT -> Claude -> ElevenLabs TTS (spoken reply). */
static void voice_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "voice task started (free internal heap=%u)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    /* Load the wake word model (non-fatal: tap-to-talk still works). */
    s_wake_ok = wake_init();
    ESP_LOGI(TAG, "wake word: %s", s_wake_ok ? "enabled" : "disabled (tap only)");

    /* Spoken greeting once, on this task's large (TLS-capable) stack. Greet by
     * name if we remember the user. */
    bool wake_active = s_wake_ok || bsp_nvs_get_u8("wake", 1);
    if (strlen(CONFIG_ELEVENLABS_API_KEY) > 0) {
        char name[48];
        memory_get_name(name, sizeof(name));
        char greet[200];
        if (name[0] != '\0') {
            snprintf(greet, sizeof(greet),
                     "Halo %s! Aku Wanda. %s", name,
                     wake_active ? "Panggil namaku atau tap layar, lalu bicara."
                                 : "Tap layar lalu bicara setelah muncul tulisan "
                                   "mendengarkan.");
        } else {
            snprintf(greet, sizeof(greet), "%s",
                     wake_active
                     ? "Halo! Aku Wanda. Panggil namaku atau tap layar, lalu bicara."
                     : "Halo! Aku Wanda, asistenmu. Tap layar lalu bicara setelah "
                       "muncul tulisan mendengarkan.");
        }
        chat_ui_set_status("Tes suara...");
        chat_ui_set_state(UI_SPEAKING);
        tts_say(greet);
    }
    chat_ui_set_status("Tap untuk bicara");
    chat_ui_set_state(UI_IDLE);

    for (;;) {
        /* Fire any reminders that are due (chime + spoken). */
        announce_due_reminders();

        /* Wait for the wake word or a tap. May return a command captured in the
         * same breath as the wake word (e.g. "Wanda, nyalakan lampu"). Returns
         * false when a reminder came due while waiting -> loop to announce it. */
        char *initial = NULL;
        if (!wait_for_trigger(&initial)) {
            free(initial);
            continue;
        }

        /* Conversation loop: after each spoken reply we listen again for a
         * follow-up. Staying silent (empty transcript) ends the conversation. */
        bool first = true;
        for (;;) {
            char *text;

            if (first && initial != NULL) {
                /* Use the command spoken with the wake word; skip recording. */
                text = initial;
                initial = NULL;
            } else {
                int max_rec = first ? RECORD_SECONDS : FOLLOWUP_SECONDS;
                int16_t *pcm = heap_caps_malloc(
                    (size_t)RECORD_SECONDS * MIC_SAMPLE_RATE * sizeof(int16_t),
                    MALLOC_CAP_SPIRAM);
                if (pcm == NULL) {
                    chat_ui_set_status("Memori penuh");
                    break;
                }
                chat_ui_set_status("Mendengarkan... (diam untuk berhenti)");
                chat_ui_set_state(UI_LISTENING);
                size_t n = mic_record_window(pcm, max_rec, CONV_TRAIL_MS, NULL);
                chat_ui_set_status("Memproses suara...");
                chat_ui_set_state(UI_THINKING);
                text = stt_transcribe(pcm, n);
                free(pcm);
            }
            first = false;

            if (text == NULL || text[0] == '\0') {
                free(text);
                break;   /* no speech -> end the conversation */
            }
            chat_ui_set_response(text);

            if (is_stop_command(text)) {
                ESP_LOGI(TAG, "stop command: '%s'", text);
                free(text);
                break;   /* user said "diam"/"stop" -> end the conversation */
            }

            chat_ui_set_status("Berpikir...");
            chat_ui_set_state(UI_THINKING);
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

        free(initial);   /* normally already consumed/NULL; safe either way */
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

    /* Timezone (WIB, UTC+7) up front so reminder times are computed correctly
     * once NTP syncs. Load any saved reminders from NVS. */
    setenv("TZ", "WIB-7", 1);
    tzset();
    reminders_init();
    memory_init();

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
        /* Sync the clock over NTP. Non-blocking; the UI shows "--:--" until the
         * first sync arrives (TZ was already set above). */
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
