#include "tts.h"
#include "audio.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "tts";

/* ElevenLabs streams raw 16-bit mono PCM (output_format=pcm_16000). A chunk
 * boundary can split a 16-bit sample, so carry the odd leftover byte over. */
typedef struct {
    uint8_t carry;
    bool    have_carry;
    size_t  played;     /* total PCM bytes sent to the speaker (diagnostic) */
} tts_state_t;

static esp_err_t http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA) {
        return ESP_OK;
    }
    /* Only play a 200 response; otherwise the body is a JSON error, not audio. */
    if (esp_http_client_get_status_code(evt->client) != 200) {
        return ESP_OK;
    }

    tts_state_t *st = (tts_state_t *)evt->user_data;
    const uint8_t *d = (const uint8_t *)evt->data;
    int len = evt->data_len;

    uint8_t *buf = malloc(len + 1);
    if (buf == NULL) {
        return ESP_FAIL;
    }
    int n = 0;
    if (st->have_carry) {
        buf[n++] = st->carry;
        st->have_carry = false;
    }
    memcpy(buf + n, d, len);
    n += len;
    if (n & 1) {                 /* keep the dangling byte for the next chunk */
        st->carry = buf[n - 1];
        st->have_carry = true;
        n -= 1;
    }
    if (n > 0) {
        audio_play_mono16(buf, n);
        st->played += n;
    }
    free(buf);
    return ESP_OK;
}

void tts_say(const char *text)
{
    if (strlen(CONFIG_ELEVENLABS_API_KEY) == 0) {
        ESP_LOGE(TAG, "no ElevenLabs API key configured");
        return;
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
             "https://api.elevenlabs.io/v1/text-to-speech/%s?output_format=pcm_16000",
             CONFIG_ELEVENLABS_VOICE_ID);

    esp_err_t err = ESP_FAIL;
    int status = 0;
    tts_state_t st = {0};

    /* Retry: DNS / TLS connect can fail transiently right after WiFi is up. */
    for (int attempt = 1; attempt <= 3; attempt++) {
        st = (tts_state_t){0};
        esp_http_client_config_t config = {
            .url = url,
            .method = HTTP_METHOD_POST,
            .event_handler = http_event,
            .user_data = &st,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = 30000,
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
    } else {
        ESP_LOGI(TAG, "TTS done: HTTP 200, played %u PCM bytes (%.1f s of audio)",
                 (unsigned)st.played, st.played / 2.0f / 16000.0f);
        if (st.played == 0) {
            ESP_LOGW(TAG, "got 0 audio bytes - check voice ID / output_format");
        }
    }
}
