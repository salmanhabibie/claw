#include "claude_client.h"
#include "tools.h"
#include "memory.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "claude";

#define ANTHROPIC_URL "https://api.anthropic.com/v1/messages"

/* Voice assistant persona, in the system prompt. */
static const char *SYSTEM_PROMPT =
    "Namamu adalah Wanda, asisten suara di sebuah perangkat kecil dengan layar "
    "bulat. Bila pengguna menanyakan namamu, jawab bahwa kamu Wanda. "
    "Jawab dalam Bahasa Indonesia yang ramah dan ringkas, maksimal 2-3 kalimat, "
    "tanpa format markdown, tanda bintang, atau emoji, karena jawabanmu akan "
    "dibacakan dengan suara. Untuk data terkini seperti cuaca atau harga kripto, "
    "WAJIB gunakan alat yang tersedia, jangan menebak. "
    "Kamu juga bisa mengendalikan smart home lewat Home Assistant: bila pengguna "
    "minta menyalakan/mematikan/mengatur perangkat (lampu, AC, kipas, saklar) atau "
    "menanyakan status sensor, panggil ha_list_entities dulu bila belum tahu "
    "entity_id-nya; untuk sensor panggil ha_list_entities dengan domain=sensor. "
    "Lalu ha_call_service untuk aksi atau ha_get_state untuk membaca. "
    "Kamu juga bisa membuat pengingat/alarm lokal dengan set_reminder (timer "
    "pakai in_minutes, alarm jam tertentu pakai at_time), serta menjadwalkan "
    "otomasi perangkat di Home Assistant dengan ha_schedule. "
    "Kamu punya ingatan jangka panjang: simpan nama pengguna dengan set_user_name, "
    "simpan preferensi/catatan/daftar (mis. daftar belanja) dengan remember, dan "
    "hapus dengan forget. Manfaatkan ingatan itu untuk menjawab tanpa bertanya ulang. "
    "Bila diminta 'kabar hari ini' atau briefing, sapa sesuai waktu, sebutkan hari "
    "dan tanggal serta jam, lalu cuaca kota pengguna (pakai get_weather; kalau "
    "kotanya belum diketahui, tanyakan atau ingat lewat remember) dan pengingat "
    "hari ini (list_reminders), ringkas saja.";

/* Compose the live system prompt = persona + current date/time + memory. The
 * caller frees it. Returns a strdup of SYSTEM_PROMPT alone on OOM. */
static char *compose_system(void)
{
    char extra[900];
    size_t off = 0;
    extra[0] = '\0';

    time_t now = time(NULL);
    if (now > 1700000000) {                 /* clock is NTP-synced */
        struct tm tm;
        localtime_r(&now, &tm);
        static const char *days[] = {
            "Minggu", "Senin", "Selasa", "Rabu", "Kamis", "Jumat", "Sabtu" };
        static const char *mons[] = {
            "Januari", "Februari", "Maret", "April", "Mei", "Juni", "Juli",
            "Agustus", "September", "Oktober", "November", "Desember" };
        off += snprintf(extra + off, sizeof(extra) - off,
                        "\n\nWaktu sekarang: %s, %d %s %d, pukul %02d:%02d WIB.",
                        days[tm.tm_wday], tm.tm_mday, mons[tm.tm_mon],
                        tm.tm_year + 1900, tm.tm_hour, tm.tm_min);
    }

    char mem[600];
    memory_get_prompt(mem, sizeof(mem));
    if (mem[0] && off < sizeof(extra)) {
        off += snprintf(extra + off, sizeof(extra) - off, "\n\n%s", mem);
    }

    char *out = malloc(strlen(SYSTEM_PROMPT) + strlen(extra) + 1);
    if (out == NULL) return strdup(SYSTEM_PROMPT);
    strcpy(out, SYSTEM_PROMPT);
    strcat(out, extra);
    return out;
}

