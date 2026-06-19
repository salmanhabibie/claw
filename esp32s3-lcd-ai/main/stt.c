#include "stt.h"
#include "audio.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "stt";

#define STT_URL      "https://api.elevenlabs.io/v1/speech-to-text"
#define STT_MODEL    "scribe_v2"   /* scribe_v1 is deprecated (removed 2026-07-09) */
#define STT_BOUNDARY "----esp32clawboundary7e2c"

/* Accumulates the (small) JSON response body. */
typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} resp_t;

static esp_err_t http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA) {
        return ESP_OK;
    }
    resp_t *r = (resp_t *)evt->user_data;
    if (r->len + evt->data_len + 1 > r->cap) {
        size_t ncap = (r->cap ? r->cap : 1024);
        while (ncap < r->len + evt->data_len + 1) ncap *= 2;
        char *nb = realloc(r->buf, ncap);
        if (nb == NULL) return ESP_FAIL;
        r->buf = nb;
        r->cap = ncap;
    }
    memcpy(r->buf + r->len, evt->data, evt->data_len);
    r->len += evt->data_len;
    r->buf[r->len] = '\0';
    return ESP_OK;
}

/* Fill a 44-byte canonical PCM WAV header (mono, 16-bit). */
static void wav_header(uint8_t *h, uint32_t pcm_bytes, uint32_t rate)
{
    uint32_t byte_rate  = rate * 2;          /* mono * 16-bit */
    uint32_t riff_size  = 36 + pcm_bytes;
    memcpy(h, "RIFF", 4);
    h[4] = riff_size; h[5] = riff_size >> 8; h[6] = riff_size >> 16; h[7] = riff_size >> 24;
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    h[16] = 16; h[17] = 0; h[18] = 0; h[19] = 0;   /* fmt chunk size */
    h[20] = 1;  h[21] = 0;                          /* PCM */
    h[22] = 1;  h[23] = 0;                          /* mono */
    h[24] = rate; h[25] = rate >> 8; h[26] = rate >> 16; h[27] = rate >> 24;
    h[28] = byte_rate; h[29] = byte_rate >> 8; h[30] = byte_rate >> 16; h[31] = byte_rate >> 24;
    h[32] = 2; h[33] = 0;                           /* block align */
    h[34] = 16; h[35] = 0;                          /* bits per sample */
    memcpy(h + 36, "data", 4);
    h[40] = pcm_bytes; h[41] = pcm_bytes >> 8; h[42] = pcm_bytes >> 16; h[43] = pcm_bytes >> 24;
}

char *stt_transcribe(const int16_t *pcm, size_t nsamples)
{
#if CONFIG_STT_PROVIDER_OPENAI
    const char *api_key = CONFIG_OPENAI_API_KEY;
#else
    const char *api_key = CONFIG_ELEVENLABS_API_KEY;
#endif
    if (strlen(api_key) == 0) {
        ESP_LOGE(TAG, "no STT API key configured");
        return NULL;
    }
    if (pcm == NULL || nsamples < MIC_SAMPLE_RATE / 4) {   /* < ~0.25s: nothing useful */
        ESP_LOGW(TAG, "too little audio (%u samples)", (unsigned)nsamples);
        return NULL;
    }

    const uint32_t pcm_bytes = (uint32_t)(nsamples * sizeof(int16_t));

    /* multipart/form-data: provider-specific text fields + the WAV file. Both
     * ElevenLabs and the OpenAI-compatible API return {"text": ...}. */
    char pre[384];
    int pre_len;
#if CONFIG_STT_PROVIDER_OPENAI
    if (strlen(CONFIG_STT_LANGUAGE) > 0) {
        pre_len = snprintf(pre, sizeof(pre),
            "--%s\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n%s\r\n"
            "--%s\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\n%s\r\n"
            "--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"rec.wav\"\r\n"
            "Content-Type: audio/wav\r\n\r\n",
            STT_BOUNDARY, CONFIG_STT_OPENAI_MODEL,
            STT_BOUNDARY, CONFIG_STT_LANGUAGE, STT_BOUNDARY);
    } else {
        pre_len = snprintf(pre, sizeof(pre),
            "--%s\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n%s\r\n"
            "--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"rec.wav\"\r\n"
            "Content-Type: audio/wav\r\n\r\n",
            STT_BOUNDARY, CONFIG_STT_OPENAI_MODEL, STT_BOUNDARY);
    }
#else
    pre_len = snprintf(pre, sizeof(pre),
        "--%s\r\nContent-Disposition: form-data; name=\"model_id\"\r\n\r\n%s\r\n"
        "--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"rec.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n",
        STT_BOUNDARY, STT_MODEL, STT_BOUNDARY);
#endif

    char post[64];
    int post_len = snprintf(post, sizeof(post), "\r\n--%s--\r\n", STT_BOUNDARY);

    const size_t body_len = pre_len + 44 + pcm_bytes + post_len;
    uint8_t *body = heap_caps_malloc(body_len, MALLOC_CAP_SPIRAM);
    if (body == NULL) {
        ESP_LOGE(TAG, "OOM allocating %u-byte body", (unsigned)body_len);
        return NULL;
    }
    size_t off = 0;
    memcpy(body + off, pre, pre_len);            off += pre_len;
    wav_header(body + off, pcm_bytes, MIC_SAMPLE_RATE); off += 44;
    memcpy(body + off, pcm, pcm_bytes);          off += pcm_bytes;
    memcpy(body + off, post, post_len);          off += post_len;

    resp_t resp = {0};
    esp_http_client_config_t config = {
#if CONFIG_STT_PROVIDER_OPENAI
        .url = CONFIG_STT_OPENAI_URL,
#else
        .url = STT_URL,
#endif
        .method = HTTP_METHOD_POST,
        .event_handler = http_event,
        .user_data = &resp,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 45000,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
#if CONFIG_STT_PROVIDER_OPENAI
    char *auth = malloc(strlen(api_key) + 8);
    if (auth != NULL) {
        snprintf(auth, strlen(api_key) + 8, "Bearer %s", api_key);
        esp_http_client_set_header(client, "Authorization", auth);
    }
#else
    esp_http_client_set_header(client, "xi-api-key", api_key);
#endif
    esp_http_client_set_header(client, "Content-Type",
                               "multipart/form-data; boundary=" STT_BOUNDARY);
    esp_http_client_set_post_field(client, (const char *)body, body_len);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(body);
#if CONFIG_STT_PROVIDER_OPENAI
    free(auth);
#endif

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "STT request failed: %s", esp_err_to_name(err));
        free(resp.buf);
        return NULL;
    }
    if (status != 200) {
        ESP_LOGW(TAG, "STT HTTP %d: %s", status, resp.buf ? resp.buf : "(no body)");
        free(resp.buf);
        return NULL;
    }

    char *text = NULL;
    cJSON *root = cJSON_Parse(resp.buf);
    if (root) {
        cJSON *t = cJSON_GetObjectItem(root, "text");
        if (cJSON_IsString(t) && t->valuestring && t->valuestring[0]) {
            text = strdup(t->valuestring);
        }
        cJSON_Delete(root);
    }
    free(resp.buf);

    if (text) {
        ESP_LOGI(TAG, "transcript: %s", text);
    } else {
        ESP_LOGW(TAG, "no text in STT response");
    }
    return text;
}
