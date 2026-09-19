#include "x_post.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include <time.h>

#define TAG "x"

#define TWEET_URL       "https://api.x.com/2/tweets"
#define HTTP_TIMEOUT_MS 15000
#define RESP_MAX        2048

/* RFC 3986 percent-encoding, as OAuth 1.0a requires. Returns false if it does not fit. */
static bool pct_encode(const char *in, char *out, size_t n)
{
    size_t o = 0;
    for (; *in; in++) {
        unsigned char c = (unsigned char)*in;
        bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                     c == '-' || c == '.' || c == '_' || c == '~';
        if (o + (plain ? 1 : 3) >= n) return false;
        o += plain ? (size_t)sprintf(out + o, "%c", c) : (size_t)sprintf(out + o, "%%%02X", c);
    }
    out[o] = '\0';
    return true;
}

/* Build the OAuth 1.0a Authorization header for POST TWEET_URL. The JSON body
   is not signed. */
static bool oauth1_header(char *hdr, size_t n)
{
    char nonce[33], ts[24];
    uint32_t r[4];
    esp_fill_random(r, sizeof r);
    snprintf(nonce, sizeof nonce, "%08x%08x%08x%08x", (unsigned)r[0], (unsigned)r[1], (unsigned)r[2], (unsigned)r[3]);
    snprintf(ts, sizeof ts, "%lld", (long long)time(NULL));

    static char key[128], tok[128], ksec[128], tsec[128];
    static char params[768], enc_params[1024], enc_url[128], base[1400], skey[300];
    if (!pct_encode(CONFIG_X_OAUTH1_CONSUMER_KEY, key, sizeof key) ||
        !pct_encode(CONFIG_X_OAUTH1_ACCESS_TOKEN, tok, sizeof tok) ||
        !pct_encode(CONFIG_X_OAUTH1_CONSUMER_SECRET, ksec, sizeof ksec) ||
        !pct_encode(CONFIG_X_OAUTH1_ACCESS_TOKEN_SECRET, tsec, sizeof tsec)) {
        return false;
    }
    /* Parameters in alphabetical order by name. */
    snprintf(params, sizeof params,
             "oauth_consumer_key=%s&oauth_nonce=%s&oauth_signature_method=HMAC-SHA1"
             "&oauth_timestamp=%s&oauth_token=%s&oauth_version=1.0",
             key, nonce, ts, tok);
    if (!pct_encode(params, enc_params, sizeof enc_params) || !pct_encode(TWEET_URL, enc_url, sizeof enc_url)) {
        return false;
    }
    snprintf(base, sizeof base, "POST&%s&%s", enc_url, enc_params);
    snprintf(skey, sizeof skey, "%s&%s", ksec, tsec);

    unsigned char mac[20];
    unsigned char b64[32];
    size_t b64len = 0;
    if (mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA1), (const unsigned char *)skey, strlen(skey),
                        (const unsigned char *)base, strlen(base), mac) != 0 ||
        mbedtls_base64_encode(b64, sizeof b64, &b64len, mac, sizeof mac) != 0) {
        return false;
    }
    b64[b64len] = '\0';
    char sig[64];
    if (!pct_encode((const char *)b64, sig, sizeof sig)) return false;

    int len = snprintf(hdr, n,
                       "OAuth oauth_consumer_key=\"%s\", oauth_nonce=\"%s\", oauth_signature=\"%s\", "
                       "oauth_signature_method=\"HMAC-SHA1\", oauth_timestamp=\"%s\", oauth_token=\"%s\", "
                       "oauth_version=\"1.0\"",
                       key, nonce, sig, ts, tok);
    memset(skey, 0, sizeof skey);
    memset(ksec, 0, sizeof ksec);
    memset(tsec, 0, sizeof tsec);
    return len > 0 && (size_t)len < n;
}

x_post_result_t x_post_tweet(const char *text, int *http_status)
{
    *http_status = -1;
    if (CONFIG_X_OAUTH1_CONSUMER_KEY[0] == '\0' || CONFIG_X_OAUTH1_CONSUMER_SECRET[0] == '\0' ||
        CONFIG_X_OAUTH1_ACCESS_TOKEN[0] == '\0' || CONFIG_X_OAUTH1_ACCESS_TOKEN_SECRET[0] == '\0') {
        ESP_LOGW(TAG, "X_OAUTH1_* credentials are not all set; skipping X");
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
    static char auth[600];
    if (!oauth1_header(auth, sizeof auth)) {
        ESP_LOGE(TAG, "could not build the OAuth 1.0a header");
        free(body);
        return X_POST_RETRYABLE;
    }
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
