/*
 * compare.c -- host test: run panch_compute_day over every case in a
 * reference JSON file (schema: test/SCHEMA.md) and report mismatches.
 *
 * Usage: compare [reference.json]      (default: test/reference.json)
 * Pass: anga indices and list counts equal exactly; every instant within
 * TOL_SECONDS. Exit status is non-zero on any mismatch.
 */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/panchangam.h"

#define TOL_SECONDS 5.0

/* ---- minimal JSON reader (numbers, strings, arrays, objects) ------------- */

typedef enum { J_NULL, J_NUM, J_STR, J_ARR, J_OBJ } jtype_t;

typedef struct jnode {
    jtype_t type;
    double num;
    char *str;              /* J_STR value */
    char **keys;            /* J_OBJ keys, parallel to items */
    struct jnode **items;   /* J_ARR / J_OBJ children */
    int n;
} jnode_t;

static const char *p;

static void die(const char *msg)
{
    fprintf(stderr, "json: %s near \"%.20s\"\n", msg, p);
    exit(2);
}

static void skip_ws(void) { while (isspace((unsigned char)*p)) p++; }

static char *parse_string(void)
{
    const char *s;
    char *out;
    size_t n = 0;

    if (*p != '"') die("expected string");
    s = ++p;
    while (*p && *p != '"') p += (*p == '\\' && p[1]) ? 2 : 1;
    if (*p != '"') die("unterminated string");
    out = malloc((size_t)(p - s) + 1);
    for (; s < p; s++) {
        if (*s == '\\' && s + 1 < p) s++;  /* keep the escaped char; \uXXXX is not needed here */
        out[n++] = *s;
    }
    out[n] = '\0';
    p++;
    return out;
}

static jnode_t *parse_value(void)
{
    jnode_t *node = calloc(1, sizeof *node);
    char close;

    skip_ws();
    if (*p == '"') {
        node->type = J_STR;
        node->str = parse_string();
    } else if (*p == '[' || *p == '{') {
        node->type = (*p == '[') ? J_ARR : J_OBJ;
        close = (*p == '[') ? ']' : '}';
        p++;
        skip_ws();
        while (*p != close) {
            node->items = realloc(node->items, (size_t)(node->n + 1) * sizeof *node->items);
            if (node->type == J_OBJ) {
                node->keys = realloc(node->keys, (size_t)(node->n + 1) * sizeof *node->keys);
                skip_ws();
                node->keys[node->n] = parse_string();
                skip_ws();
                if (*p++ != ':') die("expected ':'");
            }
            node->items[node->n++] = parse_value();
            skip_ws();
            if (*p == ',') { p++; skip_ws(); }
            else if (*p != close) die("expected ',' or close");
        }
        p++;
    } else if (!strncmp(p, "null", 4) || !strncmp(p, "true", 4) || !strncmp(p, "false", 5)) {
        p += (*p == 'f') ? 5 : 4;   /* booleans/null are not used by the schema */
    } else {
        char *end;
        node->type = J_NUM;
        node->num = strtod(p, &end);
        if (end == p) die("unexpected token");
        p = end;
    }
    return node;
}

static jnode_t *get(const jnode_t *obj, const char *key)
{
    int i;
    if (obj && obj->type == J_OBJ)
        for (i = 0; i < obj->n; i++)
            if (!strcmp(obj->keys[i], key)) return obj->items[i];
    fprintf(stderr, "missing key \"%s\"\n", key);
    exit(2);
}

static int has(const jnode_t *obj, const char *key)
{
    int i;
    for (i = 0; i < obj->n; i++)
        if (!strcmp(obj->keys[i], key)) return 1;
    return 0;
}

/* ---- comparison ---------------------------------------------------------- */

static int index_mismatches, time_mismatches;
static const char *cur_date;

static void check_time(const char *what, double got, const jnode_t *inst)
{
    double ref = get(inst, "jd_ut")->num;
    double diff = (got - ref) * 86400.0;
    if (fabs(diff) > TOL_SECONDS) {
        printf("%s  %-22s TIME  c=%.6f ref=%.6f diff=%+.2fs\n", cur_date, what, got, ref, diff);
        time_mismatches++;
    }
}

static void check_index(const char *what, int got, int ref)
{
    if (got != ref) {
        printf("%s  %-22s INDEX c=%d ref=%d\n", cur_date, what, got, ref);
        index_mismatches++;
    }
}

