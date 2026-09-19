/* format.c -- see format.h. Pure C99. */
#include "format.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "names.h"

#define UNIX_EPOCH_JD 2440587.5

/* ---- names ---------------------------------------------------------------- */

#define LOOKUP(fn, table, count) \
    const char *fn(int i) { return (i >= 1 && i <= (count)) ? table[i - 1] : NULL; }

LOOKUP(panch_tithi_name, PANCH_TITHI_NAMES, 30)
LOOKUP(panch_nakshatra_name, PANCH_NAKSHATRA_NAMES, 27)
LOOKUP(panch_yoga_name, PANCH_YOGA_NAMES, 27)
LOOKUP(panch_vara_name, PANCH_VARA_NAMES, 7)

const char *panch_tamil_month_name(int m)
{
    return (m >= 0 && m < 12) ? PANCH_TAMIL_MONTH_NAMES[m] : NULL;
}

/* Half-tithi k = index - 1: 0 Kimstughna, 57 Shakuni, 58 Chatushpada, 59 Naga,
   otherwise movable[(k - 1) % 7]. */
const char *panch_karana_name(int index)
{
    int k = index - 1;
    if (k < 0 || k > 59) return NULL;
    if (k == 0) return PANCH_KARANA_FIXED[0];
    if (k >= 57) return PANCH_KARANA_FIXED[k - 56];
    return PANCH_KARANA_MOVABLE[(k - 1) % 7];
}

/* ---- X weight ------------------------------------------------------------- */

/* twitter-text: these ranges weigh 1, everything else 2. */
static int cp_weight(unsigned long cp)
{
    if (cp <= 4351) return 1;
    if (cp >= 8192 && cp <= 8205) return 1;
    if (cp >= 8208 && cp <= 8223) return 1;
    if (cp >= 8242 && cp <= 8247) return 1;
    return 2;
}

size_t panch_x_weight(const char *s)
{
    size_t w = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned long cp;
        int len;
        if (p[0] < 0x80)                { cp = p[0];        len = 1; }
        else if ((p[0] & 0xE0) == 0xC0) { cp = p[0] & 0x1F; len = 2; }
        else if ((p[0] & 0xF0) == 0xE0) { cp = p[0] & 0x0F; len = 3; }
        else if ((p[0] & 0xF8) == 0xF0) { cp = p[0] & 0x07; len = 4; }
        else { w += 2; p++; continue; }
        int ok = 1;
        for (int i = 1; i < len; i++) {
            if ((p[i] & 0xC0) != 0x80) { ok = 0; break; }
            cp = (cp << 6) | (p[i] & 0x3F);
        }
        if (!ok) { w += 2; p++; continue; }
        w += (size_t)cp_weight(cp);
        p += len;
    }
    return w;
}

/* ---- text builder --------------------------------------------------------- */

typedef struct { char *buf; size_t n; size_t len; int overflow; } sb_t;

static void sb_printf(sb_t *sb, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (!sb->overflow) {
        int r = vsnprintf(sb->buf + sb->len, sb->n - sb->len, fmt, ap);
        if (r < 0 || (size_t)r >= sb->n - sb->len) sb->overflow = 1;
        else sb->len += (size_t)r;
    }
    va_end(ap);
}

/* Local HH:MM: whole seconds by round-half-up of the JD, plus the fixed offset. */
static void hhmm(char out[6], double jd_ut, int utc_offset_seconds)
{
    long long secs = (long long)floor((jd_ut - UNIX_EPOCH_JD) * 86400.0 + 0.5);
    long long local = secs + utc_offset_seconds;
    long long sod = ((local % 86400) + 86400) % 86400;
    snprintf(out, 6, "%02d:%02d", (int)(sod / 3600), (int)((sod % 3600) / 60));
}

static void offset_label(char out[16], int off)
{
    int a = off < 0 ? -off : off;
    char sign = off < 0 ? '-' : '+';
    if (a % 3600 == 0) snprintf(out, 16, "UTC%c%d", sign, a / 3600);
    else snprintf(out, 16, "UTC%c%d:%02d", sign, a / 3600, (a % 3600) / 60);
}

typedef const char *(*name_fn)(int);

static void put_spans(sb_t *sb, const char *label, const panch_span_t *s, int count,
                      name_fn names, int off)
{
    sb_printf(sb, "%s: ", label);
    for (int i = 0; i < count; i++) {
        char a[6], b[6];
        hhmm(a, s[i].start_jd_ut, off);
        hhmm(b, s[i].end_jd_ut, off);
        const char *nm = names(s[i].index);
        sb_printf(sb, "%s%s %s-%s", i ? ", " : "", nm ? nm : "?", a, b);
    }
    sb_printf(sb, "\n");
}

