#include "claude_client.h"
#include "tools.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "claude";

#define ANTHROPIC_URL "https://api.anthropic.com/v1/messages"

/* Voice assistant persona, in the system prompt. */
static const char *SYSTEM_PROMPT =
    "Kamu adalah asisten suara di sebuah perangkat kecil dengan layar bulat. "
    "Jawab dalam Bahasa Indonesia yang ramah dan ringkas, maksimal 2-3 kalimat, "
    "tanpa format markdown, tanda bintang, atau emoji, karena jawabanmu akan "
    "dibacakan dengan suara. Untuk data terkini seperti cuaca atau harga kripto, "
    "WAJIB gunakan alat yang tersedia, jangan menebak. "
    "Kamu juga bisa mengendalikan smart home lewat Home Assistant: bila pengguna "
    "minta menyalakan/mematikan/mengatur perangkat (lampu, AC, kipas, saklar) atau "
    "menanyakan status sensor, panggil ha_list_entities dulu bila belum tahu "
    "entity_id-nya, lalu ha_call_service untuk aksi atau ha_get_state untuk membaca.";

/* Tool definitions sent to Claude on every request. */
static const char *TOOLS_JSON =
"[{"
  "\"name\":\"get_weather\","
  "\"description\":\"Cuaca terkini untuk sebuah kota atau lokasi. Gunakan saat pengguna bertanya soal cuaca, suhu, hujan, atau kelembapan.\","
  "\"input_schema\":{\"type\":\"object\",\"properties\":{"
    "\"location\":{\"type\":\"string\",\"description\":\"Nama kota, contoh: Bogor, Jakarta\"}},"
    "\"required\":[\"location\"]}"
"},{"
  "\"name\":\"get_crypto_price\","
  "\"description\":\"Harga mata uang kripto terkini dalam USD dan IDR. Gunakan saat pengguna bertanya harga koin seperti Bitcoin, Ethereum, dll.\","
  "\"input_schema\":{\"type\":\"object\",\"properties\":{"
    "\"coin\":{\"type\":\"string\",\"description\":\"ID CoinGecko, contoh: bitcoin, ethereum, solana\"}},"
    "\"required\":[\"coin\"]}"
"},{"
  "\"name\":\"ha_list_entities\","
  "\"description\":\"Daftar perangkat Home Assistant (entity_id, nama, status). Panggil ini saat belum tahu entity_id perangkat yang dimaksud pengguna.\","
  "\"input_schema\":{\"type\":\"object\",\"properties\":{}}"
"},{"
  "\"name\":\"ha_call_service\","
  "\"description\":\"Jalankan layanan Home Assistant untuk mengendalikan perangkat. Contoh: nyalakan lampu -> domain=light, service=turn_on, entity_id=light.ruang_tamu. Atur kecerahan/suhu lewat data, mis. {\\\"brightness_pct\\\":30} atau {\\\"temperature\\\":24}.\","
  "\"input_schema\":{\"type\":\"object\",\"properties\":{"
    "\"domain\":{\"type\":\"string\",\"description\":\"Domain HA, mis. light, switch, climate, fan, cover\"},"
    "\"service\":{\"type\":\"string\",\"description\":\"Service, mis. turn_on, turn_off, set_temperature\"},"
    "\"entity_id\":{\"type\":\"string\",\"description\":\"Entity yang dikendalikan, mis. light.ruang_tamu\"},"
    "\"data\":{\"type\":\"object\",\"description\":\"Parameter tambahan opsional, mis. brightness_pct, temperature, hs_color\"}},"
    "\"required\":[\"domain\",\"service\"]}"
"},{"
  "\"name\":\"ha_get_state\","
  "\"description\":\"Baca status satu perangkat/sensor Home Assistant.\","
  "\"input_schema\":{\"type\":\"object\",\"properties\":{"
    "\"entity_id\":{\"type\":\"string\",\"description\":\"Entity yang dibaca, mis. sensor.suhu_kamar\"}},"
    "\"required\":[\"entity_id\"]}"
"}]";

/* ---- HTTP plumbing ---- */

typedef struct { char *buf; int len; } response_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        response_t *r = (response_t *)evt->user_data;
        char *grown = realloc(r->buf, r->len + evt->data_len + 1);
        if (grown == NULL) {
            ESP_LOGE(TAG, "out of memory while reading response");
            return ESP_FAIL;
        }
        r->buf = grown;
        memcpy(r->buf + r->len, evt->data, evt->data_len);
        r->len += evt->data_len;
        r->buf[r->len] = '\0';
    }
    return ESP_OK;
}

/* POST a request body, return the response body as a heap string (caller frees). */
static char *do_request(const char *body)
{
    response_t resp = {0};
    esp_http_client_config_t config = {
        .url = ANTHROPIC_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 45000,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "content-type", "application/json");
    esp_http_client_set_header(client, "x-api-key", CONFIG_CLAUDE_API_KEY);
    esp_http_client_set_header(client, "anthropic-version", "2023-06-01");
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        free(resp.buf);
        return NULL;
    }
    if (status != 200) {
        ESP_LOGW(TAG, "API returned HTTP %d", status);
    }
    return resp.buf;
}

/* Concatenate every text block in a content array into one heap string. */
static char *concat_text(const cJSON *content)
{
    size_t total = 1;
    const cJSON *block;
    cJSON_ArrayForEach(block, content) {
        cJSON *type = cJSON_GetObjectItem(block, "type");
        cJSON *text = cJSON_GetObjectItem(block, "text");
        if (cJSON_IsString(type) && strcmp(type->valuestring, "text") == 0 &&
            cJSON_IsString(text)) {
            total += strlen(text->valuestring);
        }
    }
    char *out = malloc(total);
    if (out == NULL) return NULL;
    out[0] = '\0';
    cJSON_ArrayForEach(block, content) {
        cJSON *type = cJSON_GetObjectItem(block, "type");
        cJSON *text = cJSON_GetObjectItem(block, "text");
        if (cJSON_IsString(type) && strcmp(type->valuestring, "text") == 0 &&
            cJSON_IsString(text)) {
            strcat(out, text->valuestring);
        }
    }
    return out;
}

