#include "tts.h"
#include "audio.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "tts";

/* Reason the last tts_say() produced no audio, shown on screen so a silent
 * Wanda explains itself without needing a serial monitor. */
static char s_last_err[200];

const char *tts_last_error(void)
{
    return s_last_err;
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
    tts_state_t *st = (tts_state_t *)evt->user_data;
    /* Collect the body whatever the status: on 200 it is audio, otherwise it
     * is the JSON error saying WHY the request was refused - which is exactly
     * what we want to show. Cap the error case; those bodies are tiny. */
    if (esp_http_client_get_status_code(evt->client) != 200 && st->len > 2048) {
        return ESP_OK;
    }
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

bool tts_say(const char *text)
{
    s_last_err[0] = '\0';

    if (strlen(CONFIG_ELEVENLABS_API_KEY) == 0) {
        ESP_LOGE(TAG, "no ElevenLabs API key configured");
        snprintf(s_last_err, sizeof(s_last_err), "API key ElevenLabs kosong");
        return false;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "text", text);
    cJSON_AddStringToObject(root, "model_id", CONFIG_ELEVENLABS_MODEL);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) {
        snprintf(s_last_err, sizeof(s_last_err), "Memori penuh");
        return false;
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

    bool spoke = false;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TTS request failed after retries: %s", esp_err_to_name(err));
        snprintf(s_last_err, sizeof(s_last_err), "Suara: jaringan gagal");
    } else if (status == 401 || status == 403) {
        ESP_LOGW(TAG, "TTS HTTP %d (API key rejected)", status);
        snprintf(s_last_err, sizeof(s_last_err),
                 "Suara: API key ditolak (%d)", status);
    } else if (status != 200) {
        /* Report the API's own explanation rather than a bare status code. */
        char detail[140] = "";
        if (st.buf != NULL && st.len > 0) {
            size_t n = (st.len < 511) ? st.len : 511;
            char *raw = malloc(n + 1);
            if (raw != NULL) {
                memcpy(raw, st.buf, n);
                raw[n] = '\0';
                ESP_LOGW(TAG, "TTS error body: %s", raw);
                cJSON *e = cJSON_Parse(raw);
                if (e != NULL) {
                    cJSON *d = cJSON_GetObjectItem(e, "detail");
                    const char *msg = NULL;
                    if (cJSON_IsObject(d)) {
                        msg = cJSON_GetStringValue(cJSON_GetObjectItem(d, "message"));
                        if (msg == NULL) {
                            msg = cJSON_GetStringValue(cJSON_GetObjectItem(d, "status"));
                        }
                    } else if (cJSON_IsString(d)) {
                        msg = d->valuestring;
                    }
                    if (msg != NULL) snprintf(detail, sizeof(detail), "%s", msg);
                    cJSON_Delete(e);
                }
                if (detail[0] == '\0') snprintf(detail, sizeof(detail), "%s", raw);
                free(raw);
            }
        }
        ESP_LOGW(TAG, "TTS HTTP %d for voice '%s' model '%s'", status,
                 CONFIG_ELEVENLABS_VOICE_ID, CONFIG_ELEVENLABS_MODEL);
        snprintf(s_last_err, sizeof(s_last_err), "Suara (%d): %s", status, detail);
    } else if (st.len < 2) {
        ESP_LOGW(TAG, "got 0 audio bytes - check voice ID / output_format");
        snprintf(s_last_err, sizeof(s_last_err), "Suara: audio kosong");
    } else {
        spoke = true;
        ESP_LOGI(TAG, "TTS got HTTP 200, %u PCM bytes (%.1f s); playing",
                 (unsigned)st.len, st.len / 2.0f / 24000.0f);
        /* One smooth pass from the complete buffer — no network-stall gaps. */
        audio_play_mono16(st.buf, st.len & ~(size_t)1);
    }
    free(st.buf);
    return spoke;
}
