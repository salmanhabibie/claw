#include "tts.h"
#include "audio.h"
#include "sdcard.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "tts";

/* Cache clips only up to this text length: short phrases (greetings,
 * reminders, confirmations) repeat a lot; long unique answers don't. */
#define TTS_CACHE_TEXT_MAX 600

/* FNV-1a over voice + model + text -> stable 8-hex-char cache key. */
static void cache_key(const char *text, char out[16])
{
    uint32_t h = 2166136261u;
    const char *parts[] = { CONFIG_ELEVENLABS_VOICE_ID, CONFIG_ELEVENLABS_MODEL,
                            text };
    for (size_t p = 0; p < 3; p++) {
        for (const char *s = parts[p]; *s; s++) {
            h ^= (uint8_t)*s;
            h *= 16777619u;
        }
        h ^= 0xff; h *= 16777619u;       /* separator so fields can't blend */
    }
    snprintf(out, 16, "%08lx", (unsigned long)h);
}

/* ElevenLabs streams raw 16-bit mono PCM (output_format=pcm_24000). We collect
 * the WHOLE clip into a PSRAM buffer first, then play it in one smooth pass.
 * Playing chunks straight from the HTTP callback underruns the I2S DMA whenever
 * the network stalls, which is heard as stutter / crackle. */
typedef struct {
    uint8_t *buf;
    size_t   len;
    size_t   cap;
} tts_state_t;

static esp_err_t http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA) {
        return ESP_OK;
    }
    /* Only collect a 200 response; otherwise the body is a JSON error, not audio. */
    if (esp_http_client_get_status_code(evt->client) != 200) {
        return ESP_OK;
    }

    tts_state_t *st = (tts_state_t *)evt->user_data;
    if (st->len + evt->data_len > st->cap) {
        size_t ncap = st->cap ? st->cap : 65536;
        while (ncap < st->len + evt->data_len) ncap *= 2;
        uint8_t *nb = heap_caps_realloc(st->buf, ncap, MALLOC_CAP_SPIRAM);
        if (nb == NULL) {
            ESP_LOGE(TAG, "OOM buffering audio (%u bytes)", (unsigned)ncap);
            return ESP_FAIL;
        }
        st->buf = nb;
        st->cap = ncap;
    }
    memcpy(st->buf + st->len, evt->data, evt->data_len);
    st->len += evt->data_len;
    return ESP_OK;
}

void tts_say(const char *text)
{
    if (strlen(CONFIG_ELEVENLABS_API_KEY) == 0) {
        ESP_LOGE(TAG, "no ElevenLabs API key configured");
        return;
    }

    /* SD cache first: a hit skips the network entirely (instant + free). */
    bool cacheable = strlen(text) <= TTS_CACHE_TEXT_MAX;
    char key[16] = {0};
    if (cacheable) {
        cache_key(text, key);
        size_t clen = 0;
        uint8_t *clip = sd_tts_cache_get(key, &clen);
        if (clip != NULL) {
            audio_play_mono16(clip, clen & ~(size_t)1);
            free(clip);
            return;
        }
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "text", text);
    cJSON_AddStringToObject(root, "model_id", CONFIG_ELEVENLABS_MODEL);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) {
        return;
    }

    char url[256];
    snprintf(url, sizeof(url),
             "https://api.elevenlabs.io/v1/text-to-speech/%s?output_format=pcm_24000",
             CONFIG_ELEVENLABS_VOICE_ID);

    esp_err_t err = ESP_FAIL;
    int status = 0;
    tts_state_t st = {0};

    /* Retry: DNS / TLS connect can fail transiently right after WiFi is up. */
    for (int attempt = 1; attempt <= 3; attempt++) {
        free(st.buf);
        st = (tts_state_t){0};
        esp_http_client_config_t config = {
            .url = url,
            .method = HTTP_METHOD_POST,
            .event_handler = http_event,
            .user_data = &st,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = 60000,
            .buffer_size = 2048,
            .buffer_size_tx = 1024,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        esp_http_client_set_header(client, "xi-api-key", CONFIG_ELEVENLABS_API_KEY);
        esp_http_client_set_header(client, "content-type", "application/json");
        esp_http_client_set_header(client, "accept", "audio/pcm");
        esp_http_client_set_post_field(client, body, strlen(body));

        err = esp_http_client_perform(client);
        status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);

        if (err == ESP_OK) {
            break;   /* got a response (200 or an HTTP error) */
        }
        ESP_LOGW(TAG, "TTS attempt %d failed: %s; retrying...",
                 attempt, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    free(body);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TTS request failed after retries: %s", esp_err_to_name(err));
    } else if (status != 200) {
        ESP_LOGW(TAG, "TTS HTTP %d (check API key / voice ID)", status);
    } else if (st.len < 2) {
        ESP_LOGW(TAG, "got 0 audio bytes - check voice ID / output_format");
    } else {
        ESP_LOGI(TAG, "TTS got HTTP 200, %u PCM bytes (%.1f s); playing",
                 (unsigned)st.len, st.len / 2.0f / 24000.0f);
        /* One smooth pass from the complete buffer — no network-stall gaps. */
        audio_play_mono16(st.buf, st.len & ~(size_t)1);
        if (cacheable) {
            sd_tts_cache_put(key, st.buf, st.len & ~(size_t)1);
        }
    }
    free(st.buf);
}
