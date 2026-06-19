#include "tools.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

static const char *TAG = "tools";

/* ---- tiny HTTP GET that returns the body as a heap string ---- */

typedef struct { char *buf; size_t len, cap; } body_t;

static esp_err_t on_data(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    body_t *b = (body_t *)evt->user_data;
    if (b->len + evt->data_len + 1 > b->cap) {
        size_t ncap = b->cap ? b->cap : 1024;
        while (ncap < b->len + evt->data_len + 1) ncap *= 2;
        char *nb = realloc(b->buf, ncap);
        if (!nb) return ESP_FAIL;
        b->buf = nb; b->cap = ncap;
    }
    memcpy(b->buf + b->len, evt->data, evt->data_len);
    b->len += evt->data_len;
    b->buf[b->len] = '\0';
    return ESP_OK;
}

static char *http_get(const char *url)
{
    body_t b = {0};
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = on_data,
        .user_data = &b,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_http_client_set_header(c, "User-Agent", "curl/8.0");   /* wttr.in wants this */
    esp_err_t err = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "GET %s -> err=%s status=%d", url, esp_err_to_name(err), status);
        free(b.buf);
        return NULL;
    }
    return b.buf;   /* may be NULL if empty */
}

/* Append `s` into `dst` (size cap) starting at *off, URL-encoding spaces. */
static void append_encoded(char *dst, size_t cap, size_t *off, const char *s)
{
    for (; *s && *off + 3 < cap; s++) {
        if (*s == ' ') { dst[(*off)++] = '%'; dst[(*off)++] = '2'; dst[(*off)++] = '0'; }
        else            { dst[(*off)++] = *s; }
    }
    dst[*off] = '\0';
}

/* ---- weather via wttr.in (free, no key) ---- */
static char *tool_get_weather(const cJSON *input)
{
    const cJSON *loc = cJSON_GetObjectItem(input, "location");
    const char *city = (cJSON_IsString(loc) && loc->valuestring[0]) ? loc->valuestring : "";

    char url[256];
    size_t off = 0;
    /* Plain-text one-liner: location, condition, temp, feels-like, humidity, wind. */
    const char *prefix = "https://wttr.in/";
    memcpy(url, prefix, strlen(prefix)); off = strlen(prefix);
    append_encoded(url, sizeof(url), &off, city);
    const char *fmt = "?format=%l:+%C,+suhu+%t,+kelembapan+%h,+angin+%w&m";
    snprintf(url + off, sizeof(url) - off, "%s", fmt);

    char *body = http_get(url);
    if (body == NULL) {
        return strdup("Maaf, data cuaca tidak bisa diambil sekarang.");
    }
    /* strip trailing newline */
    size_t n = strlen(body);
    while (n && (body[n-1] == '\n' || body[n-1] == '\r')) body[--n] = '\0';
    ESP_LOGI(TAG, "weather: %s", body);
    return body;
}

/* ---- crypto price via CoinGecko (free, no key) ---- */
static char *tool_get_crypto_price(const cJSON *input)
{
    const cJSON *c = cJSON_GetObjectItem(input, "coin");
    char coin[48] = "bitcoin";
    if (cJSON_IsString(c) && c->valuestring[0]) {
        size_t i = 0;
        for (const char *p = c->valuestring; *p && i < sizeof(coin) - 1; p++) {
            coin[i++] = (char)tolower((unsigned char)*p);
        }
        coin[i] = '\0';
    }

    char url[256];
    snprintf(url, sizeof(url),
             "https://api.coingecko.com/api/v3/simple/price?ids=%s&vs_currencies=usd,idr",
             coin);
    char *body = http_get(url);
    if (body == NULL) {
        return strdup("Maaf, harga kripto tidak bisa diambil sekarang.");
    }

    char *out = NULL;
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (root) {
        cJSON *obj = cJSON_GetObjectItem(root, coin);
        cJSON *usd = obj ? cJSON_GetObjectItem(obj, "usd") : NULL;
        cJSON *idr = obj ? cJSON_GetObjectItem(obj, "idr") : NULL;
        if (cJSON_IsNumber(usd)) {
            char tmp[160];
            if (cJSON_IsNumber(idr)) {
                snprintf(tmp, sizeof(tmp), "%s: USD %.2f, IDR %.0f",
                         coin, usd->valuedouble, idr->valuedouble);
            } else {
                snprintf(tmp, sizeof(tmp), "%s: USD %.2f", coin, usd->valuedouble);
            }
            out = strdup(tmp);
        }
        cJSON_Delete(root);
    }
    if (out == NULL) {
        out = strdup("Maaf, koin itu tidak ditemukan. Pakai nama CoinGecko seperti bitcoin, ethereum.");
    }
    ESP_LOGI(TAG, "crypto: %s", out);
    return out;
}

char *tool_execute(const char *name, const cJSON *input)
{
    if (name == NULL) return NULL;
    if (strcmp(name, "get_weather") == 0)       return tool_get_weather(input);
    if (strcmp(name, "get_crypto_price") == 0)  return tool_get_crypto_price(input);
    ESP_LOGW(TAG, "unknown tool: %s", name);
    return strdup("Alat itu tidak tersedia.");
}