/* Tool definitions, in Anthropic shape (name/description/input_schema). The
 * OpenAI path converts these to its function-tool shape at request time. */
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
  "\"description\":\"Daftar perangkat Home Assistant yang bisa dikontrol (lampu, saklar, AC, kipas, tirai, dll) beserta entity_id, nama, dan status. Panggil saat belum tahu entity_id. Default TIDAK menampilkan sensor agar daftar peralatan tidak terpotong; untuk sensor atau domain lain, isi parameter domain, mis. domain=sensor.\","
  "\"input_schema\":{\"type\":\"object\",\"properties\":{"
    "\"domain\":{\"type\":\"string\",\"description\":\"Opsional. Batasi ke satu domain saja, mis. sensor, binary_sensor, light, switch. Kosongkan untuk semua peralatan yang bisa dikontrol.\"}}}"
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
"},{"
  "\"name\":\"set_reminder\","
  "\"description\":\"Buat pengingat/alarm lokal yang akan dibunyikan dan diucapkan perangkat. Isi in_minutes UNTUK relatif (mis. 10 menit lagi) ATAU at_time untuk jam tertentu (mis. 20:00). Set repeat_daily true untuk alarm harian.\","
  "\"input_schema\":{\"type\":\"object\",\"properties\":{"
    "\"message\":{\"type\":\"string\",\"description\":\"Isi pengingat, mis. minum obat\"},"
    "\"in_minutes\":{\"type\":\"number\",\"description\":\"Berapa menit dari sekarang (untuk timer)\"},"
    "\"at_time\":{\"type\":\"string\",\"description\":\"Jam tertentu format HH:MM 24 jam, mis. 20:00\"},"
    "\"repeat_daily\":{\"type\":\"boolean\",\"description\":\"true bila diulang setiap hari\"}},"
    "\"required\":[\"message\"]}"
"},{"
  "\"name\":\"list_reminders\","
  "\"description\":\"Tampilkan daftar pengingat/alarm lokal yang aktif.\","
  "\"input_schema\":{\"type\":\"object\",\"properties\":{}}"
"},{"
  "\"name\":\"cancel_reminders\","
  "\"description\":\"Hapus semua pengingat/alarm lokal.\","
  "\"input_schema\":{\"type\":\"object\",\"properties\":{}}"
"},{"
  "\"name\":\"ha_schedule\","
  "\"description\":\"Jadwalkan otomasi Home Assistant harian pada jam tertentu, mis. nyalakan lampu teras tiap 18:00. Membuat automation bertrigger waktu. Cari entity_id lewat ha_list_entities bila perlu.\","
  "\"input_schema\":{\"type\":\"object\",\"properties\":{"
    "\"at_time\":{\"type\":\"string\",\"description\":\"Jam pemicu HH:MM 24 jam, mis. 18:00\"},"
    "\"domain\":{\"type\":\"string\",\"description\":\"Domain HA, mis. light, switch, climate\"},"
    "\"service\":{\"type\":\"string\",\"description\":\"Service, mis. turn_on, turn_off\"},"
    "\"entity_id\":{\"type\":\"string\",\"description\":\"Entity target, mis. light.teras\"},"
    "\"data\":{\"type\":\"object\",\"description\":\"Parameter tambahan opsional, mis. brightness_pct\"},"
    "\"description\":{\"type\":\"string\",\"description\":\"Nama singkat jadwal\"}},"
    "\"required\":[\"at_time\",\"domain\",\"service\"]}"
"},{"
  "\"name\":\"set_user_name\","
  "\"description\":\"Simpan nama panggilan pengguna agar Wanda mengingatnya dan menyapa dengan namanya.\","
  "\"input_schema\":{\"type\":\"object\",\"properties\":{"
    "\"name\":{\"type\":\"string\",\"description\":\"Nama panggilan pengguna\"}},"
    "\"required\":[\"name\"]}"
"},{"
  "\"name\":\"remember\","
  "\"description\":\"Ingat satu catatan, preferensi, atau item daftar untuk jangka panjang (mis. 'alergi udang', 'kota: Bogor', 'belanja: telur'). Satu pemanggilan untuk satu hal.\","
  "\"input_schema\":{\"type\":\"object\",\"properties\":{"
    "\"note\":{\"type\":\"string\",\"description\":\"Hal yang diingat, kalimat singkat\"}},"
    "\"required\":[\"note\"]}"
"},{"
  "\"name\":\"forget\","
  "\"description\":\"Hapus catatan yang diingat. Isi note untuk menghapus yang mengandung kata itu (mis. 'telur'), atau kosongkan untuk menghapus semua catatan.\","
  "\"input_schema\":{\"type\":\"object\",\"properties\":{"
    "\"note\":{\"type\":\"string\",\"description\":\"Kata kunci catatan yang dihapus; kosong = hapus semua\"}}}"
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

/* POST a request body, return the response body as a heap string (caller frees).
 * URL and auth headers depend on the selected LLM provider. */
