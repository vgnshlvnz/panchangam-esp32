/*
 * format_test.c -- host test for core/format.c.
 *
 * 1. Length test: every case in test/format_cases.txt (all 750 reference dates,
 *    written by tools/gen_format_cases.py) must produce an X text within
 *    PANCH_X_MAX_WEIGHT.
 * 2. Byte-for-byte test: the 30 cases that carry expected text (from the Python
 *    reference formatter) must match panch_format_slack / panch_format_x exactly.
 * 3. Name, karana-rule, X-weight and buffer-overflow unit checks.
 *
 * Usage: format_test [format_cases.txt]     Exit status non-zero on any failure.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/format.h"

#define UTC_OFFSET 28800

static int failures;

#define CHECK(cond, ...) \
    do { if (!(cond)) { failures++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- unit checks ---------------------------------------------------------- */

static void unit_checks(void)
{
    /* Karana rule on the half-tithi index k = index - 1. */
    CHECK(!strcmp(panch_karana_name(1), "Kimstughna"), "k=0");
    CHECK(!strcmp(panch_karana_name(58), "Shakuni"), "k=57");
    CHECK(!strcmp(panch_karana_name(59), "Chatushpada"), "k=58");
    CHECK(!strcmp(panch_karana_name(60), "Naga"), "k=59");
    CHECK(!strcmp(panch_karana_name(2), "Bava"), "k=1");
    CHECK(!strcmp(panch_karana_name(8), "Vishti"), "k=7");
    CHECK(!strcmp(panch_karana_name(9), "Bava"), "k=8");
    CHECK(panch_karana_name(0) == NULL && panch_karana_name(61) == NULL, "karana range");

    CHECK(!strcmp(panch_tithi_name(1), "Shukla Pratipada"), "tithi 1");
    CHECK(!strcmp(panch_tithi_name(15), "Purnima"), "tithi 15");
    CHECK(!strcmp(panch_tithi_name(25), "Krishna Dashami"), "tithi 25");
    CHECK(!strcmp(panch_tithi_name(30), "Amavasya"), "tithi 30");
    CHECK(!strcmp(panch_nakshatra_name(1), "Ashwini") && !strcmp(panch_nakshatra_name(27), "Revati"), "nakshatra");
    CHECK(!strcmp(panch_yoga_name(1), "Vishkambha") && !strcmp(panch_yoga_name(27), "Vaidhriti"), "yoga");
    CHECK(!strcmp(panch_vara_name(1), "Ravivara") && !strcmp(panch_vara_name(7), "Shanivara"), "vara");
    CHECK(!strcmp(panch_tamil_month_name(0), "Chithirai") && !strcmp(panch_tamil_month_name(11), "Panguni"), "tamil month");
    CHECK(panch_tithi_name(0) == NULL && panch_tithi_name(31) == NULL, "tithi range");

    /* X weight: Latin 1, Tamil letter 1 (U+0B85), emoji 2, CJK 2. */
    CHECK(panch_x_weight("abc") == 3, "ascii weight");
    CHECK(panch_x_weight("\xE0\xAE\x85") == 1, "tamil weight");
    CHECK(panch_x_weight("\xF0\x9F\x8C\x99") == 2, "emoji weight");
    CHECK(panch_x_weight("\xE4\xB8\xAD") == 2, "cjk weight");
    CHECK(panch_x_weight("a\n") == 2, "newline weight");
}

/* ---- case parser ---------------------------------------------------------- */

typedef struct {
    char date[16];
    panch_query_t q;
    panch_day_t d;
    char slack[2048];
    char x[1024];
} tcase_t;

static void add_span(panch_span_t *arr, int *count, int cap, const char *line)
{
    int idx;
    double a, b;
    if (sscanf(line + 2, "%d %lf %lf", &idx, &a, &b) != 3 || *count >= cap) {
        printf("bad span line: %s\n", line);
        exit(2);
    }
    arr[*count].index = idx;
    arr[*count].start_jd_ut = a;
    arr[*count].end_jd_ut = b;
    (*count)++;
}

static void set_period(panch_period_t *p, const char *line)
{
    if (sscanf(line + 2, "%lf %lf", &p->start_jd_ut, &p->end_jd_ut) != 2) {
        printf("bad period line: %s\n", line);
        exit(2);
    }
}

static void append_line(char *dst, size_t cap, const char *line)
{
    size_t len = strlen(dst);
    if (len + strlen(line) + 2 > cap) { printf("expected text too long\n"); exit(2); }
    if (len) dst[len++] = '\n';
    strcpy(dst + len, line);
}

