#include "tools.h"
#include "reminders.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <time.h>

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

static const char *TAG = "tools";

/* ---- tiny HTTP helper that returns the body as a heap string ---- */

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

static char *http_do(esp_http_client_method_t method, const char *url,
                     const char *bearer, const char *body, int *status_out)
{
    body_t b = {0};
    esp_http_client_config_t cfg = {
        .url = url,
        .method = method,
        .event_handler = on_data,
        .user_data = &b,
        .crt_bundle_attach = esp_crt_bundle_attach,   /* used only for https */
        .timeout_ms = 15000,
        .buffer_size = 4096,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_http_client_set_header(c, "User-Agent", "claw/1.0");

    char *authhdr = NULL;
    if (bearer && bearer[0]) {
        size_t L = strlen(bearer) + 16;
        authhdr = malloc(L);
        if (authhdr) {
            snprintf(authhdr, L, "Bearer %s", bearer);
            esp_http_client_set_header(c, "Authorization", authhdr);
        }
    }
    if (body) {
        esp_http_client_set_header(c, "Content-Type", "application/json");
        esp_http_client_set_post_field(c, body, strlen(body));
    }

    esp_err_t err = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    free(authhdr);
    if (status_out) *status_out = status;

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s -> err=%s status=%d", url, esp_err_to_name(err), status);
        free(b.buf);
        return NULL;
    }
    return b.buf;   /* may be NULL if the body was empty */
}

static char *http_get(const char *url)
{
    int status = 0;
    char *body = http_do(HTTP_METHOD_GET, url, NULL, NULL, &status);
    if (status != 200) { free(body); return NULL; }
    return body;
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
    const char *prefix = "https://wttr.in/";
    memcpy(url, prefix, strlen(prefix)); off = strlen(prefix);
    append_encoded(url, sizeof(url), &off, city);
    const char *fmt = "?format=%l:+%C,+suhu+%t,+kelembapan+%h,+angin+%w&m";
    snprintf(url + off, sizeof(url) - off, "%s", fmt);

    char *body = http_get(url);
    if (body == NULL) return strdup("Maaf, data cuaca tidak bisa diambil sekarang.");
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
    if (body == NULL) return strdup("Maaf, harga kripto tidak bisa diambil sekarang.");

    char *out = NULL;
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (root) {
        cJSON *obj = cJSON_GetObjectItem(root, coin);
        cJSON *usd = obj ? cJSON_GetObjectItem(obj, "usd") : NULL;
        cJSON *idr = obj ? cJSON_GetObjectItem(obj, "idr") : NULL;
        if (cJSON_IsNumber(usd)) {
            char tmp[160];
            if (cJSON_IsNumber(idr))
                snprintf(tmp, sizeof(tmp), "%s: USD %.2f, IDR %.0f",
                         coin, usd->valuedouble, idr->valuedouble);
            else
                snprintf(tmp, sizeof(tmp), "%s: USD %.2f", coin, usd->valuedouble);
            out = strdup(tmp);
        }
        cJSON_Delete(root);
    }
    if (out == NULL)
        out = strdup("Koin tidak ditemukan. Pakai nama CoinGecko seperti bitcoin, ethereum.");
    ESP_LOGI(TAG, "crypto: %s", out);
    return out;
}

/* ====================== Home Assistant ====================== */

static bool ha_ready(void)
{
    return strlen(CONFIG_HA_BASE_URL) > 0 && strlen(CONFIG_HA_TOKEN) > 0;
}

/* List entities using HA's template API, which renders a compact text list
 * server-side (no huge JSON to parse on the device). By default it lists only
 * *controllable* appliances; sensors are excluded so they don't crowd out and
 * truncate the device list. Pass domain="sensor" (or any domain) to list that
 * domain specifically. */