static void put_period(sb_t *sb, const char *label, const panch_period_t *p, int off)
{
    char a[6], b[6];
    hhmm(a, p->start_jd_ut, off);
    hhmm(b, p->end_jd_ut, off);
    sb_printf(sb, "%s %s-%s\n", label, a, b);
}

static int args_ok(char *buf, size_t n, const panch_query_t *q, const panch_day_t *d)
{
    return buf && n > 0 && q && d;
}

int panch_format_slack(char *buf, size_t n, const panch_query_t *q,
                       const panch_day_t *d, int off)
{
    if (!args_ok(buf, n, q, d)) return PANCH_ERR_ARG;
    sb_t sb = { buf, n, 0, 0 };
    char sr[6], ss[6], tz[16];
    hhmm(sr, d->sunrise_jd_ut, off);
    hhmm(ss, d->sunset_jd_ut, off);
    offset_label(tz, off);
    const char *vara = panch_vara_name(d->vara.index);
    sb_printf(&sb, "*Panchangam %04d-%02d-%02d* (%s, %s)\n", q->year, q->month, q->day,
              vara ? vara : "?", tz);
    sb_printf(&sb, "Sunrise %s  Sunset %s\n", sr, ss);
    put_spans(&sb, "Tithi", d->tithi.spans, d->tithi.count, panch_tithi_name, off);
    put_spans(&sb, "Nakshatra", d->nakshatra.spans, d->nakshatra.count, panch_nakshatra_name, off);
    put_spans(&sb, "Yoga", d->yoga.spans, d->yoga.count, panch_yoga_name, off);
    put_spans(&sb, "Karana", d->karana.spans, d->karana.count, panch_karana_name, off);
    put_period(&sb, "Rahu Kalam", &d->rahu_kalam, off);
    put_period(&sb, "Yamagandam", &d->yamagandam, off);
    put_period(&sb, "Gulika", &d->gulika, off);
    if (sb.overflow) { buf[0] = '\0'; return PANCH_ERR_CAPACITY; }
    return PANCH_OK;
}

static int build_x(sb_t *sb, const panch_query_t *q, const panch_day_t *d, int off,
                   int with_yoga, int with_karana)
{
    char sr[6], ss[6];
    hhmm(sr, d->sunrise_jd_ut, off);
    hhmm(ss, d->sunset_jd_ut, off);
    const char *vara = panch_vara_name(d->vara.index);
    sb_printf(sb, "\xF0\x9F\x8C\x99 Panchangam %04d-%02d-%02d, %s\n", q->year, q->month,
              q->day, vara ? vara : "?");
    sb_printf(sb, "Sunrise %s Sunset %s\n", sr, ss);
    put_spans(sb, "Tithi", d->tithi.spans, d->tithi.count, panch_tithi_name, off);
    put_spans(sb, "Nakshatra", d->nakshatra.spans, d->nakshatra.count, panch_nakshatra_name, off);
    if (with_yoga)
        put_spans(sb, "Yoga", d->yoga.spans, d->yoga.count, panch_yoga_name, off);
    if (with_karana)
        put_spans(sb, "Karana", d->karana.spans, d->karana.count, panch_karana_name, off);
    /* No trailing newline. */
    if (!sb->overflow && sb->len > 0 && sb->buf[sb->len - 1] == '\n') sb->buf[--sb->len] = '\0';
    return sb->overflow ? PANCH_ERR_CAPACITY : PANCH_OK;
}

int panch_format_x(char *buf, size_t n, const panch_query_t *q,
                   const panch_day_t *d, int off)
{
    if (!args_ok(buf, n, q, d)) return PANCH_ERR_ARG;
    static const int steps[3][2] = { {1, 1}, {1, 0}, {0, 0} }; /* {yoga, karana} */
    for (int i = 0; i < 3; i++) {
        sb_t sb = { buf, n, 0, 0 };
        if (build_x(&sb, q, d, off, steps[i][0], steps[i][1]) != PANCH_OK) continue;
        if (panch_x_weight(buf) <= PANCH_X_MAX_WEIGHT) return PANCH_OK;
    }
    buf[0] = '\0';
    return PANCH_ERR_CAPACITY;
}
