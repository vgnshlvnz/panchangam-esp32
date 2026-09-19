# Reference test-case schema

One JSON file per (place, date range), generated **from the Python reference**
(`~/projects/panchangam_mcp`, conventions in `../CONVENTIONS.md`). The file is a
single object; `cases` holds **one object per date**. Every field of
`core/panchangam.h`'s `panch_day_t` appears in each case.

Instants are given twice: `jd_ut` (what the C API returns, compared numerically)
and `iso` (human-readable, local zone, second precision). If they disagree,
`jd_ut` wins. `jd_ut` must be produced with `swe.julday` semantics (Gregorian, UT),
from the *unrounded* Python datetime, not the ISO string.

## Top level

```json
{
  "meta": {
    "generator": "panchangam_mcp @ <git sha>",
    "swisseph_version": "<recorded by generator>",
    "ayanamsa": "lahiri",
    "ephemeris": "moshier",
    "sunrise_definition": "upper_limb_refraction",
    "place": {
      "name": "Kuala Lumpur",
      "latitude_deg": 3.14111,
      "longitude_deg": 101.68639,
      "elevation_m": 56.0,
      "timezone": "Asia/Kuala_Lumpur"
    },
    "tolerance": {
      "sun_event_seconds": 2.0,
      "anga_boundary_seconds": 2.0,
      "muhurta_seconds": 2.0
    }
  },
  "cases": [ /* one Case per date, ascending */ ]
}
```

`meta.tolerance` is the pass threshold in **seconds** (`|c_jd - ref_jd| * 86400`).
Rationale: Python bisects to 1 s and returns the midpoint (±0.5 s) and both sides
use Moshier; 2 s leaves margin without hiding a wrong convention (a disc-centre
vs. upper-limb mistake is ~1 min at the equator; a refraction mistake ~ 1–2 min).

## Case

```json
{
  "date": "2026-09-06",
  "input": {
    "year": 2026, "month": 9, "day": 6,
    "day_start_jd_ut": 2461289.6666667,
    "next_day_start_jd_ut": 2461290.6666667
  },
  "sunrise":      { "jd_ut": 2461289.7, "iso": "2026-09-06T07:06:49+08:00" },
  "sunset":       { "jd_ut": 2461290.0, "iso": "2026-09-06T19:11:02+08:00" },
  "next_sunrise": { "jd_ut": 2461290.7, "iso": "2026-09-07T07:06:41+08:00" },

  "vara": { "index": 1, "name": "Ravivara",
            "start": { "jd_ut": 0.0, "iso": "" }, "end": { "jd_ut": 0.0, "iso": "" } },

  "tithi":     [ Span, ... ],
  "nakshatra": [ Span, ... ],
  "yoga":      [ Span, ... ],
  "karana":    [ Span, ... ],

  "rahu_kalam": Period,
  "yamagandam": Period,
  "gulika":     Period
}
```

(The numeric values above are placeholders showing shape, not real data.)

- `input` mirrors `panch_query_t` (place comes from `meta.place`). `day_start_jd_ut` /
  `next_day_start_jd_ut` are local midnights in `meta.place.timezone`, computed by
  the generator — the C test harness must not need a tz database.
- `Span` = `{ "index": int, "name": string, "start": Instant, "end": Instant }`
  with `Instant` = `{ "jd_ut": double, "iso": string }`.
  `index` ranges: tithi 1..30, nakshatra 1..27, yoga 1..27, karana 1..60.
  `name` is informational (Python's name tables); C tests compare `index` only.
- `Period` = `{ "start": Instant, "end": Instant }`. `auspicious` is omitted (always false).
- Span order is chronological; `spans[i].end == spans[i+1].start`. Array lengths
  are `count` in the C lists and must match exactly.
- Field ↔ C mapping: `sunrise`→`sunrise_jd_ut`, `sunset`→`sunset_jd_ut`,
  `next_sunrise`→`next_sunrise_jd_ut`, `vara`→`vara`, `tithi`→`tithi`,
  `nakshatra`→`nakshatra`, `yoga`→`yoga`, `karana`→`karana`,
  `rahu_kalam`→`rahu_kalam`, `yamagandam`→`yamagandam`, `gulika`→`gulika`.

## Error cases

A case may instead carry `"expect_error": "PANCH_ERR_CIRCUMPOLAR"` (name from
`panch_err_t`) in place of all result fields, e.g. a high-latitude date. `input`
is still present.

## Pass criteria

1. `panch_compute_day` returns 0 (or the named error).
2. All `index` values and list `count`s equal the reference exactly.
3. Every instant is within the tolerance above.
4. Invariants hold independently of the reference: spans contiguous, first span
   starts ≤ sunrise, last span ends ≥ next sunrise, rahu/yamagandam/gulika are
   1/8 of daylight each.

Tamil month is intentionally absent: the Python core does not compute it
(`../CONVENTIONS.md`, "Tamil month").