static char *do_request(const char *body)
{
    response_t resp = {0};
    esp_http_client_config_t config = {
#if CONFIG_LLM_PROVIDER_OPENAI
        .url = CONFIG_LLM_OPENAI_URL,
#else
        .url = ANTHROPIC_URL,
#endif
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
#if CONFIG_LLM_PROVIDER_OPENAI
    char *auth = malloc(strlen(CONFIG_OPENAI_API_KEY) + 8);
    if (auth != NULL) {
        snprintf(auth, strlen(CONFIG_OPENAI_API_KEY) + 8, "Bearer %s",
                 CONFIG_OPENAI_API_KEY);
        esp_http_client_set_header(client, "Authorization", auth);
    }
#else
    esp_http_client_set_header(client, "x-api-key", CONFIG_CLAUDE_API_KEY);
    esp_http_client_set_header(client, "anthropic-version", "2023-06-01");
#endif
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
#if CONFIG_LLM_PROVIDER_OPENAI
    free(auth);
#endif

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

/* ---- conversation memory (shared; {role,content} works for both APIs) ---- */

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
    while (cJSON_GetArraySize(s_history) > MAX_HISTORY_MSGS) {
        cJSON_DeleteItemFromArray(s_history, 0);
        cJSON_DeleteItemFromArray(s_history, 0);
    }
}

#if CONFIG_LLM_PROVIDER_OPENAI
/* ============================ OpenAI-compatible ============================ */

/* Convert the Anthropic-shape TOOLS_JSON into OpenAI function tools. */
static cJSON *build_openai_tools(void)
{
    cJSON *src = cJSON_Parse(TOOLS_JSON);
    if (src == NULL) return NULL;
    cJSON *out = cJSON_CreateArray();
    cJSON *t;
    cJSON_ArrayForEach(t, src) {
        cJSON *name   = cJSON_GetObjectItem(t, "name");
        cJSON *desc   = cJSON_GetObjectItem(t, "description");
        cJSON *schema = cJSON_GetObjectItem(t, "input_schema");
        if (!cJSON_IsString(name)) continue;
        cJSON *fn = cJSON_CreateObject();
        cJSON_AddStringToObject(fn, "name", name->valuestring);
        if (cJSON_IsString(desc))
            cJSON_AddStringToObject(fn, "description", desc->valuestring);
        if (schema)
            cJSON_AddItemToObject(fn, "parameters", cJSON_Duplicate(schema, true));
        cJSON *wrap = cJSON_CreateObject();
        cJSON_AddStringToObject(wrap, "type", "function");
        cJSON_AddItemToObject(wrap, "function", fn);
        cJSON_AddItemToArray(out, wrap);
    }
    cJSON_Delete(src);
    return out;
}

static char *build_body(const cJSON *messages)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return NULL;
    cJSON_AddStringToObject(root, "model", CONFIG_LLM_OPENAI_MODEL);
    cJSON_AddNumberToObject(root, "max_tokens", CONFIG_CLAUDE_MAX_TOKENS);
    cJSON_AddItemToObject(root, "messages", cJSON_Duplicate(messages, true));
    cJSON *tools = build_openai_tools();
    if (tools) cJSON_AddItemToObject(root, "tools", tools);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return body;
}

