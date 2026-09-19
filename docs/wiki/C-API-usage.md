# C API usage

The API is declared in `core/panchangam.h`. Link `core/panchangam.c` with the
Swiss Ephemeris library. The core is pure C99; the only include beyond the
standard library is `swephexp.h`.

## Conventions

- Every instant is a **Julian Day in UT** (`double`), as `swe_julday()` gives.
- Every function returns an `int` error code. `PANCH_OK` (0) means success.
- No allocation: results are written to caller-provided structs.
- Sidereal zodiac with the Lahiri ayanamsa; Moshier ephemeris.
- Not thread-safe. Keep all calls in one thread or task.

## Time zones

The core has no time zone database. The caller passes the JD (UT) of local
midnight at the start of the date and at the start of the following date. Two
values are passed rather than "plus one day" so that days with a UTC offset
change stay correct.

Weekday-based results (vara, rahu kalam and so on) use the civil `year`, `month`
and `day` you pass, not the weekday of a JD.

## Example

```c
#include <stdio.h>
#include "panchangam.h"

int main(void)
{
    /* Local midnight 2026-09-06 in UTC+8 is 2026-09-05 16:00 UT. */
    double jd_start = 2461289.1666666665;

    panch_query_t q = {
        .year = 2026, .month = 9, .day = 6,
        .day_start_jd_ut      = jd_start,
        .next_day_start_jd_ut = jd_start + 1.0,
        .place = { .latitude_deg = 3.14111, .longitude_deg = 101.68639,
                   .elevation_m = 56.0 },
    };

    static panch_day_t d;
    int rc = panch_compute_day(&q, &d);
    if (rc != PANCH_OK) {
        fprintf(stderr, "error: %s\n", panch_strerror(rc));
        return 1;
    }

    printf("vara %d, sunrise JD %.6f\n", d.vara.index, d.sunrise_jd_ut);
    for (int i = 0; i < d.tithi.count; i++)
        printf("tithi %d: %.6f .. %.6f\n", d.tithi.spans[i].index,
               d.tithi.spans[i].start_jd_ut, d.tithi.spans[i].end_jd_ut);
    return 0;
}
```

Compile it the same way the `Makefile` builds `compare` (see the root `Makefile`
for the exact Swiss Ephemeris source list), adding `-Icore`.

## Results

`panch_compute_day` fills a `panch_day_t`:

| Field | Meaning |
|---|---|
| `sunrise_jd_ut`, `sunset_jd_ut`, `next_sunrise_jd_ut` | Solar events; the day is sunrise to next sunrise |
| `vara` | `index` 1 to 7, Ravivara (Sunday) = 1; start and end are the two sunrises |
| `tithi`, `nakshatra`, `yoga`, `karana` | Lists of spans (`count` and `spans[]`) overlapping the day |
| `rahu_kalam`, `yamagandam`, `gulika` | `start_jd_ut` and `end_jd_ut` |

Span `index` values: tithi 1 to 30 (1 = Shukla Pratipada, 15 = Purnima,
16 = Krishna Pratipada, 30 = Amavasya), nakshatra 1 to 27 (1 = Ashwini),
yoga 1 to 27 (1 = Vishkambha), karana 1 to 60. The API returns numbers, not
names; name tables are in the Python reference and the firmware has the vara
names.

List capacities are `PANCH_MAX_TITHI`, `PANCH_MAX_NAKSHATRA`, `PANCH_MAX_YOGA`
(4 each) and `PANCH_MAX_KARANA` (8).

Individual pieces are also available: `panch_sunrise`, `panch_sunset`,
`panch_vara`, `panch_tithi`, `panch_nakshatra`, `panch_yoga`, `panch_karana`,
`panch_rahu_kalam`, `panch_yamagandam`, `panch_gulika`.

## Error codes

| Code | Meaning |
|---|---|
| `PANCH_OK` | Success |
| `PANCH_ERR_ARG` | NULL pointer, latitude or longitude out of range, bad date, or the two midnights out of order |
| `PANCH_ERR_EPHEMERIS` | `swe_calc_ut` failed |
| `PANCH_ERR_CIRCUMPOLAR` | The Sun does not rise or set on that day (polar regions) |
| `PANCH_ERR_NO_CROSSING` | The boundary search found no crossing |
| `PANCH_ERR_CAPACITY` | A span list exceeded its maximum |

`panch_strerror(rc)` returns a description and never NULL.

## Not included

The Tamil month, abhijit, durmuhurtam and choghadiya are not in the API. The
Python reference has the last three but they were not part of the contract.