static int n_compared;

static void run_case(const tcase_t *c)
{
    char buf[2048];

    /* Length test: every date. */
    int rc = panch_format_x(buf, sizeof buf, &c->q, &c->d, UTC_OFFSET);
    CHECK(rc == PANCH_OK, "%s x rc=%d", c->date, rc);
    CHECK(panch_x_weight(buf) <= PANCH_X_MAX_WEIGHT, "%s x weight %u", c->date,
          (unsigned)panch_x_weight(buf));

    if (c->x[0] == '\0') return; /* no expected text for this date */

    /* Byte-for-byte test against the Python formatter. */
    n_compared++;
    CHECK(strcmp(buf, c->x) == 0, "%s x differs:\n--- C\n%s\n--- Python\n%s", c->date, buf, c->x);

    rc = panch_format_slack(buf, sizeof buf, &c->q, &c->d, UTC_OFFSET);
    /* Expected Slack text ends with the last line's newline. */
    char want[2100];
    snprintf(want, sizeof want, "%s\n", c->slack);
    CHECK(rc == PANCH_OK, "%s slack rc=%d", c->date, rc);
    CHECK(strcmp(buf, want) == 0, "%s slack differs:\n--- C\n%s--- Python\n%s", c->date, buf, want);
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "test/format_cases.txt";
    unit_checks();

    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return 2; }

    static tcase_t c;
    char line[512];
    int ncases = 0, in_case = 0, mode = 0; /* mode 0 data, 1 slack, 2 x */
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '#' && !in_case) continue;
        if (!strncmp(line, "CASE ", 5)) {
            memset(&c, 0, sizeof c);
            snprintf(c.date, sizeof c.date, "%s", line + 5);
            in_case = 1; mode = 0;
        } else if (!strcmp(line, "@@SLACK")) { mode = 1;
        } else if (!strcmp(line, "@@X")) { mode = 2;
        } else if (!strcmp(line, "@@END")) {
            run_case(&c);
            ncases++;
            in_case = 0;
        } else if (mode == 1) { append_line(c.slack, sizeof c.slack, line);
        } else if (mode == 2) { append_line(c.x, sizeof c.x, line);
        } else if (line[0] == 'D') {
            sscanf(line + 2, "%d %d %d", &c.q.year, &c.q.month, &c.q.day);
        } else if (line[0] == 'J') {
            sscanf(line + 2, "%lf %lf %lf", &c.d.sunrise_jd_ut, &c.d.sunset_jd_ut, &c.d.next_sunrise_jd_ut);
        } else if (line[0] == 'V') {
            c.d.vara.index = atoi(line + 2);
        } else if (line[0] == 'T') { add_span(c.d.tithi.spans, &c.d.tithi.count, PANCH_MAX_TITHI, line);
        } else if (line[0] == 'N') { add_span(c.d.nakshatra.spans, &c.d.nakshatra.count, PANCH_MAX_NAKSHATRA, line);
        } else if (line[0] == 'Y') { add_span(c.d.yoga.spans, &c.d.yoga.count, PANCH_MAX_YOGA, line);
        } else if (line[0] == 'K') { add_span(c.d.karana.spans, &c.d.karana.count, PANCH_MAX_KARANA, line);
        } else if (line[0] == 'R') { set_period(&c.d.rahu_kalam, line);
        } else if (line[0] == 'M') { set_period(&c.d.yamagandam, line);
        } else if (line[0] == 'G') { set_period(&c.d.gulika, line);
        }
    }
    fclose(f);

    /* Too-small buffers must fail cleanly, never overrun. */
    if (ncases) {
        char tiny[16];
        CHECK(panch_format_slack(tiny, sizeof tiny, &c.q, &c.d, UTC_OFFSET) == PANCH_ERR_CAPACITY, "slack overflow rc");
        CHECK(panch_format_x(tiny, sizeof tiny, &c.q, &c.d, UTC_OFFSET) == PANCH_ERR_CAPACITY, "x overflow rc");
        CHECK(tiny[0] == '\0', "x overflow leaves empty string");
    }
    CHECK(ncases == 750, "expected 750 cases, got %d", ncases);
    CHECK(n_compared == 30, "expected 30 byte-for-byte cases, got %d", n_compared);

    printf("%d case(s) length-checked, %d compared byte for byte, %d failure(s)\n",
           ncases, n_compared, failures);
    return failures ? 1 : 0;
}
