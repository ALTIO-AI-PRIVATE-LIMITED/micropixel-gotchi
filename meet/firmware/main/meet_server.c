// Talks to the Gotchi Meet server (meet/server in this repo) over HTTPS.

#include "meet_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "meet_server";

#define MAX_RESPONSE 4096

// Sends a request to CONFIG_GOTCHI_MEET_SERVER_URL + path (POST when `body`
// is given, else GET). Returns the HTTP status (or -1) and, when `response`
// is given, the body (caller frees it).
static int request(const char *path, const char *body, char **response)
{
    char url[256];
    snprintf(url, sizeof(url), "%s%s", CONFIG_GOTCHI_MEET_SERVER_URL, path);
    esp_http_client_config_t cfg = {
        .url = url,
        .method = body ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return -1;
    }
    char auth[160];
    snprintf(auth, sizeof(auth), "Bearer %s", CONFIG_GOTCHI_MEET_DEVICE_SECRET);
    esp_http_client_set_header(client, "Authorization", auth);
    if (body) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
    }

    int status = -1;
    size_t len = body ? strlen(body) : 0;
    if (esp_http_client_open(client, len) == ESP_OK &&
        (len == 0 || esp_http_client_write(client, body, len) == (int)len) &&
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

static bool copy_field(const cJSON *root, const char *name, char *dst, size_t size)
{
    const cJSON *item = cJSON_GetObjectItem(root, name);
    if (!cJSON_IsString(item) || strlen(item->valuestring) >= size) {
        return false;
    }
    strcpy(dst, item->valuestring);
    return true;
}

static void copy_truncated(const cJSON *root, const char *name, char *dst, size_t size)
{
    const cJSON *item = cJSON_GetObjectItem(root, name);
    snprintf(dst, size, "%s", cJSON_IsString(item) ? item->valuestring : "");
}

int meet_server_meetings(meeting_t *out, int max)
{
    char *response = NULL;
    int status = request("/api/meetings", NULL, &response);
    if (status != 200 || response == NULL) {
        ESP_LOGE(TAG, "meetings failed: HTTP %d %s", status, response ? response : "");
        free(response);
        return -1;
    }
    cJSON *root = cJSON_Parse(response);
    free(response);
    const cJSON *list = cJSON_GetObjectItem(root, "meetings");
    if (!cJSON_IsArray(list)) {
        cJSON_Delete(root);
        return -1;
    }
    int count = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, list) {
        if (count == max) {
            break;
        }
        meeting_t *m = &out[count];
        if (!copy_field(item, "id", m->id, sizeof(m->id))) {
            continue;
        }
        copy_truncated(item, "title", m->title, sizeof(m->title));
        copy_truncated(item, "label", m->label, sizeof(m->label));
        m->start = (int64_t)cJSON_GetNumberValue(cJSON_GetObjectItem(item, "start"));
        m->end = (int64_t)cJSON_GetNumberValue(cJSON_GetObjectItem(item, "end"));
        count++;
    }
    cJSON_Delete(root);
    return count;
}

bool meet_server_join(const char *event_id, meet_session_t *session)
{
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "eventId", event_id);
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    char *response = NULL;
    int status = request("/api/meet/join", body, &response);
    cJSON_free(body);
    if (status != 200 || response == NULL) {
        ESP_LOGE(TAG, "join failed: HTTP %d %s", status, response ? response : "");
        free(response);
        return false;
    }
    cJSON *root = cJSON_Parse(response);
    free(response);
    bool ok = root != NULL &&
              copy_field(root, "meetId", session->meet_id, sizeof(session->meet_id)) &&
              copy_field(root, "botId", session->bot_id, sizeof(session->bot_id)) &&
              copy_field(root, "url", session->url, sizeof(session->url)) &&
              copy_field(root, "token", session->token, sizeof(session->token)) &&
              copy_field(root, "identity", session->identity, sizeof(session->identity));
    cJSON_Delete(root);
    if (!ok) {
        ESP_LOGE(TAG, "join: unexpected response");
    }
    return ok;
}

void meet_server_leave(const meet_session_t *session)
{
    char body[192];
    snprintf(body, sizeof(body), "{\"meetId\":\"%s\",\"botId\":\"%s\"}", session->meet_id, session->bot_id);
    int status = request("/api/meet/leave", body, NULL);
    if (status != 200) {
        ESP_LOGW(TAG, "leave: HTTP %d", status);
    }
}
