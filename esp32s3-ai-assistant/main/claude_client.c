#include "claude_client.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "claude";

#define ANTHROPIC_URL "https://api.anthropic.com/v1/messages"

/* Response body accumulator, filled by the HTTP event handler. */
typedef struct {
    char  *buf;
    int    len;
} response_t;

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

/* Build the JSON request body:
 *   {"model": "...", "max_tokens": N, "messages": [{"role":"user","content":"..."}]}
 */
static char *build_request_body(const char *prompt)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "model", CONFIG_CLAUDE_MODEL);
    cJSON_AddNumberToObject(root, "max_tokens", CONFIG_CLAUDE_MAX_TOKENS);

    cJSON *messages = cJSON_AddArrayToObject(root, "messages");
    cJSON *message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "role", "user");
    cJSON_AddStringToObject(message, "content", prompt);
    cJSON_AddItemToArray(messages, message);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return body;
}

/* Parse the API response and return the concatenated text blocks, or the
 * error message if the API returned an error object. */
static char *parse_response(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        ESP_LOGE(TAG, "failed to parse JSON response");
        return NULL;
    }

    char *result = NULL;
    cJSON *content = cJSON_GetObjectItem(root, "content");

    if (cJSON_IsArray(content)) {
        /* Sum the length of all text blocks first, then concatenate. */
        size_t total = 1;
        cJSON *block;
        cJSON_ArrayForEach(block, content) {
            cJSON *type = cJSON_GetObjectItem(block, "type");
            cJSON *text = cJSON_GetObjectItem(block, "text");
            if (cJSON_IsString(type) && strcmp(type->valuestring, "text") == 0 &&
                cJSON_IsString(text)) {
                total += strlen(text->valuestring);
            }
        }
        result = malloc(total);
        if (result != NULL) {
            result[0] = '\0';
            cJSON_ArrayForEach(block, content) {
                cJSON *type = cJSON_GetObjectItem(block, "type");
                cJSON *text = cJSON_GetObjectItem(block, "text");
                if (cJSON_IsString(type) && strcmp(type->valuestring, "text") == 0 &&
                    cJSON_IsString(text)) {
                    strcat(result, text->valuestring);
                }
            }
        }
    } else {
        /* Error path: {"error": {"message": "...", "type": "..."}} */
        cJSON *error = cJSON_GetObjectItem(root, "error");
        cJSON *message = error ? cJSON_GetObjectItem(error, "message") : NULL;
        if (cJSON_IsString(message)) {
            result = strdup(message->valuestring);
        }
    }

    cJSON_Delete(root);
    return result;
}

char *claude_ask(const char *prompt)
{
    char *body = build_request_body(prompt);
    if (body == NULL) {
        return NULL;
    }

    response_t resp = {0};
    esp_http_client_config_t config = {
        .url = ANTHROPIC_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 30000,
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
    free(body);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        free(resp.buf);
        return NULL;
    }

    if (status != 200) {
        ESP_LOGW(TAG, "API returned HTTP %d", status);
    }

    char *reply = (resp.buf != NULL) ? parse_response(resp.buf) : NULL;
    free(resp.buf);
    return reply;
}
