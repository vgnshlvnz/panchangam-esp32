/*
 * panchangam.h -- C API contract for the panchangam calculation core.
 *
 * Mirrors the Python reference at ~/projects/panchangam_mcp
 * (src/panchangam/{ephemeris,angas,muhurta}.py). Semantics and conventions
 * are pinned in CONVENTIONS.md; JSON reference cases are in test/SCHEMA.md.
 *
 * Rules of this API:
 *   - Every instant is a Julian Day in UT (double), as swe_julday() produces.
 *     There are no time zones inside the core: the caller resolves the IANA
 *     zone and supplies the JD of local midnight (see panch_query_t).
 *   - Every real quantity is a double. Indices, counts, and the calendar
 *     date are int. Every function returns an int error code (0 == OK).
 *   - No heap allocation; all output is written to caller-provided structs.
 *   - Not computed by the Python core, therefore not in this API: Tamil
 *     month, abhijit, durmuhurtam, choghadiya, graha positions.
 */
#ifndef PANCHANGAM_H
#define PANCHANGAM_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- error codes -------------------------------------------------------- */

typedef enum {
    PANCH_OK                = 0,
    PANCH_ERR_ARG           = 1,  /* NULL pointer, lat/lon out of range, bad date,
                                     next_day_start_jd_ut <= day_start_jd_ut */
    PANCH_ERR_EPHEMERIS     = 2,  /* swe_calc_ut failed (Python: RuntimeError) */
    PANCH_ERR_CIRCUMPOLAR   = 3,  /* Sun does not rise or set (CircumpolarError) */
    PANCH_ERR_NO_CROSSING   = 4,  /* boundary search precondition failed
                                     (Python: find_crossing ValueError) */
    PANCH_ERR_CAPACITY      = 5   /* a span list exceeded its PANCH_MAX_* bound */
} panch_err_t;

/* ---- capacities (spans overlapping one sunrise-to-sunrise window) -------- */

#define PANCH_MAX_TITHI      4
#define PANCH_MAX_NAKSHATRA  4
#define PANCH_MAX_YOGA       4
#define PANCH_MAX_KARANA     8

/* ---- inputs ------------------------------------------------------------- */

/* Python: types.Place minus name/timezone. */
typedef struct {
    double latitude_deg;     /* north positive, [-90, 90]   */
    double longitude_deg;    /* east positive, [-180, 180]  */
    double elevation_m;      /* metres above sea level; Python default 0.0 */
} panch_place_t;

/*
 * One civil day at one place.
 *
 * The core has no tz database (ESP32 target), so the caller passes the JD (UT)
 * of local civil midnight at the start of the requested date AND at the start
 * of the following date. Passing both (rather than +1.0) keeps DST days exact,
 * matching Python's datetime(y,m,d,tzinfo=tz) at ephemeris.py:223.
 */
typedef struct {
    int    year;                    /* civil date being asked about, proleptic Gregorian */
    int    month;                   /* 1..12 */
    int    day;                     /* 1..31 */
    double day_start_jd_ut;         /* local 00:00 of (year,month,day), as JD UT */
    double next_day_start_jd_ut;    /* local 00:00 of the next civil date, as JD UT */
    panch_place_t place;
} panch_query_t;

/* ---- outputs ------------------------------------------------------------ */

/* One occurrence of a moving anga. Python: types.AngaSpan without the name. */
typedef struct {
    int    index;        /* 1-based: tithi 1..30, nakshatra 1..27, yoga 1..27, karana 1..60 */
    double start_jd_ut;  /* true astronomical start; usually before sunrise */
    double end_jd_ut;    /* true astronomical end; == next span's start     */
} panch_span_t;

typedef struct { int count; panch_span_t spans[PANCH_MAX_TITHI];     } panch_tithi_list_t;
typedef struct { int count; panch_span_t spans[PANCH_MAX_NAKSHATRA]; } panch_nakshatra_list_t;
typedef struct { int count; panch_span_t spans[PANCH_MAX_YOGA];      } panch_yoga_list_t;
typedef struct { int count; panch_span_t spans[PANCH_MAX_KARANA];    } panch_karana_list_t;

/* Python: types.NamedPeriod. Always start < end, auspicious is always false here. */
typedef struct {
    double start_jd_ut;
    double end_jd_ut;
} panch_period_t;

typedef struct {
    double sunrise_jd_ut;         /* sunrise on the requested date            */
    double sunset_jd_ut;          /* sunset on the requested date             */
    double next_sunrise_jd_ut;    /* sunrise on the following date (day end)  */

    /* Vara: sunrise-to-sunrise. index 1..7, Ravivara (Sunday) = 1.
       start == sunrise_jd_ut, end == next_sunrise_jd_ut. */
    panch_span_t vara;

    panch_tithi_list_t     tithi;
    panch_nakshatra_list_t nakshatra;
    panch_yoga_list_t      yoga;
    panch_karana_list_t    karana;

    panch_period_t rahu_kalam;
    panch_period_t yamagandam;    /* Python name: yamaganda */
    panch_period_t gulika;        /* Python name: gulika, label "Gulika Kalam" */
} panch_day_t;

/* ---- functions ---------------------------------------------------------- */

/* Compute everything above for one civil day. Configures the ephemeris
   (Lahiri, Moshier) on first use; see CONVENTIONS.md. Returns panch_err_t. */
int panch_compute_day(const panch_query_t *query, panch_day_t *out);

/* Individual pieces, same inputs and error codes. Each is what the matching
   Python function returns; panch_compute_day is their composition. */
int panch_sunrise(const panch_query_t *query, double *jd_ut_out);
int panch_sunset(const panch_query_t *query, double *jd_ut_out);
int panch_vara(const panch_query_t *query, panch_span_t *out);
int panch_tithi(const panch_query_t *query, panch_tithi_list_t *out);
int panch_nakshatra(const panch_query_t *query, panch_nakshatra_list_t *out);
int panch_yoga(const panch_query_t *query, panch_yoga_list_t *out);
int panch_karana(const panch_query_t *query, panch_karana_list_t *out);
int panch_rahu_kalam(const panch_query_t *query, panch_period_t *out);
int panch_yamagandam(const panch_query_t *query, panch_period_t *out);
int panch_gulika(const panch_query_t *query, panch_period_t *out);

/* Static description of an error code; never NULL. */
const char *panch_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif /* PANCHANGAM_H */
