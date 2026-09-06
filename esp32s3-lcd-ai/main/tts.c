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
 * Wanda explains herself without needing a serial monitor. */
static char s_last_err[200];

const char *tts_last_error(void)
{
    return s_last_err;
}

/* Raw formats we can hand straight to the speaker, best quality first. Which
 * ones an account may request depends on its plan, so they are tried in turn
 * rather than assumed. */
typedef struct { const char *name; int rate; } tts_fmt_t;
static const tts_fmt_t FORMATS[] = {
    { "pcm_24000", 24000 },
    { "pcm_22050", 22050 },
    { "pcm_16000", 16000 },
};
#define NFORMATS ((int)(sizeof(FORMATS) / sizeof(FORMATS[0])))

/* A stock ElevenLabs voice, tried if the configured voice id is refused
 * (mistyped, or a library voice never added to the account). */
#define FALLBACK_VOICE "21m00Tcm4TlvDq8ikWAM"

/* The combination discovered to work, reused for the rest of the session so
 * only the first call pays for the search. -1 = not known yet. */
static int s_ok_voice = -1;
static int s_ok_fmt   = -1;

/* ElevenLabs streams raw 16-bit mono PCM. We collect the WHOLE clip into a
 * PSRAM buffer first, then play it in one smooth pass. Playing chunks straight
 * from the HTTP callback underruns the I2S DMA whenever the network stalls,
 * which is heard as stutter / crackle. */
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
     * what we want to report. Cap the error case; those bodies are tiny. */
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

/* One request for `voice` in `fmt`. Returns the HTTP status, or 0 when the
 * request never completed (DNS/TLS/timeout). `st` receives the body. */
static int tts_request(const char *voice, const char *fmt, const char *body,
                       tts_state_t *st)
{
    char url[256];
    snprintf(url, sizeof(url),
             "https://api.elevenlabs.io/v1/text-to-speech/%s?output_format=%s",
             voice, fmt);

    esp_err_t err = ESP_FAIL;
    int status = 0;

    /* Retry: DNS / TLS connect can fail transiently right after WiFi is up. */
    for (int attempt = 1; attempt <= 2; attempt++) {
        free(st->buf);
        *st = (tts_state_t){0};
        esp_http_client_config_t config = {
            .url = url,
            .method = HTTP_METHOD_POST,
            .event_handler = http_event,
            .user_data = st,
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
            return status;          /* a real answer: 200 or an HTTP error */
        }
        ESP_LOGW(TAG, "TTS attempt %d failed: %s; retrying...",
                 attempt, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return 0;
}

/* Record why a request was refused, preferring the API's own explanation
 * (detail.message) over a bare status code. Returns true when the cause is
 * authentication, which every other voice/format combination hits alike -
 * ElevenLabs reports a bad key as 400, not 401, so the status alone can't
 * distinguish "wrong key" from "wrong voice". */
static bool note_error(int status, const tts_state_t *st)
{
    if (status == 0) {
        snprintf(s_last_err, sizeof(s_last_err), "Suara: jaringan gagal");
        return false;
    }

    bool auth = (status == 401 || status == 403);
    char detail[140] = "";
    if (st->buf != NULL && st->len > 0) {
        size_t n = (st->len < 511) ? st->len : 511;
        char *raw = malloc(n + 1);
        if (raw != NULL) {
            memcpy(raw, st->buf, n);
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
                    const char *type = cJSON_GetStringValue(
                        cJSON_GetObjectItem(d, "type"));
                    if (type != NULL && strcmp(type, "authentication_error") == 0) {
                        auth = true;
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

    if (auth) {
        snprintf(s_last_err, sizeof(s_last_err), "API key salah: %s", detail);
    } else {
        snprintf(s_last_err, sizeof(s_last_err), "Suara (%d): %s", status, detail);
    }
    return auth;
}

/* Play `st` as the clip for FORMATS[fi] and remember the winning combination. */
static void play_clip(const tts_state_t *st, int vi, int fi, const char *voice)
{
    if (s_ok_voice != vi || s_ok_fmt != fi) {
        ESP_LOGI(TAG, "TTS works with voice '%s' format %s", voice, FORMATS[fi].name);
    }
    s_ok_voice = vi;
    s_ok_fmt   = fi;
    ESP_LOGI(TAG, "TTS %u PCM bytes (%.1f s); playing",
             (unsigned)st->len, st->len / 2.0f / FORMATS[fi].rate);
    audio_set_sample_rate(FORMATS[fi].rate);
    audio_play_mono16(st->buf, st->len & ~(size_t)1);
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

    const char *voices[2];
    int nvoices = 0;
    voices[nvoices++] = CONFIG_ELEVENLABS_VOICE_ID;
    if (strcmp(CONFIG_ELEVENLABS_VOICE_ID, FALLBACK_VOICE) != 0) {
        voices[nvoices++] = FALLBACK_VOICE;
    }

    tts_state_t st = {0};
    bool spoke = false;

    /* Fast path: reuse the combination that worked last time. */
    if (s_ok_voice >= 0 && s_ok_voice < nvoices && s_ok_fmt >= 0) {
        int status = tts_request(voices[s_ok_voice], FORMATS[s_ok_fmt].name,
                                 body, &st);
        if (status == 200 && st.len >= 2) {
            play_clip(&st, s_ok_voice, s_ok_fmt, voices[s_ok_voice]);
            spoke = true;
        } else {
            note_error(status, &st);
            s_ok_voice = s_ok_fmt = -1;      /* re-discover below */
        }
    }

    /* Search: try each voice in each format until the account accepts one. */
    for (int vi = 0; !spoke && vi < nvoices; vi++) {
        for (int fi = 0; !spoke && fi < NFORMATS; fi++) {
            int status = tts_request(voices[vi], FORMATS[fi].name, body, &st);
            if (status == 200 && st.len >= 2) {
                play_clip(&st, vi, fi, voices[vi]);
                spoke = true;
                break;
            }
            bool auth = note_error(status, &st);
            ESP_LOGW(TAG, "voice '%s' fmt %s -> %d: %s",
                     voices[vi], FORMATS[fi].name, status, s_last_err);
            /* A rejected key fails identically for every combination, and a
             * dead network won't heal within one search - stop early. */
            if (auth || status == 0) {
                vi = nvoices;
                break;
            }
        }
    }

    free(body);
    free(st.buf);
    if (spoke) {
        s_last_err[0] = '\0';
    }
    return spoke;
}
