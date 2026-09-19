#include "x_client.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "nvs.h"

#define TAG "x"

#define NVS_NS          "x_auth"
#define TOKEN_URL       "https://api.x.com/2/oauth2/token"
#define TWEET_URL       "https://api.x.com/2/tweets"
#define HTTP_TIMEOUT_MS 15000
#define RETRY_DELAY_MS  5000

#define CRED_MAX   256
#define ACCESS_MAX 1024
#define RESP_MAX   4096

/* ---- NVS ------------------------------------------------------------------ */

esp_err_t x_store_set(const char *key, const char *value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, key, value);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t x_store_clear(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_erase_all(h);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static bool store_get(nvs_handle_t h, const char *key, char *out, size_t cap)
{
    size_t len = cap;
    return nvs_get_str(h, key, out, &len) == ESP_OK && out[0] != '\0';
}

void x_print_status(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        printf("x_auth: nothing stored\n");
        return;
    }
    static const char *const keys[] = { X_KEY_CLIENT_ID, X_KEY_CLIENT_SECRET, X_KEY_REFRESH_TOKEN };
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        char v[CRED_MAX];
        size_t len = sizeof v;
        if (nvs_get_str(h, keys[i], v, &len) == ESP_OK && v[0]) {
            printf("%-14s set (%u chars)\n", keys[i], (unsigned)strlen(v));
        } else {
            printf("%-14s NOT SET\n", keys[i]);
        }
        memset(v, 0, sizeof v);
    }
    nvs_close(h);
}

/* ---- HTTP ----------------------------------------------------------------- */

/* One request. Returns the HTTP status, or -1 on a transport error. The
   response body (NUL-terminated, truncated to resp_cap - 1) goes to resp. */
static int http_request(esp_http_client_method_t method, const char *url, const char *auth,
                        const char *content_type, const char *body, char *resp, size_t resp_cap)
{
    resp[0] = '\0';
    esp_http_client_config_t cfg = {
        .url = url,
        .method = method,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return -1;
    esp_http_client_set_header(c, "Authorization", auth);
    esp_http_client_set_header(c, "Content-Type", content_type);

    int status = -1;
    int blen = (int)strlen(body);
    if (esp_http_client_open(c, blen) == ESP_OK) {
        if (esp_http_client_write(c, body, blen) == blen && esp_http_client_fetch_headers(c) >= 0) {
            status = esp_http_client_get_status_code(c);
            size_t total = 0;
            for (;;) {
                int r = esp_http_client_read(c, resp + total, (int)(resp_cap - 1 - total));
                if (r <= 0) break;
                total += (size_t)r;
                if (total >= resp_cap - 1) break;
            }
            resp[total] = '\0';
        }
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return status;
}

/* ---- OAuth refresh -------------------------------------------------------- */

static void urlencode(const char *in, char *out, size_t cap)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (; *in && o + 4 < cap; in++) {
        unsigned char ch = (unsigned char)*in;
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '.' || ch == '_' || ch == '~') {
            out[o++] = (char)ch;
        } else {
            out[o++] = '%';
            out[o++] = hex[ch >> 4];
            out[o++] = hex[ch & 15];
        }
    }
    out[o] = '\0';
}

typedef enum { REFRESH_OK, REFRESH_INVALID_GRANT, REFRESH_RETRYABLE } refresh_result_t;

/* Exchange the refresh token. On success: access token and the NEW refresh
   token (X rotates it on every use). Nothing sensitive is logged. */
static refresh_result_t refresh_once(const char *client_id, const char *client_secret,
                                     const char *refresh_token, char *access, size_t access_cap,
                                     char *new_refresh, size_t new_refresh_cap)
{
    char creds[2 * CRED_MAX + 2], b64[4 * CRED_MAX], auth[4 * CRED_MAX + 16];
    char enc[3 * CRED_MAX], body[3 * CRED_MAX + 64];
    static char resp[RESP_MAX];

    snprintf(creds, sizeof creds, "%s:%s", client_id, client_secret);
    size_t olen = 0;
    if (mbedtls_base64_encode((unsigned char *)b64, sizeof b64 - 1, &olen,
                              (const unsigned char *)creds, strlen(creds)) != 0) {
        return REFRESH_RETRYABLE;
    }
    b64[olen] = '\0';
    snprintf(auth, sizeof auth, "Basic %s", b64);
    urlencode(refresh_token, enc, sizeof enc);
    snprintf(body, sizeof body, "grant_type=refresh_token&refresh_token=%s", enc);

    int status = http_request(HTTP_METHOD_POST, TOKEN_URL, auth,
                              "application/x-www-form-urlencoded", body, resp, sizeof resp);
    memset(creds, 0, sizeof creds);
    memset(b64, 0, sizeof b64);
    memset(auth, 0, sizeof auth);
    memset(enc, 0, sizeof enc);
    memset(body, 0, sizeof body);

    refresh_result_t result = REFRESH_RETRYABLE;
    cJSON *json = status > 0 ? cJSON_Parse(resp) : NULL;
    if (status == 200 && json) {
        const cJSON *at = cJSON_GetObjectItem(json, "access_token");
        const cJSON *rt = cJSON_GetObjectItem(json, "refresh_token");
        if (cJSON_IsString(at) && cJSON_IsString(rt) &&
            strlen(at->valuestring) < access_cap && strlen(rt->valuestring) < new_refresh_cap) {
            strcpy(access, at->valuestring);
            strcpy(new_refresh, rt->valuestring);
            result = REFRESH_OK;
        } else {
            ESP_LOGE(TAG, "refresh: 200 but response lacks usable access_token/refresh_token");
        }
    } else if (json) {
        const cJSON *err = cJSON_GetObjectItem(json, "error");
        const cJSON *desc = cJSON_GetObjectItem(json, "error_description");
        ESP_LOGE(TAG, "refresh: HTTP %d error=%s (%s)", status,
                 cJSON_IsString(err) ? err->valuestring : "?",
                 cJSON_IsString(desc) ? desc->valuestring : "");
        if (cJSON_IsString(err) && strcmp(err->valuestring, "invalid_grant") == 0) {
            result = REFRESH_INVALID_GRANT;
        }
    } else {
        ESP_LOGE(TAG, "refresh: transport failure or unparseable reply (HTTP %d)", status);
    }
    cJSON_Delete(json);
    memset(resp, 0, sizeof resp);
    return result;
}