static void check_spans(const char *name, const panch_span_t *spans, int count,
                        const jnode_t *ref)
{
    char label[48];
    int i;

    if (count != ref->n) {
        printf("%s  %-22s COUNT c=%d ref=%d\n", cur_date, name, count, ref->n);
        index_mismatches++;
        return;
    }
    for (i = 0; i < count; i++) {
        snprintf(label, sizeof label, "%s[%d].index", name, i);
        check_index(label, spans[i].index, (int)get(ref->items[i], "index")->num);
        snprintf(label, sizeof label, "%s[%d].start", name, i);
        check_time(label, spans[i].start_jd_ut, get(ref->items[i], "start"));
        snprintf(label, sizeof label, "%s[%d].end", name, i);
        check_time(label, spans[i].end_jd_ut, get(ref->items[i], "end"));
    }
}

static void check_period(const char *name, const panch_period_t *got, const jnode_t *ref)
{
    char label[48];
    snprintf(label, sizeof label, "%s.start", name);
    check_time(label, got->start_jd_ut, get(ref, "start"));
    snprintf(label, sizeof label, "%s.end", name);
    check_time(label, got->end_jd_ut, get(ref, "end"));
}

static const char *err_name(int err)
{
    static const char *names[] = {"PANCH_OK", "PANCH_ERR_ARG", "PANCH_ERR_EPHEMERIS",
                                  "PANCH_ERR_CIRCUMPOLAR", "PANCH_ERR_NO_CROSSING",
                                  "PANCH_ERR_CAPACITY"};
    return (err >= 0 && err <= PANCH_ERR_CAPACITY) ? names[err] : "?";
}

static void run_case(const jnode_t *c, const panch_place_t *place)
{
    const jnode_t *in = get(c, "input");
    panch_query_t q;
    panch_day_t d;
    panch_span_t vara;
    const jnode_t *v;
    int err;

    cur_date = get(c, "date")->str;
    q.year = (int)get(in, "year")->num;
    q.month = (int)get(in, "month")->num;
    q.day = (int)get(in, "day")->num;
    q.day_start_jd_ut = get(in, "day_start_jd_ut")->num;
    q.next_day_start_jd_ut = get(in, "next_day_start_jd_ut")->num;
    q.place = *place;

    err = panch_compute_day(&q, &d);
    if (has(c, "expect_error")) {
        if (strcmp(err_name(err), get(c, "expect_error")->str)) {
            printf("%s  error                  INDEX c=%s ref=%s\n", cur_date,
                   err_name(err), get(c, "expect_error")->str);
            index_mismatches++;
        }
        return;
    }
    if (err != PANCH_OK) {
        printf("%s  panch_compute_day failed: %s\n", cur_date, panch_strerror(err));
        index_mismatches++;
        return;
    }

    check_time("sunrise", d.sunrise_jd_ut, get(c, "sunrise"));
    check_time("sunset", d.sunset_jd_ut, get(c, "sunset"));
    check_time("next_sunrise", d.next_sunrise_jd_ut, get(c, "next_sunrise"));

    v = get(c, "vara");
    vara = d.vara;
    check_index("vara.index", vara.index, (int)get(v, "index")->num);
    check_time("vara.start", vara.start_jd_ut, get(v, "start"));
    check_time("vara.end", vara.end_jd_ut, get(v, "end"));

    check_spans("tithi", d.tithi.spans, d.tithi.count, get(c, "tithi"));
    check_spans("nakshatra", d.nakshatra.spans, d.nakshatra.count, get(c, "nakshatra"));
    check_spans("yoga", d.yoga.spans, d.yoga.count, get(c, "yoga"));
    check_spans("karana", d.karana.spans, d.karana.count, get(c, "karana"));

    check_period("rahu_kalam", &d.rahu_kalam, get(c, "rahu_kalam"));
    check_period("yamagandam", &d.yamagandam, get(c, "yamagandam"));
    check_period("gulika", &d.gulika, get(c, "gulika"));
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "test/reference.json";
    FILE *f = fopen(path, "rb");
    long len;
    char *text;
    jnode_t *root, *cases, *mp;
    panch_place_t place;
    int i;

    if (!f) { perror(path); return 2; }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    text = malloc((size_t)len + 1);
    if (fread(text, 1, (size_t)len, f) != (size_t)len) { perror("read"); return 2; }
    text[len] = '\0';
    fclose(f);

    p = text;
    root = parse_value();
    mp = get(get(root, "meta"), "place");
    place.latitude_deg = get(mp, "latitude_deg")->num;
    place.longitude_deg = get(mp, "longitude_deg")->num;
    place.elevation_m = get(mp, "elevation_m")->num;

    cases = get(root, "cases");
    for (i = 0; i < cases->n; i++) run_case(cases->items[i], &place);

    printf("%d case(s): %d index/count mismatch(es), %d time mismatch(es) (tolerance %.1f s)\n",
           cases->n, index_mismatches, time_mismatches, TOL_SECONDS);
    return (index_mismatches || time_mismatches) ? 1 : 0;
}