/* Build the request body for one round, embedding the running message list. */
static char *build_body(const cJSON *messages)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return NULL;
    cJSON_AddStringToObject(root, "model", CONFIG_CLAUDE_MODEL);
    cJSON_AddNumberToObject(root, "max_tokens", CONFIG_CLAUDE_MAX_TOKENS);
    cJSON_AddStringToObject(root, "system", SYSTEM_PROMPT);
    cJSON *tools = cJSON_Parse(TOOLS_JSON);
    if (tools) cJSON_AddItemToObject(root, "tools", tools);
    cJSON_AddItemToObject(root, "messages", cJSON_Duplicate(messages, true));
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return body;
}

/* Clean conversation memory: only finished user/assistant text turns (no tool
 * intermediates), so it stays valid and easy to trim. Kept across calls. */
#define MAX_HISTORY_MSGS 8     /* 4 exchanges; must stay even (user/assistant) */
static cJSON *s_history;

static void history_add(const char *role, const char *content)
{
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "role", role);
    cJSON_AddStringToObject(m, "content", content);
    cJSON_AddItemToArray(s_history, m);
}

static void history_trim(void)
{
    /* Drop whole exchanges from the front so it still starts with a user turn. */
    while (cJSON_GetArraySize(s_history) > MAX_HISTORY_MSGS) {
        cJSON_DeleteItemFromArray(s_history, 0);
        cJSON_DeleteItemFromArray(s_history, 0);
    }
}

char *claude_ask(const char *prompt)
{
    if (s_history == NULL) {
        s_history = cJSON_CreateArray();
    }

    /* Working list = remembered turns + this new user turn. */
    cJSON *messages = cJSON_Duplicate(s_history, true);
    cJSON *um = cJSON_CreateObject();
    cJSON_AddStringToObject(um, "role", "user");
    cJSON_AddStringToObject(um, "content", prompt);
    cJSON_AddItemToArray(messages, um);

    char *result = NULL;

    /* Tool-use loop: Claude may ask to call a tool; run it, feed the result
     * back, and ask again. Cap the iterations so we always terminate. */
    for (int iter = 0; iter < 6 && result == NULL; iter++) {
        char *body = build_body(messages);
        if (body == NULL) break;
        char *resp = do_request(body);
        free(body);
        if (resp == NULL) break;

        cJSON *rroot = cJSON_Parse(resp);
        free(resp);
        if (rroot == NULL) {
            ESP_LOGE(TAG, "failed to parse response JSON");
            break;
        }

        cJSON *content = cJSON_GetObjectItem(rroot, "content");
        if (!cJSON_IsArray(content)) {
            cJSON *error = cJSON_GetObjectItem(rroot, "error");
            cJSON *msg = error ? cJSON_GetObjectItem(error, "message") : NULL;
            if (cJSON_IsString(msg)) result = strdup(msg->valuestring);
            cJSON_Delete(rroot);
            break;
        }

        bool has_tool = false;
        cJSON *block;
        cJSON_ArrayForEach(block, content) {
            cJSON *type = cJSON_GetObjectItem(block, "type");
            if (cJSON_IsString(type) && strcmp(type->valuestring, "tool_use") == 0) {
                has_tool = true;
                break;
            }
        }

        if (!has_tool) {
            result = concat_text(content);
            if (result == NULL) result = strdup("");
            cJSON_Delete(rroot);
            break;
        }

        /* Echo the assistant's tool_use turn back into the conversation. */
        cJSON *am = cJSON_CreateObject();
        cJSON_AddStringToObject(am, "role", "assistant");
        cJSON_AddItemToObject(am, "content", cJSON_Duplicate(content, true));
        cJSON_AddItemToArray(messages, am);

        /* Run each requested tool and collect the results. */
        cJSON *results = cJSON_CreateArray();
        cJSON_ArrayForEach(block, content) {
            cJSON *type = cJSON_GetObjectItem(block, "type");
            if (!(cJSON_IsString(type) && strcmp(type->valuestring, "tool_use") == 0)) {
                continue;
            }
            const char *id   = cJSON_GetStringValue(cJSON_GetObjectItem(block, "id"));
            const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(block, "name"));
            cJSON *input = cJSON_GetObjectItem(block, "input");
            ESP_LOGI(TAG, "tool_use: %s", name ? name : "(null)");

            char *tres = tool_execute(name, input);
            cJSON *tr = cJSON_CreateObject();
            cJSON_AddStringToObject(tr, "type", "tool_result");
            cJSON_AddStringToObject(tr, "tool_use_id", id ? id : "");
            cJSON_AddStringToObject(tr, "content", tres ? tres : "error");
            cJSON_AddItemToArray(results, tr);
            free(tres);
        }

        cJSON *urm = cJSON_CreateObject();
        cJSON_AddStringToObject(urm, "role", "user");
        cJSON_AddItemToObject(urm, "content", results);
        cJSON_AddItemToArray(messages, urm);

        cJSON_Delete(rroot);
    }

    /* Commit only the clean turns to memory (the tool round-trips are dropped). */
    if (result != NULL) {
        history_add("user", prompt);
        history_add("assistant", result);
        history_trim();
    }

    cJSON_Delete(messages);
    return result;
}
