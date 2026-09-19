#include "x_post.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#define TAG "x"

#define TWEET_URL       "https://api.x.com/2/tweets"
#define HTTP_TIMEOUT_MS 15000
#define RESP_MAX        2048

x_post_result_t x_post_tweet(const char *text, int *http_status)
{
    *http_status = -1;
    const char *token = CONFIG_X_OAUTH2_ACCESS_TOKEN;
    if (token[0] == '\0') {
        ESP_LOGW(TAG, "X_OAUTH2_ACCESS_TOKEN is empty; skipping X");
        return X_POST_NO_TOKEN;
    }

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "text", text);
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) {
        return X_POST_RETRYABLE;
    }

    static char resp[RESP_MAX];
    static char auth[1200];
    snprintf(auth, sizeof auth, "Bearer %s", token);
    resp[0] = '\0';

    esp_http_client_config_t cfg = {
        .url = TWEET_URL,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        free(body);
        return X_POST_RETRYABLE;
    }
    esp_http_client_set_header(c, "Authorization", auth);
    esp_http_client_set_header(c, "Content-Type", "application/json");

    int blen = (int)strlen(body);
    if (esp_http_client_open(c, blen) == ESP_OK &&
        esp_http_client_write(c, body, blen) == blen &&
        esp_http_client_fetch_headers(c) >= 0) {
        *http_status = esp_http_client_get_status_code(c);
        size_t total = 0;
        for (;;) {
            int r = esp_http_client_read(c, resp + total, (int)(sizeof resp - 1 - total));
            if (r <= 0 || (total += (size_t)r) >= sizeof resp - 1) break;
        }
        resp[total] = '\0';
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    free(body);
    memset(auth, 0, sizeof auth);

    x_post_result_t result = X_POST_RETRYABLE;
    int status = *http_status;
    if (status == 201) {
        /* Success body: {"data":{"id":"...","text":"..."}} */
        cJSON *json = cJSON_Parse(resp);
        const cJSON *data = json ? cJSON_GetObjectItem(json, "data") : NULL;
        const cJSON *id = data ? cJSON_GetObjectItem(data, "id") : NULL;
        ESP_LOGI(TAG, "tweet posted: HTTP 201, id %s", cJSON_IsString(id) ? id->valuestring : "?");
        cJSON_Delete(json);
        result = X_POST_OK;
    } else if (status == 401) {
        ESP_LOGE(TAG, "HTTP 401 Unauthorized: %.200s", resp);
        result = X_POST_UNAUTHORIZED;
    } else if (status == 403 && strstr(resp, "duplicate")) {
        ESP_LOGW(TAG, "HTTP 403: X rejected the text as duplicate content");
        result = X_POST_DUPLICATE;
    } else if (status < 0) {
        ESP_LOGE(TAG, "tweet failed: transport error");
    } else {
        ESP_LOGE(TAG, "tweet failed: HTTP %d %.200s", status, resp);
    }
    memset(resp, 0, sizeof resp);
    return result;
}
