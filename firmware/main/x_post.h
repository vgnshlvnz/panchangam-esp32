/*
 * x_post.h -- post one tweet with a static OAuth 2.0 user access token.
 *
 * The token comes from menuconfig (CONFIG_X_OAUTH2_ACCESS_TOKEN) and is used
 * as-is: no refresh flow, no OAuth 1.0a. Equivalent to
 *   curl -X POST https://api.x.com/2/tweets \
 *        -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
 *        -d '{"text": "..."}'
 */
#pragma once

typedef enum {
    X_POST_OK,            /* HTTP 201 */
    X_POST_NO_TOKEN,      /* no access token configured; nothing sent */
    X_POST_UNAUTHORIZED,  /* HTTP 401: do not retry */
    X_POST_DUPLICATE,     /* HTTP 403, duplicate content: do not retry */
    X_POST_RETRYABLE,     /* 5xx, network error, or anything else */
} x_post_result_t;

/* One attempt. `*http_status` gets the HTTP status, or -1 on a transport error. */
x_post_result_t x_post_tweet(const char *text, int *http_status);
