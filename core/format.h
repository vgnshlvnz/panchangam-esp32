/*
 * format.h -- name lookup and message formatting for the Slack and X posts.
 *
 * Pure C99, no ESP-IDF, no libc time zone code: local time is UTC plus a fixed
 * offset supplied by the caller (MYT is 28800). Names come from the generated
 * core/names.h. The reference formatter is tools/gen_format_cases.py; the host
 * test (test/format_test.c) requires byte-identical output.
 *
 * Output is plain UTF-8 text. JSON escaping is the caller's job.
 */
#ifndef PANCHANGAM_FORMAT_H
#define PANCHANGAM_FORMAT_H

#include <stddef.h>

#include "panchangam.h"

#ifdef __cplusplus
extern "C" {
#endif

/* X's weighted limit: Latin and most scripts weigh 1, emoji and CJK weigh 2. */
#define PANCH_X_MAX_WEIGHT 280

/* Name lookups. NULL when the index is out of range. */
const char *panch_tithi_name(int index);       /* 1..30 */
const char *panch_nakshatra_name(int index);   /* 1..27 */
const char *panch_yoga_name(int index);        /* 1..27 */
const char *panch_karana_name(int index);      /* 1..60, the API index (half-tithi k = index - 1) */
const char *panch_vara_name(int index);        /* 1..7, Ravivara = 1 */
const char *panch_tamil_month_name(int month); /* 0..11, Chithirai = 0. Names only. */

/* X weighted length of a UTF-8 string (twitter-text rules). Invalid UTF-8
   bytes count as 2 each. */
size_t panch_x_weight(const char *utf8);

/* The Slack message body (Slack mrkdwn, includes all angas and periods).
   Returns PANCH_OK, PANCH_ERR_ARG, or PANCH_ERR_CAPACITY if buf is too small. */
int panch_format_slack(char *buf, size_t n, const panch_query_t *q,
                       const panch_day_t *d, int utc_offset_seconds);

/* The X post: date and vara, sunrise and sunset, tithi, nakshatra, yoga,
   karana; no URLs. If the weight exceeds PANCH_X_MAX_WEIGHT, karana is dropped,
   then yoga. Returns PANCH_ERR_CAPACITY if it still does not fit (or buf is too
   small); buf then holds an empty string. */
int panch_format_x(char *buf, size_t n, const panch_query_t *q,
                   const panch_day_t *d, int utc_offset_seconds);

#ifdef __cplusplus
}
#endif

#endif /* PANCHANGAM_FORMAT_H */
