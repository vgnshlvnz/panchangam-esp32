/*
 * panchangam.c -- C99 port of the Python reference (panchangam_mcp:
 * src/panchangam/{ephemeris,angas,muhurta}.py). Conventions: CONVENTIONS.md.
 *
 * Pure C99: only swephexp.h, no ESP-IDF. Every instant is a JD in UT.
 * Not thread-safe: all swe_* calls must stay in one task.
 */
#include "panchangam.h"

#include <math.h>
#include <stddef.h>

#include "swephexp.h"

/* ephemeris.py:42,101 */
#define EPHE_FLAG     SEFLG_MOSEPH
#define SIDEREAL_FLAG (SEFLG_MOSEPH | SEFLG_SIDEREAL | SEFLG_SPEED)

/* angas.py:35-48 */
#define DEGREES_PER_TITHI      (360.0 / 30)
#define DEGREES_PER_NAKSHATRA  (360.0 / 27)
#define DEGREES_PER_YOGA       (360.0 / 27)
#define DEGREES_PER_KARANA     (DEGREES_PER_TITHI / 2)
#define TITHI_COUNT      30
#define NAKSHATRA_COUNT  27
#define YOGA_COUNT       27
#define KARANA_COUNT     60

/* angas.py:62 -- 30 h, in days */
#define BOUNDARY_SEARCH_DAYS (30.0 / 24.0)
/* ephemeris.py:272 -- find_crossing default tolerance, 1 s in days */
#define CROSSING_TOLERANCE_DAYS (1.0 / 86400.0)
#define ONE_SECOND_DAYS         (1.0 / 86400.0)

/* muhurta.py:34-36, indexed Sunday-first */
static const int RAHU_KALAM_PART[7]  = {8, 2, 7, 5, 6, 4, 3};
static const int YAMAGANDA_PART[7]   = {5, 4, 3, 2, 1, 7, 6};
static const int GULIKA_PART[7]      = {7, 6, 5, 4, 3, 2, 1};
#define DAYLIGHT_PARTS 8

typedef int (*angle_fn_t)(double jd_ut, double *deg_out);

/* Python: the `%` operator on floats (result takes the sign of the divisor). */
static double pymod(double x, double m)
{
    double r = fmod(x, m);
    if (r < 0.0) r += m;
    return r;
}

/* Python: ephemeris._ensure_configured / configure (Lahiri only). */
static void ensure_configured(void)
{
    static int configured = 0;
    if (!configured) {
        swe_set_sid_mode(SE_SIDM_LAHIRI, 0.0, 0.0);
        configured = 1;
    }
}

/* Python: ephemeris._calc (longitude only). */
static int calc_longitude(double jd_ut, int body, double *lon_out)
{
    double xx[6];
    char serr[256];

    ensure_configured();
    if (swe_calc_ut(jd_ut, body, SIDEREAL_FLAG, xx, serr) < 0)
        return PANCH_ERR_EPHEMERIS;
    *lon_out = pymod(xx[0], 360.0);
    return PANCH_OK;
}

/* Python: ephemeris.sun_longitude. */
static int sun_longitude(double jd_ut, double *out)
{
    return calc_longitude(jd_ut, SE_SUN, out);
}

/* Python: ephemeris.moon_longitude. */
static int moon_longitude(double jd_ut, double *out)
{
    return calc_longitude(jd_ut, SE_MOON, out);
}

/* Python: ephemeris.elongation. */
static int elongation(double jd_ut, double *out)
{
    double sun, moon;
    int err;

    if ((err = moon_longitude(jd_ut, &moon)) != PANCH_OK) return err;
    if ((err = sun_longitude(jd_ut, &sun)) != PANCH_OK) return err;
    *out = pymod(moon - sun, 360.0);
    return PANCH_OK;
}

/* Python: angas._yoga_sum. */
static int yoga_sum(double jd_ut, double *out)
{
    double sun, moon;
    int err;

    if ((err = sun_longitude(jd_ut, &sun)) != PANCH_OK) return err;
    if ((err = moon_longitude(jd_ut, &moon)) != PANCH_OK) return err;
    *out = pymod(sun + moon, 360.0);
    return PANCH_OK;
}