/* ---- tweet ---------------------------------------------------------------- */

typedef enum { TWEET_OK, TWEET_DUPLICATE, TWEET_RETRYABLE } tweet_result_t;

static tweet_result_t tweet_once(const char *access, const char *text)
{
    static char resp[RESP_MAX];
    char auth[ACCESS_MAX + 16];
    snprintf(auth, sizeof auth, "Bearer %s", access);

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "text", text);
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return TWEET_RETRYABLE;

    int status = http_request(HTTP_METHOD_POST, TWEET_URL, auth, "application/json", body,
                              resp, sizeof resp);
    free(body);
    memset(auth, 0, sizeof auth);

    tweet_result_t result = TWEET_RETRYABLE;
    if (status == 201) {
        ESP_LOGI(TAG, "tweet posted (HTTP 201)");
        result = TWEET_OK;
    } else if (status == 403 && strstr(resp, "duplicate")) {
        ESP_LOGW(TAG, "X rejected the tweet as duplicate content (HTTP 403); not retrying");
        result = TWEET_DUPLICATE;
    } else {
        ESP_LOGE(TAG, "tweet failed: HTTP %d %.200s", status, resp);
    }
    memset(resp, 0, sizeof resp);
    return result;
}

/* ---- the daily cycle ------------------------------------------------------ */

x_result_t x_post(const char *text)
{
    char client_id[CRED_MAX], client_secret[CRED_MAX], refresh[CRED_MAX];
    nvs_handle_t h;
    bool have = nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK;
    if (have) {
        have = store_get(h, X_KEY_CLIENT_ID, client_id, sizeof client_id) &&
               store_get(h, X_KEY_CLIENT_SECRET, client_secret, sizeof client_secret) &&
               store_get(h, X_KEY_REFRESH_TOKEN, refresh, sizeof refresh);
        nvs_close(h);
    }
    if (!have) {
        ESP_LOGW(TAG, "X credentials not set in NVS; skipping X (see x_status in the console)");
        return XPOST_NO_CREDENTIALS;
    }

    char access[ACCESS_MAX], new_refresh[CRED_MAX];
    x_result_t out = XPOST_FAILED;

    /* 1. Refresh: at most one retry, never after invalid_grant. */
    refresh_result_t rr = REFRESH_RETRYABLE;
    for (int attempt = 0; attempt < 2 && rr == REFRESH_RETRYABLE; attempt++) {
        if (attempt) {
            ESP_LOGW(TAG, "retrying token refresh once");
            vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
        }
        rr = refresh_once(client_id, client_secret, refresh, access, sizeof access,
                          new_refresh, sizeof new_refresh);
    }
    memset(client_secret, 0, sizeof client_secret);
    memset(refresh, 0, sizeof refresh);

    if (rr == REFRESH_INVALID_GRANT) {
        out = XPOST_INVALID_GRANT;
    } else if (rr == REFRESH_OK) {
        /* 2. Persist the rotated refresh token BEFORE using the access token. */
        esp_err_t err = x_store_set(X_KEY_REFRESH_TOKEN, new_refresh);
        memset(new_refresh, 0, sizeof new_refresh);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "could not store the new refresh token (%s); not posting", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "refresh token rotated and stored");
            /* 3. Post: at most one retry, never after a duplicate rejection. */
            tweet_result_t tr = TWEET_RETRYABLE;
            for (int attempt = 0; attempt < 2 && tr == TWEET_RETRYABLE; attempt++) {
                if (attempt) {
                    ESP_LOGW(TAG, "retrying tweet once");
                    vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
                }
                tr = tweet_once(access, text);
            }
            out = tr == TWEET_OK ? XPOST_OK : tr == TWEET_DUPLICATE ? XPOST_DUPLICATE : XPOST_FAILED;
        }
    }
    memset(access, 0, sizeof access);
    memset(client_id, 0, sizeof client_id);
    return out;
}
