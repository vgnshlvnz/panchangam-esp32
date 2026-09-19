/*
 * Posts the daily panchangam to a Slack Incoming Webhook at 05:30 MYT.
 *
 * All calculations run in one dedicated task (swisseph is not thread-safe).
 * Nothing is posted until SNTP has synced.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "cJSON.h"
#include "format.h"
#include "panchangam.h"
#include "x_post.h"

#define TAG "panch"

#define X_RETRY_DELAY_MS    (60 * 1000)
#define TZ_MYT              "MYT-8"
#define MYT_OFFSET_SECONDS  (8 * 3600)
#define CALC_TASK_STACK     (32 * 1024)
#define UNIX_EPOCH_JD       2440587.5
#define MIN_VALID_EPOCH     1735689600  /* 2025-01-01: anything earlier means "not synced" */

static EventGroupHandle_t s_wifi_events;
#define WIFI_CONNECTED_BIT  BIT0

/* ---- Wi-Fi --------------------------------------------------------------- */

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static void wifi_start(void)
{
    s_wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));

    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, CONFIG_PANCH_WIFI_SSID, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, CONFIG_PANCH_WIFI_PASSWORD, sizeof(wc.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* ---- time ---------------------------------------------------------------- */

static double unix_to_jd(time_t t)
{
    return UNIX_EPOCH_JD + (double)t / 86400.0;
}

static bool time_is_synced(void)
{
    return time(NULL) >= MIN_VALID_EPOCH;
}

static void time_start(void)
{
    setenv("TZ", TZ_MYT, 1);
    tzset();
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    ESP_ERROR_CHECK(esp_netif_sntp_init(&cfg));
}

/* Seconds until the next HH:MM local time, given now. Always > 0. */
static time_t seconds_until_post(time_t now)
{
    struct tm tm;
    localtime_r(&now, &tm);
    tm.tm_hour = CONFIG_PANCH_POST_HOUR;
    tm.tm_min = CONFIG_PANCH_POST_MINUTE;
    tm.tm_sec = 0;
    time_t target = mktime(&tm);
    if (target <= now) {
        tm.tm_mday += 1;
        target = mktime(&tm);
    }
    return target - now;
}

/* ---- Slack --------------------------------------------------------------- */

/* Post plain text to the Slack webhook as {"text": ...}. */
static esp_err_t slack_send_text(const char *text)
{
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "text", text);
    char *json = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t cfg = {
        .url = CONFIG_PANCH_SLACK_WEBHOOK_URL,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_http_client_set_header(c, "Content-Type", "application/json");
    esp_http_client_set_post_field(c, json, strlen(json));
    esp_err_t err = esp_http_client_perform(c);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(c);
        ESP_LOGI(TAG, "Slack HTTP %d", status);
        if (status != 200) {
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "Slack POST failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(c);
    free(json);
    return err;
}

/* ---- the day ------------------------------------------------------------- */

/* Query and result for the local civil date containing `when`. */
static int compute_day(time_t when, panch_query_t *q, panch_day_t *day)
{
    struct tm tm;
    localtime_r(&when, &tm);
    tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
    time_t start = mktime(&tm);
    struct tm next = tm;
    next.tm_mday += 1;
    time_t next_start = mktime(&next);

    *q = (panch_query_t){
        .year = tm.tm_year + 1900,
        .month = tm.tm_mon + 1,
        .day = tm.tm_mday,
        .day_start_jd_ut = unix_to_jd(start),
        .next_day_start_jd_ut = unix_to_jd(next_start),
        .place = {
            .latitude_deg = CONFIG_PANCH_LATITUDE_MDEG / 1000.0,
            .longitude_deg = CONFIG_PANCH_LONGITUDE_MDEG / 1000.0,
            .elevation_m = CONFIG_PANCH_ELEVATION_M,
        },
    };
    int rc = panch_compute_day(q, day);
    if (rc != PANCH_OK) {
        ESP_LOGE(TAG, "panch_compute_day: %s", panch_strerror(rc));
    }
    return rc;
}

/* Post to X after Slack has already been sent, so X can never delay Slack.
   401: no retry, Slack alert, X skipped until the next daily post. Duplicate
   content: logged, no retry. Anything else: one retry after 60 s, then a Slack
   alert with the status code. */
static void x_publish(const char *text)
{
    int status;
    x_post_result_t r = x_post_tweet(text, &status);
    if (r == X_POST_RETRYABLE) {
        ESP_LOGW(TAG, "X post failed (HTTP %d); retrying once in %d s", status, X_RETRY_DELAY_MS / 1000);
        vTaskDelay(pdMS_TO_TICKS(X_RETRY_DELAY_MS));
        r = x_post_tweet(text, &status);
    }
    switch (r) {
    case X_POST_OK:
    case X_POST_NO_TOKEN: /* logged by x_post_tweet */
        break;
    case X_POST_UNAUTHORIZED:
        slack_send_text("X post failed: 401 Unauthorized. The access token may have expired. "
                        "Update X_OAUTH2_ACCESS_TOKEN.");
        break;
    case X_POST_DUPLICATE:
        break; /* logged by x_post_tweet */
    case X_POST_RETRYABLE:
    default: {
        char msg[96];
        if (status > 0) {
            snprintf(msg, sizeof msg, "X post failed: HTTP %d (after one retry).", status);
        } else {
            snprintf(msg, sizeof msg, "X post failed: network error (after one retry).");
        }
        slack_send_text(msg);
        break;
    }
    }
}

/* Compute and post the panchangam for the local civil date containing `now`:
   Slack first, then X. */
static void post_today(time_t now)
{
    panch_query_t q;
    static panch_day_t day;
    if (compute_day(now, &q, &day) != PANCH_OK) {
        return;
    }

    static char text[2048];
    if (panch_format_slack(text, sizeof text, &q, &day, MYT_OFFSET_SECONDS) == PANCH_OK) {
        ESP_LOGI(TAG, "%s", text);
        slack_send_text(text);
    } else {
        ESP_LOGE(TAG, "Slack text did not fit its buffer");
    }

    if (panch_format_x(text, sizeof text, &q, &day, MYT_OFFSET_SECONDS) == PANCH_OK) {
        ESP_LOGI(TAG, "X text (%u weighted):\n%s", (unsigned)panch_x_weight(text), text);
        x_publish(text);
    } else {
        ESP_LOGE(TAG, "X text does not fit %d weighted characters; skipping X", PANCH_X_MAX_WEIGHT);
    }
}

/* ---- the one calculation task -------------------------------------------- */

static void calc_task(void *arg)
{
    xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    while (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) != ESP_OK || !time_is_synced()) {
        ESP_LOGW(TAG, "waiting for SNTP sync; not posting");
    }
    ESP_LOGI(TAG, "time synced");

#ifdef CONFIG_PANCH_POST_AT_BOOT
    post_today(time(NULL));
#endif

    for (;;) {
        time_t now = time(NULL);
        if (!time_is_synced()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        time_t wait_s = seconds_until_post(now);
        ESP_LOGI(TAG, "next post in %lld s", (long long)wait_s);
        /* Sleep in bounded chunks so an SNTP step correction is picked up. */
        if (wait_s > 60) {
            vTaskDelay(pdMS_TO_TICKS(60 * 1000));
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(wait_s * 1000));
        if (!time_is_synced()) {
            continue;
        }
        post_today(time(NULL));
        vTaskDelay(pdMS_TO_TICKS(2000)); /* step past the trigger second */
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    wifi_start();
    time_start();
    xTaskCreate(calc_task, "calc", CALC_TASK_STACK, NULL, 5, NULL);
}