/* Python: ephemeris._signed_delta. */
static double signed_delta(double angle, double base)
{
    return pymod(angle - base + 180.0, 360.0) - 180.0;
}

/* Python: ephemeris.find_crossing (bisection, 1 s tolerance, midpoint result). */
static int find_crossing(angle_fn_t fn, double target_deg, double lo, double hi,
                         double *out)
{
    double base, end_angle, span, offset, mid, angle;
    int err;

    if (hi <= lo) return PANCH_ERR_NO_CROSSING;

    if ((err = fn(lo, &base)) != PANCH_OK) return err;
    if ((err = fn(hi, &end_angle)) != PANCH_OK) return err;
    span = signed_delta(end_angle, base);
    if (span <= 0.0) return PANCH_ERR_NO_CROSSING;

    offset = signed_delta(target_deg, base);
    if (!(offset >= 0.0 && offset <= span)) return PANCH_ERR_NO_CROSSING;
    if (offset == 0.0) { *out = lo; return PANCH_OK; }
    if (offset == span) { *out = hi; return PANCH_OK; }

    while (hi - lo > CROSSING_TOLERANCE_DAYS) {
        mid = lo + (hi - lo) / 2;
        if ((err = fn(mid, &angle)) != PANCH_OK) return err;
        if (signed_delta(angle, base) < offset)
            lo = mid;
        else
            hi = mid;
    }

    *out = lo + (hi - lo) / 2;
    return PANCH_OK;
}

/* Validation for the C API's PANCH_ERR_ARG; no Python counterpart
   (Python validates in server.py:build_place and via types). */