char *claude_ask(const char *prompt)
{
    if (s_history == NULL) s_history = cJSON_CreateArray();

    /* messages = system + remembered turns + new user turn. */
    cJSON *messages = cJSON_CreateArray();
    cJSON *sys = cJSON_CreateObject();
    cJSON_AddStringToObject(sys, "role", "system");
    char *sysprompt = compose_system();
    cJSON_AddStringToObject(sys, "content", sysprompt ? sysprompt : "");
    free(sysprompt);
    cJSON_AddItemToArray(messages, sys);
    cJSON *h;
    cJSON_ArrayForEach(h, s_history) {
        cJSON_AddItemToArray(messages, cJSON_Duplicate(h, true));
    }
    cJSON *um = cJSON_CreateObject();
    cJSON_AddStringToObject(um, "role", "user");
    cJSON_AddStringToObject(um, "content", prompt);
    cJSON_AddItemToArray(messages, um);

    char *result = NULL;

    for (int iter = 0; iter < 6 && result == NULL; iter++) {
        char *body = build_body(messages);
        if (body == NULL) break;
        char *resp = do_request(body);
        free(body);
        if (resp == NULL) break;

        cJSON *root = cJSON_Parse(resp);
        free(resp);
        if (root == NULL) { ESP_LOGE(TAG, "bad response JSON"); break; }

        cJSON *choices = cJSON_GetObjectItem(root, "choices");
        cJSON *first = cJSON_IsArray(choices) ? cJSON_GetArrayItem(choices, 0) : NULL;
        cJSON *msg = first ? cJSON_GetObjectItem(first, "message") : NULL;
        if (msg == NULL) {
            cJSON *error = cJSON_GetObjectItem(root, "error");
            cJSON *emsg = error ? cJSON_GetObjectItem(error, "message") : NULL;
            if (cJSON_IsString(emsg)) result = strdup(emsg->valuestring);
            cJSON_Delete(root);
            break;
        }

        cJSON *tool_calls = cJSON_GetObjectItem(msg, "tool_calls");
        if (cJSON_IsArray(tool_calls) && cJSON_GetArraySize(tool_calls) > 0) {
            /* Echo a clean assistant turn (content + tool_calls) back. */
            cJSON *am = cJSON_CreateObject();
            cJSON_AddStringToObject(am, "role", "assistant");
            cJSON *content = cJSON_GetObjectItem(msg, "content");
            if (cJSON_IsString(content))
                cJSON_AddStringToObject(am, "content", content->valuestring);
            else
                cJSON_AddNullToObject(am, "content");
            cJSON_AddItemToObject(am, "tool_calls", cJSON_Duplicate(tool_calls, true));
            cJSON_AddItemToArray(messages, am);

            /* Run each requested tool and append its result message. */
            cJSON *tc;
            cJSON_ArrayForEach(tc, tool_calls) {
                const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(tc, "id"));
                cJSON *fn = cJSON_GetObjectItem(tc, "function");
                const char *name = fn ? cJSON_GetStringValue(cJSON_GetObjectItem(fn, "name")) : NULL;
                const char *args = fn ? cJSON_GetStringValue(cJSON_GetObjectItem(fn, "arguments")) : NULL;
                ESP_LOGI(TAG, "tool_call: %s", name ? name : "(null)");

                cJSON *input = (args && args[0]) ? cJSON_Parse(args) : NULL;
                char *tres = tool_execute(name, input);
                cJSON_Delete(input);

                cJSON *trm = cJSON_CreateObject();
                cJSON_AddStringToObject(trm, "role", "tool");
                cJSON_AddStringToObject(trm, "tool_call_id", id ? id : "");
                cJSON_AddStringToObject(trm, "content", tres ? tres : "error");
                cJSON_AddItemToArray(messages, trm);
                free(tres);
            }
            cJSON_Delete(root);
            continue;   /* ask again with tool results */
        }

        cJSON *content = cJSON_GetObjectItem(msg, "content");
        result = strdup(cJSON_IsString(content) ? content->valuestring : "");
        cJSON_Delete(root);
        break;
    }

    if (result != NULL) {
        history_add("user", prompt);
        history_add("assistant", result);
        history_trim();
    }
    cJSON_Delete(messages);
    return result;
}

#else
/* ============================== Anthropic ================================== */

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

static char *build_body(const cJSON *messages)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return NULL;
    cJSON_AddStringToObject(root, "model", CONFIG_CLAUDE_MODEL);
    cJSON_AddNumberToObject(root, "max_tokens", CONFIG_CLAUDE_MAX_TOKENS);
    char *sysprompt = compose_system();
    cJSON_AddStringToObject(root, "system", sysprompt ? sysprompt : "");
    free(sysprompt);
    cJSON *tools = cJSON_Parse(TOOLS_JSON);
    if (tools) cJSON_AddItemToObject(root, "tools", tools);
    cJSON_AddItemToObject(root, "messages", cJSON_Duplicate(messages, true));
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return body;
}

char *claude_ask(const char *prompt)
{
    if (s_history == NULL) s_history = cJSON_CreateArray();

    cJSON *messages = cJSON_Duplicate(s_history, true);
    cJSON *um = cJSON_CreateObject();
    cJSON_AddStringToObject(um, "role", "user");
    cJSON_AddStringToObject(um, "content", prompt);
    cJSON_AddItemToArray(messages, um);

    char *result = NULL;

    for (int iter = 0; iter < 6 && result == NULL; iter++) {
        char *body = build_body(messages);
        if (body == NULL) break;
        char *resp = do_request(body);
        free(body);
        if (resp == NULL) break;

        cJSON *rroot = cJSON_Parse(resp);
        free(resp);
        if (rroot == NULL) { ESP_LOGE(TAG, "failed to parse response JSON"); break; }

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

        cJSON *am = cJSON_CreateObject();
        cJSON_AddStringToObject(am, "role", "assistant");
        cJSON_AddItemToObject(am, "content", cJSON_Duplicate(content, true));
        cJSON_AddItemToArray(messages, am);

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

    if (result != NULL) {
        history_add("user", prompt);
        history_add("assistant", result);
        history_trim();
    }

    cJSON_Delete(messages);
    return result;
}

#endif