static char *tool_ha_list_entities(const cJSON *input)
{
    if (!ha_ready()) return strdup("Home Assistant belum dikonfigurasi di perangkat.");

    /* Optional domain filter, sanitised to a safe identifier so it can't
     * break the Jinja template. */
    const char *dom = cJSON_GetStringValue(cJSON_GetObjectItem(input, "domain"));
    char domain[24] = {0};
    if (dom) {
        size_t i = 0;
        for (const char *p = dom; *p && i < sizeof(domain) - 1; p++) {
            char ch = (char)tolower((unsigned char)*p);
            if ((ch >= 'a' && ch <= 'z') || ch == '_') domain[i++] = ch;
        }
        domain[i] = '\0';
    }

    char dyn[256];
    const char *tmpl;
    if (domain[0]) {
        snprintf(dyn, sizeof(dyn),
                 "{%% for s in states.%s %%}"
                 "{{ s.entity_id }} = {{ s.name }} = {{ s.state }}\n"
                 "{%% endfor %%}", domain);
        tmpl = dyn;
    } else {
        tmpl =
            "{% for s in states if s.domain in "
            "['light','switch','fan','climate','cover','lock','media_player',"
            "'vacuum','humidifier','water_heater','valve','siren','scene',"
            "'input_boolean'] %}"
            "{{ s.entity_id }} = {{ s.name }} = {{ s.state }}\n"
            "{% endfor %}";
    }

    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "template", tmpl);
    char *bodyreq = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);

    char url[256];
    snprintf(url, sizeof(url), "%s/api/template", CONFIG_HA_BASE_URL);
    int status = 0;
    char *resp = http_do(HTTP_METHOD_POST, url, CONFIG_HA_TOKEN, bodyreq, &status);
    free(bodyreq);

    if (resp == NULL || status != 200) {
        free(resp);
        return strdup("Gagal mengambil daftar device dari Home Assistant.");
    }
    /* Cap the size we hand back to Claude (token budget + device memory). */
    const size_t MAXLEN = 6000;
    if (strlen(resp) > MAXLEN) {
        strcpy(resp + MAXLEN - 40,
               "\n...(daftar dipotong, minta per domain)\n");
    }
    ESP_LOGI(TAG, "ha entities (%u bytes)", (unsigned)strlen(resp));
    return resp;
}

/* Call a HA service, e.g. domain=light service=turn_on entity_id=light.x. */
static char *tool_ha_call_service(const cJSON *input)
{
    if (!ha_ready()) return strdup("Home Assistant belum dikonfigurasi di perangkat.");

    const char *domain  = cJSON_GetStringValue(cJSON_GetObjectItem(input, "domain"));
    const char *service = cJSON_GetStringValue(cJSON_GetObjectItem(input, "service"));
    const char *entity  = cJSON_GetStringValue(cJSON_GetObjectItem(input, "entity_id"));
    const cJSON *data   = cJSON_GetObjectItem(input, "data");
    if (domain == NULL || service == NULL) {
        return strdup("Perlu domain dan service untuk menjalankan perintah.");
    }

    cJSON *body = (data && cJSON_IsObject(data)) ? cJSON_Duplicate(data, true)
                                                 : cJSON_CreateObject();
    if (entity) cJSON_AddStringToObject(body, "entity_id", entity);
    char *bstr = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    char url[256];
    snprintf(url, sizeof(url), "%s/api/services/%s/%s",
             CONFIG_HA_BASE_URL, domain, service);
    int status = 0;
    char *resp = http_do(HTTP_METHOD_POST, url, CONFIG_HA_TOKEN, bstr, &status);
    free(bstr);
    free(resp);

    char out[128];
    if (status == 200 || status == 201) {
        snprintf(out, sizeof(out), "Berhasil: %s.%s pada %s",
                 domain, service, entity ? entity : "(area)");
        ESP_LOGI(TAG, "%s", out);
    } else {
        snprintf(out, sizeof(out), "Gagal menjalankan perintah (HTTP %d).", status);
        ESP_LOGW(TAG, "%s", out);
    }
    return strdup(out);
}

