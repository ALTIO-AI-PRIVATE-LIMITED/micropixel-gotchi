// Talks to the Gotchi call server (call/server in this repo) over HTTPS.

#include "call_server.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "call_server";

#define MAX_RESPONSE 2048

// POSTs `body` to CONFIG_GOTCHI_CALL_SERVER_URL + path. Returns the HTTP
// status (or -1) and, when `response` is given, the body (caller frees it).
static int post_json(const char *path, const char *body, char **response)
{
    char url[256];
    snprintf(url, sizeof(url), "%s%s", CONFIG_GOTCHI_CALL_SERVER_URL, path);
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return -1;
    }
    char auth[160];
    snprintf(auth, sizeof(auth), "Bearer %s", CONFIG_GOTCHI_CALL_DEVICE_SECRET);
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    int status = -1;
    size_t len = strlen(body);
    if (esp_http_client_open(client, len) == ESP_OK &&
        esp_http_client_write(client, body, len) == (int)len &&
        esp_http_client_fetch_headers(client) >= 0) {
        status = esp_http_client_get_status_code(client);
        if (response != NULL) {
            char *buf = calloc(1, MAX_RESPONSE);
            int total = 0, n;
            while (buf && total < MAX_RESPONSE - 1 &&
                   (n = esp_http_client_read(client, buf + total, MAX_RESPONSE - 1 - total)) > 0) {
                total += n;
            }
            *response = buf;
        }
    }
    esp_http_client_cleanup(client);
    return status;
}

static bool copy_field(cJSON *root, const char *name, char *dst, size_t size)
{
    const cJSON *item = cJSON_GetObjectItem(root, name);
    if (!cJSON_IsString(item) || strlen(item->valuestring) >= size) {
        return false;
    }
    strcpy(dst, item->valuestring);
    return true;
}

bool call_server_start(call_info_t *info)
{
    char body[96];
    snprintf(body, sizeof(body), "{\"to\":\"%s\"}", CONFIG_GOTCHI_CALL_TO);
    char *response = NULL;
    int status = post_json("/api/call", body, &response);
    if (status != 200 || response == NULL) {
        ESP_LOGE(TAG, "start call failed: HTTP %d %s", status, response ? response : "");
        free(response);
        return false;
    }
    cJSON *root = cJSON_Parse(response);
    free(response);
    bool ok = root != NULL &&
              copy_field(root, "callId", info->call_id, sizeof(info->call_id)) &&
              copy_field(root, "url", info->url, sizeof(info->url)) &&
              copy_field(root, "token", info->token, sizeof(info->token)) &&
              copy_field(root, "identity", info->identity, sizeof(info->identity));
    cJSON_Delete(root);
    if (!ok) {
        ESP_LOGE(TAG, "start call: unexpected response");
    }
    return ok;
}

void call_server_end(const char *call_id)
{
    char body[96];
    snprintf(body, sizeof(body), "{\"callId\":\"%s\"}", call_id);
    int status = post_json("/api/call/end", body, NULL);
    if (status != 200) {
        ESP_LOGW(TAG, "end call: HTTP %d", status);
    }
}