static int check_query(const panch_query_t *q)
{
    static const int mdays[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if (q == NULL) return PANCH_ERR_ARG;
    if (!(q->place.latitude_deg >= -90.0 && q->place.latitude_deg <= 90.0))
        return PANCH_ERR_ARG;
    if (!(q->place.longitude_deg >= -180.0 && q->place.longitude_deg <= 180.0))
        return PANCH_ERR_ARG;
    if (q->month < 1 || q->month > 12 || q->day < 1 || q->day > mdays[q->month - 1])
        return PANCH_ERR_ARG;
    if (!(q->next_day_start_jd_ut > q->day_start_jd_ut)) return PANCH_ERR_ARG;
    return PANCH_OK;
}

/* Python: ephemeris._sun_event. `day_start_jd_ut` is the local midnight the
   search starts from (Python: _julian_day_ut(day_start)). */
static int sun_event(int kind, double day_start_jd_ut, const panch_place_t *place,
                     double *jd_ut_out)
{
    double geopos[3], tret[10];
    char serr[256];

    ensure_configured();
    geopos[0] = place->longitude_deg;
    geopos[1] = place->latitude_deg;
    geopos[2] = place->elevation_m;
    if (swe_rise_trans(day_start_jd_ut, SE_SUN, NULL, EPHE_FLAG, kind, geopos,
                       0.0, 0.0, tret, serr) != 0)
        return PANCH_ERR_CIRCUMPOLAR;
    *jd_ut_out = tret[0];
    return PANCH_OK;
}

/* Python: ephemeris.sunrise. */
int panch_sunrise(const panch_query_t *query, double *jd_ut_out)
{
    int err = check_query(query);
    if (err != PANCH_OK) return err;
    if (jd_ut_out == NULL) return PANCH_ERR_ARG;
    return sun_event(SE_CALC_RISE, query->day_start_jd_ut, &query->place, jd_ut_out);
}

/* Python: ephemeris.sunset. */
int panch_sunset(const panch_query_t *query, double *jd_ut_out)
{
    int err = check_query(query);
    if (err != PANCH_OK) return err;
    if (jd_ut_out == NULL) return PANCH_ERR_ARG;
    return sun_event(SE_CALC_SET, query->day_start_jd_ut, &query->place, jd_ut_out);
}

/* Python: ephemeris.sunrise(on + timedelta(days=1), place), as used by
   angas.vara and angas._angam_spans. */
static int next_sunrise(const panch_query_t *q, double *jd_ut_out)
{
    return sun_event(SE_CALC_RISE, q->next_day_start_jd_ut, &q->place, jd_ut_out);
}

/* Python: date.isoweekday() % 7 -- Sunday = 0 .. Saturday = 6. */
static int weekday_sunday0(const panch_query_t *q)
{
    double jd = swe_julday(q->year, q->month, q->day, 0.0, SE_GREG_CAL);
    return (swe_day_of_week(jd) + 1) % 7;  /* swe: Monday = 0 .. Sunday = 6 */
}

/* Python: angas.vara. */
int panch_vara(const panch_query_t *query, panch_span_t *out)
{
    double start, end;
    int err = check_query(query);
    if (err != PANCH_OK) return err;
    if (out == NULL) return PANCH_ERR_ARG;

    if ((err = sun_event(SE_CALC_RISE, query->day_start_jd_ut, &query->place, &start)) != PANCH_OK)
        return err;
    if ((err = next_sunrise(query, &end)) != PANCH_OK) return err;
    out->index = weekday_sunday0(query) + 1;
    out->start_jd_ut = start;
    out->end_jd_ut = end;
    return PANCH_OK;
}

/* Python: angas._angam_spans. Writes up to `cap` spans, count in *n_out. */
static int angam_spans(angle_fn_t angle_fn, double step_deg, int count,
                       const panch_query_t *q, panch_span_t *spans, int cap,
                       int *n_out)
{
    double window_start, window_end, angle_at_start, current_start, next_boundary;
    int step_index, n = 0, err;

    if ((err = sun_event(SE_CALC_RISE, q->day_start_jd_ut, &q->place, &window_start)) != PANCH_OK)
        return err;
    if ((err = next_sunrise(q, &window_end)) != PANCH_OK) return err;

    if ((err = angle_fn(window_start, &angle_at_start)) != PANCH_OK) return err;
    step_index = (int)floor(angle_at_start / step_deg);

    err = find_crossing(angle_fn, pymod(step_index * step_deg, 360.0),
                        window_start - BOUNDARY_SEARCH_DAYS, window_start,
                        &current_start);
    if (err != PANCH_OK) return err;

    for (;;) {
        err = find_crossing(angle_fn, pymod((step_index + 1) * step_deg, 360.0),
                            current_start + ONE_SECOND_DAYS,
                            current_start + BOUNDARY_SEARCH_DAYS,
                            &next_boundary);
        if (err != PANCH_OK) return err;
        if (n >= cap) return PANCH_ERR_CAPACITY;
        spans[n].index = step_index % count + 1;
        spans[n].start_jd_ut = current_start;
        spans[n].end_jd_ut = next_boundary;
        n++;
        if (next_boundary >= window_end) {
            *n_out = n;
            return PANCH_OK;
        }
        step_index++;
        current_start = next_boundary;
    }
}

/* Python: angas.tithi. */
int panch_tithi(const panch_query_t *query, panch_tithi_list_t *out)
{
    int err = check_query(query);
    if (err != PANCH_OK) return err;
    if (out == NULL) return PANCH_ERR_ARG;
    return angam_spans(elongation, DEGREES_PER_TITHI, TITHI_COUNT, query,
                       out->spans, PANCH_MAX_TITHI, &out->count);
}

/* Python: angas.nakshatra. */
int panch_nakshatra(const panch_query_t *query, panch_nakshatra_list_t *out)
{
    int err = check_query(query);
    if (err != PANCH_OK) return err;
    if (out == NULL) return PANCH_ERR_ARG;
    return angam_spans(moon_longitude, DEGREES_PER_NAKSHATRA, NAKSHATRA_COUNT,
                       query, out->spans, PANCH_MAX_NAKSHATRA, &out->count);
}

/* Python: angas.yoga. */
int panch_yoga(const panch_query_t *query, panch_yoga_list_t *out)
{
    int err = check_query(query);
    if (err != PANCH_OK) return err;
    if (out == NULL) return PANCH_ERR_ARG;
    return angam_spans(yoga_sum, DEGREES_PER_YOGA, YOGA_COUNT, query,
                       out->spans, PANCH_MAX_YOGA, &out->count);
}

/* Python: angas.karana. */
int panch_karana(const panch_query_t *query, panch_karana_list_t *out)
{
    int err = check_query(query);
    if (err != PANCH_OK) return err;
    if (out == NULL) return PANCH_ERR_ARG;
    return angam_spans(elongation, DEGREES_PER_KARANA, KARANA_COUNT, query,
                       out->spans, PANCH_MAX_KARANA, &out->count);
}

/* Python: muhurta._partition + muhurta._weekday_part + muhurta._eighth. */
static int eighth(const panch_query_t *q, const int table[7], panch_period_t *out)
{
    double sunrise, sunset, width;
    int part, err;

    if ((err = sun_event(SE_CALC_RISE, q->day_start_jd_ut, &q->place, &sunrise)) != PANCH_OK)
        return err;
    if ((err = sun_event(SE_CALC_SET, q->day_start_jd_ut, &q->place, &sunset)) != PANCH_OK)
        return err;

    width = (sunset - sunrise) / DAYLIGHT_PARTS;
    part = table[weekday_sunday0(q)];
    out->start_jd_ut = sunrise + (part - 1) * width;
    out->end_jd_ut = (part == DAYLIGHT_PARTS) ? sunset : sunrise + part * width;
    return PANCH_OK;
}

/* Python: muhurta.rahu_kalam. */
int panch_rahu_kalam(const panch_query_t *query, panch_period_t *out)
{
    int err = check_query(query);
    if (err != PANCH_OK) return err;
    if (out == NULL) return PANCH_ERR_ARG;
    return eighth(query, RAHU_KALAM_PART, out);
}

/* Python: muhurta.yamaganda. */
int panch_yamagandam(const panch_query_t *query, panch_period_t *out)
{
    int err = check_query(query);
    if (err != PANCH_OK) return err;
    if (out == NULL) return PANCH_ERR_ARG;
    return eighth(query, YAMAGANDA_PART, out);
}

/* Python: muhurta.gulika. */
int panch_gulika(const panch_query_t *query, panch_period_t *out)
{
    int err = check_query(query);
    if (err != PANCH_OK) return err;
    if (out == NULL) return PANCH_ERR_ARG;
    return eighth(query, GULIKA_PART, out);
}

/* Python: none -- composition of the functions above (what server.py's
   panchangam tool calls one by one). */
int panch_compute_day(const panch_query_t *query, panch_day_t *out)
{
    int err = check_query(query);
    if (err != PANCH_OK) return err;
    if (out == NULL) return PANCH_ERR_ARG;

    if ((err = panch_sunrise(query, &out->sunrise_jd_ut)) != PANCH_OK) return err;
    if ((err = panch_sunset(query, &out->sunset_jd_ut)) != PANCH_OK) return err;
    if ((err = next_sunrise(query, &out->next_sunrise_jd_ut)) != PANCH_OK) return err;
    if ((err = panch_vara(query, &out->vara)) != PANCH_OK) return err;
    if ((err = panch_tithi(query, &out->tithi)) != PANCH_OK) return err;
    if ((err = panch_nakshatra(query, &out->nakshatra)) != PANCH_OK) return err;
    if ((err = panch_yoga(query, &out->yoga)) != PANCH_OK) return err;
    if ((err = panch_karana(query, &out->karana)) != PANCH_OK) return err;
    if ((err = panch_rahu_kalam(query, &out->rahu_kalam)) != PANCH_OK) return err;
    if ((err = panch_yamagandam(query, &out->yamagandam)) != PANCH_OK) return err;
    return panch_gulika(query, &out->gulika);
}

/* Python: none (exception messages). */
const char *panch_strerror(int err)
{
    switch (err) {
    case PANCH_OK:              return "ok";
    case PANCH_ERR_ARG:         return "invalid argument";
    case PANCH_ERR_EPHEMERIS:   return "ephemeris calculation failed";
    case PANCH_ERR_CIRCUMPOLAR: return "sun does not rise or set";
    case PANCH_ERR_NO_CROSSING: return "boundary search precondition failed";
    case PANCH_ERR_CAPACITY:    return "span list capacity exceeded";
    default:                    return "unknown error";
    }
}