/* Read one entity's state (for sensors etc.). */
static char *tool_ha_get_state(const cJSON *input)
{
    if (!ha_ready()) return strdup("Home Assistant belum dikonfigurasi di perangkat.");
    const char *entity = cJSON_GetStringValue(cJSON_GetObjectItem(input, "entity_id"));
    if (entity == NULL) return strdup("Perlu entity_id untuk membaca status.");

    char url[256];
    snprintf(url, sizeof(url), "%s/api/states/%s", CONFIG_HA_BASE_URL, entity);
    int status = 0;
    char *resp = http_do(HTTP_METHOD_GET, url, CONFIG_HA_TOKEN, NULL, &status);
    if (resp == NULL || status != 200) {
        free(resp);
        return strdup("Status device tidak ditemukan.");
    }

    char *out = NULL;
    cJSON *root = cJSON_Parse(resp);
    free(resp);
    if (root) {
        const char *state = cJSON_GetStringValue(cJSON_GetObjectItem(root, "state"));
        cJSON *attr = cJSON_GetObjectItem(root, "attributes");
        const char *name = attr ? cJSON_GetStringValue(cJSON_GetObjectItem(attr, "friendly_name")) : NULL;
        const char *unit = attr ? cJSON_GetStringValue(cJSON_GetObjectItem(attr, "unit_of_measurement")) : NULL;
        char tmp[200];
        snprintf(tmp, sizeof(tmp), "%s: %s%s%s",
                 name ? name : entity,
                 state ? state : "?",
                 unit ? " " : "", unit ? unit : "");
        out = strdup(tmp);
        cJSON_Delete(root);
    }
    if (out == NULL) out = strdup("Status device tidak terbaca.");
    ESP_LOGI(TAG, "ha state: %s", out);
    return out;
}

/* ====================== Local reminders / alarms ====================== */

/* set_reminder: message + either in_minutes (relative) or at_time "HH:MM". */
static char *tool_set_reminder(const cJSON *input)
{
    const char *msg = cJSON_GetStringValue(cJSON_GetObjectItem(input, "message"));
    if (msg == NULL || msg[0] == '\0') {
        return strdup("Perlu pesan pengingatnya.");
    }
    const cJSON *inmin = cJSON_GetObjectItem(input, "in_minutes");
    const char  *at    = cJSON_GetStringValue(cJSON_GetObjectItem(input, "at_time"));
    const cJSON *rep   = cJSON_GetObjectItem(input, "repeat_daily");
    bool daily = cJSON_IsTrue(rep);

    time_t now = time(NULL);
    if (now < 1700000000) {   /* clock not synced yet */
        return strdup("Jam perangkat belum sinkron, coba lagi sebentar.");
    }
    time_t when = 0;
    if (cJSON_IsNumber(inmin) && inmin->valuedouble > 0) {
        when = now + (time_t)(inmin->valuedouble * 60);
    } else if (at != NULL) {
        int hh = -1, mm = -1;
        if (sscanf(at, "%d:%d", &hh, &mm) != 2 || hh < 0 || hh > 23 || mm < 0 || mm > 59) {
            return strdup("Format waktu harus HH:MM, mis. 20:00.");
        }
        struct tm tm;
        localtime_r(&now, &tm);
        tm.tm_hour = hh; tm.tm_min = mm; tm.tm_sec = 0;
        when = mktime(&tm);
        if (when <= now) when += 24 * 3600;   /* already passed -> tomorrow */
    } else {
        return strdup("Sebutkan waktunya, mis. 10 menit lagi atau jam 20:00.");
    }

    if (!reminders_add(when, msg, daily)) {
        return strdup("Maaf, daftar pengingat sudah penuh.");
    }
    struct tm tm;
    localtime_r(&when, &tm);
    char out[200];
    snprintf(out, sizeof(out), "Oke, pengingat diset jam %02d:%02d%s: %s",
             tm.tm_hour, tm.tm_min, daily ? " setiap hari" : "", msg);
    return strdup(out);
}

static char *tool_list_reminders(const cJSON *input)
{
    (void)input;
    char *buf = malloc(512);
    if (buf == NULL) return strdup("Memori penuh.");
    reminders_list(buf, 512);
    return buf;
}

static char *tool_cancel_reminders(const cJSON *input)
{
    (void)input;
    int n = reminders_clear();
    char out[64];
    snprintf(out, sizeof(out), "%d pengingat dihapus.", n);
    return strdup(out);
}

