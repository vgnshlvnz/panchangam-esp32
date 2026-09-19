/*
 * x_client.h -- post to X (Twitter) with OAuth 2.0 refresh-token auth.
 *
 * Confidential client. Credentials live in NVS (namespace "x_auth"), set from
 * the serial console, never in the repo or sdkconfig:
 *     client_id, client_secret, refresh_token
 * Each post does: refresh -> persist the new refresh token (nvs_commit) ->
 * POST /2/tweets. The access token is never kept across calls.
 *
 * Call only from the calc task (it runs the ~10 s of TLS work; the console
 * task must not).
 */
#pragma once

#include "esp_err.h"

#define X_KEY_CLIENT_ID     "client_id"
#define X_KEY_CLIENT_SECRET "client_secret"
#define X_KEY_REFRESH_TOKEN "refresh_token"

typedef enum {
    XPOST_OK = 0,
    XPOST_NO_CREDENTIALS,   /* one of the three NVS values is missing; nothing sent */
    XPOST_INVALID_GRANT,    /* refresh token rejected; do not retry, re-seed needed */
    XPOST_DUPLICATE,        /* X rejected the text as duplicate content; not retried */
    XPOST_FAILED,           /* anything else, after at most one retry */
} x_result_t;

x_result_t x_post(const char *text);

/* Serial-console helpers. Values are never logged or printed. */
esp_err_t x_store_set(const char *key, const char *value);
esp_err_t x_store_clear(void);
void x_print_status(void);