/* ha_schedule: create a daily time-triggered Home Assistant automation. */
static char *tool_ha_schedule(const cJSON *input)
{
    if (!ha_ready()) return strdup("Home Assistant belum dikonfigurasi.");

    const char *at      = cJSON_GetStringValue(cJSON_GetObjectItem(input, "at_time"));
    const char *domain  = cJSON_GetStringValue(cJSON_GetObjectItem(input, "domain"));
    const char *service = cJSON_GetStringValue(cJSON_GetObjectItem(input, "service"));
    const char *entity  = cJSON_GetStringValue(cJSON_GetObjectItem(input, "entity_id"));
    const cJSON *data   = cJSON_GetObjectItem(input, "data");
    const char *desc    = cJSON_GetStringValue(cJSON_GetObjectItem(input, "description"));
    if (at == NULL || domain == NULL || service == NULL) {
        return strdup("Perlu at_time (HH:MM), domain, dan service.");
    }
    int hh = -1, mm = -1;
    if (sscanf(at, "%d:%d", &hh, &mm) != 2 || hh < 0 || hh > 23 || mm < 0 || mm > 59) {
        return strdup("Format waktu harus HH:MM.");
    }

    cJSON *root = cJSON_CreateObject();
    char alias[96];
    snprintf(alias, sizeof(alias), "Wanda: %s", desc ? desc : service);
    cJSON_AddStringToObject(root, "alias", alias);

    cJSON *trig = cJSON_CreateArray();
    cJSON *t = cJSON_CreateObject();
    cJSON_AddStringToObject(t, "platform", "time");
    char attime[16];
    snprintf(attime, sizeof(attime), "%02d:%02d:00", hh, mm);
    cJSON_AddStringToObject(t, "at", attime);
    cJSON_AddItemToArray(trig, t);
    cJSON_AddItemToObject(root, "trigger", trig);

    cJSON *act = cJSON_CreateArray();
    cJSON *a = cJSON_CreateObject();
    char svc[64];
    snprintf(svc, sizeof(svc), "%s.%s", domain, service);
    cJSON_AddStringToObject(a, "service", svc);
    if (entity != NULL) {
        cJSON *tgt = cJSON_CreateObject();
        cJSON_AddStringToObject(tgt, "entity_id", entity);
        cJSON_AddItemToObject(a, "target", tgt);
    }
    if (data != NULL && cJSON_IsObject(data)) {
        cJSON_AddItemToObject(a, "data", cJSON_Duplicate(data, true));
    }
    cJSON_AddItemToArray(act, a);
    cJSON_AddItemToObject(root, "action", act);
    cJSON_AddStringToObject(root, "mode", "single");

    char *bstr = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    char url[256];
    snprintf(url, sizeof(url), "%s/api/config/automation/config/wanda_%ld",
             CONFIG_HA_BASE_URL, (long)time(NULL));
    int status = 0;
    char *resp = http_do(HTTP_METHOD_POST, url, CONFIG_HA_TOKEN, bstr, &status);
    free(bstr);
    free(resp);

    char out[180];
    if (status == 200 || status == 201) {
        snprintf(out, sizeof(out), "Jadwal HA dibuat: %s.%s jam %02d:%02d setiap hari.",
                 domain, service, hh, mm);
    } else {
        snprintf(out, sizeof(out),
                 "Gagal membuat jadwal HA (HTTP %d). Pastikan editor automation aktif.",
                 status);
    }
    return strdup(out);
}

char *tool_execute(const char *name, const cJSON *input)
{
    if (name == NULL) return NULL;
    if (strcmp(name, "get_weather") == 0)        return tool_get_weather(input);
    if (strcmp(name, "get_crypto_price") == 0)   return tool_get_crypto_price(input);
    if (strcmp(name, "ha_list_entities") == 0)   return tool_ha_list_entities(input);
    if (strcmp(name, "ha_call_service") == 0)    return tool_ha_call_service(input);
    if (strcmp(name, "ha_get_state") == 0)       return tool_ha_get_state(input);
    if (strcmp(name, "set_reminder") == 0)       return tool_set_reminder(input);
    if (strcmp(name, "list_reminders") == 0)     return tool_list_reminders(input);
    if (strcmp(name, "cancel_reminders") == 0)   return tool_cancel_reminders(input);
    if (strcmp(name, "ha_schedule") == 0)        return tool_ha_schedule(input);
    ESP_LOGW(TAG, "unknown tool: %s", name);
    return strdup("Alat itu tidak tersedia.");
}
