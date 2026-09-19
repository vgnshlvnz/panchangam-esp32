# Conventions

Extracted from the Python reference at `~/projects/panchangam_mcp` (read-only).
Paths are relative to that repo. Every rule cites the Python line it comes from;
a C implementation must reproduce these choices exactly or the reference tests
will not match.

## Ayanamsa

**Lahiri** (`SE_SIDM_LAHIRI`), default; Raman is selectable in Python but the C core uses Lahiri only.

- `src/panchangam/ephemeris.py:48` — `LAHIRI = swe.SIDM_LAHIRI`
- `src/panchangam/ephemeris.py:52` — `DEFAULT_AYANAMSA = Ayanamsa.LAHIRI`
- `src/panchangam/ephemeris.py:68` — `swe.set_sid_mode(ayanamsa.value, 0.0, 0.0)`
  (C: `swe_set_sid_mode(SE_SIDM_LAHIRI, 0.0, 0.0)`; t0 and ayan_t0 unused for a predefined mode)
- Configured lazily once per process: `ephemeris.py:72-74` (`_ensure_configured`).

## Ephemeris and swe flags

**Moshier**, no ephemeris files. Sidereal longitudes with speed. Apparent, geocentric, true-of-date defaults (no `SEFLG_TRUEPOS`, `NOABERR`, `NOGDEFL`, `J2000`, or topocentric).

- `ephemeris.py:42` — `_EPHE_FLAG = swe.FLG_MOSEPH`
- `ephemeris.py:101` — `_SIDEREAL_FLAG = _EPHE_FLAG | swe.FLG_SIDEREAL | swe.FLG_SPEED`
  (C: `SEFLG_MOSEPH | SEFLG_SIDEREAL | SEFLG_SPEED`)
- `ephemeris.py:129` — `swe.calc_ut(_julian_day_ut(when), body, _SIDEREAL_FLAG)`; longitude is `values[0] % 360.0` (`ephemeris.py:134`)
- Sun = `swe.SUN`, Moon = `swe.MOON` (`ephemeris.py:115-116`). Bodies are UT-based (`calc_ut`), so no delta-T handling is done by the caller.
- `swe.julday(..., swe.GREG_CAL)`: Gregorian calendar, UT (`ephemeris.py:85`).

## Sunrise / sunset definition

**Upper limb, with standard atmospheric refraction, with elevation dip.** *Not* disc-centre, *not* no-refraction.

- `ephemeris.py:241-246` (docstring): "the instant the Sun's **upper limb** reaches the horizon, **with** standard atmospheric refraction ... not the Hindu disc-centre / no-refraction variant ... horizon dip from elevation is included."
- `ephemeris.py:225-227` — the exact call:
  `swe.rise_trans(_julian_day_ut(day_start), swe.SUN, kind, geopos, 0.0, 0.0, _EPHE_FLAG)`
  - `kind` is bare `swe.CALC_RISE` / `swe.CALC_SET` (`ephemeris.py:248`, `258`): **no** `BIT_DISC_CENTER`, **no** `BIT_NO_REFRACTION`, **no** `BIT_HINDU_RISING`.
  - `atpress = 0.0`, `attemp = 0.0` (`ephemeris.py:226`): with `atpress == 0` Swiss Ephemeris estimates pressure from `geopos` altitude and uses its default temperature.
  - flags = `_EPHE_FLAG` only (Moshier, **not** sidereal).
  - C: `swe_rise_trans(tjd_ut, SE_SUN, NULL, SEFLG_MOSEPH, SE_CALC_RISE, geopos, 0.0, 0.0, &tret, serr)` — note the C signature differs in argument order from pyswisseph.
- `geopos = (longitude, latitude, elevation_m)` — **lon first** (`ephemeris.py:224`).
- Search start is **local midnight** of the requested date; the first event after it is used: `ephemeris.py:223` (`day_start = datetime(on.year, on.month, on.day, tzinfo=tz)`) and `ephemeris.py:226` (`_julian_day_ut(day_start)` is the `tjdut` argument).
- No rise/set (return status != 0) → `CircumpolarError`: `ephemeris.py:228-232`. C: `PANCH_ERR_CIRCUMPOLAR`.

## Time zone handling

- The reference takes an **IANA zone name**; a UTC offset is rejected: `src/panchangam/server.py:160-171` (`ZoneInfo(tz)` must resolve) and `src/panchangam/types.py:19-23` (`timezone: IANA name`).
- All datetimes crossing the ephemeris boundary must be tz-aware, else `NaiveDatetimeError`: `ephemeris.py:79-82`.
- Conversion to JD goes via UTC: `ephemeris.py:83` (`when.astimezone(timezone.utc)`) then `ephemeris.py:84-85`.
- Civil day boundaries are **local midnight in the place's zone**, DST-aware: `ephemeris.py:223`. The next day is `on + timedelta(days=1)` (calendar date +1, then local midnight again): `angas.py:233`.
- Output datetimes are converted back to the place zone: `ephemeris.py:233`; serialised to seconds with offset: `server.py:288-294`.
- **C consequence:** the core has no tz database. The caller supplies `day_start_jd_ut` and `next_day_start_jd_ut` (`core/panchangam.h`, `panch_query_t`). Everything inside the core is JD UT.
- Weekday (vara and the muhurta tables) is the **civil date's** weekday, not the weekday at the JD: `angas.py:216` and `muhurta.py:83`.

## Default location

**The Python core has no default location.** `lat`, `lon`, `tz` are required in the tool schema (`server.py:188` — `"required": ["date", "lat", "lon", "tz"]`) and `build_place` takes them positionally (`server.py:130-133`). Only the elevation has a default:

- `server.py:136` — `elevation_m: float = 0.0` (also `types.py:29`)
- `server.py:135` — `name: str = "query location"` (display only; `types.py:16-17`)

The **test/reference location** used by every fixture and the README example is Kuala Lumpur:

- `tests/test_server.py:223` — `Place("Kuala Lumpur", 3.14111, 101.68639, "Asia/Kuala_Lumpur", 56.0)`
- `README.md:121` — `{ "date": "2026-09-06", "lat": 3.14111, "lon": 101.68639, "tz": "Asia/Kuala_Lumpur" }` (no elevation → 0.0)
- `tests/fixtures/panchangam_kuala_lumpur_2026-09.json` `place`: lat 3.14111, lon 101.68639, tz `Asia/Kuala_Lumpur` (UTC+8, no DST), elevation_m **56.0**

Any firmware default should be labelled as a firmware choice, not a Python one. Elevation matters: 0.0 (via server) and 56.0 (fixtures) give sunrise differing by seconds. Fixtures use 56.0.

## Anga definitions

All from `src/panchangam/angas.py`. Angles are sidereal (Lahiri).

| Anga | Angle | Step | Count | Lines |
|---|---|---|---|---|
| tithi | elongation = (Moon − Sun) mod 360 (`ephemeris.py:197`) | 12° | 30 | `angas.py:35-36` |
| nakshatra | Moon longitude | 360/27 | 27 | `angas.py:41,43` |
| yoga | (Sun + Moon) mod 360 | 360/27 | 27 | `angas.py:42,44,131-139` |
| karana | elongation | 6° | 60 | `angas.py:47-48` |
| vara | civil weekday | — | 7 | `angas.py:216` |

- Indices are 1-based and `index = step_index % count + 1`: `angas.py:256`. Tithi 1 = Shukla Pratipada, 15 = Purnima, 16 = Krishna Pratipada, 30 = Amavasya. Karana index is 1..60 within the lunar month (not the 11-name index); names are derived from it in Python only (`angas.py:124`, `_karana_name`), so the C API returns the number.
- **Vara index:** `on.isoweekday() % 7 + 1` → Ravivara (Sunday) = 1 … Shanivara = 7 (`angas.py:216`). Start/end = consecutive sunrises (`angas.py:220-221`).
- **Reckoning window:** sunrise on the date → sunrise on the next date (`angas.py:232-233`). A span is included if it overlaps the window; the loop stops at the first span whose `end >= window_end` (`angas.py:265`). Spans carry their true start/end, so the first usually starts before sunrise.
- **Boundary finding:** bisection on the angle, bracket ±30 h (`angas.py:62`), tolerance **1 second** (`ephemeris.py:272`). Start of the span in progress at sunrise is searched in `[sunrise − 30h, sunrise]` (`angas.py:241-246`); each next boundary in `[start + 1 s, start + 30h]` (`angas.py:250-254`). `step_index = int(angle_at_sunrise // step)` (`angas.py:237`). A C port may use a faster root-finder but must agree with the 1-second bisection to within its tolerance (see `test/SCHEMA.md`).
- The bisection midpoint is returned (`ephemeris.py:325`, `lo + (hi - lo) / 2`) — up to ±0.5 s from the true root.

## Rahu kalam, yamagandam, gulika

All from `src/panchangam/muhurta.py`. Daylight = sunrise → sunset of the requested date, split into **8 equal parts** (`muhurta.py:38`, `72-80`). Part is 1-based, chosen by weekday, Sunday first (`muhurta.py:83`: `table[on.isoweekday() % 7]`). Period = `[bounds[part-1], bounds[part]]` (`muhurta.py:91`).

| Weekday | Sun | Mon | Tue | Wed | Thu | Fri | Sat |
|---|---|---|---|---|---|---|---|
| rahu kalam (`muhurta.py:34`) | 8 | 2 | 7 | 5 | 6 | 4 | 3 |
| yamagandam (`muhurta.py:35`) | 5 | 4 | 3 | 2 | 1 | 7 | 6 |
| gulika (`muhurta.py:36`) | 7 | 6 | 5 | 4 | 3 | 2 | 1 |

Equal partition uses `width = (end - start) / n` and pins the last boundary to `end` exactly (`muhurta.py:78-79`).

## Tamil month

**Not computed by the Python core.** A search of `src/` and `README.md` for `tamil`, `masa`, `sankranti` (as a computation), `rashi` finds only the word "Sankranti" in a tool description (`server.py:255`). There is no solar-month logic. Therefore `core/panchangam.h` has no Tamil month field and `test/SCHEMA.md` has no expected value for it. If it is wanted, it is new scope needing its own convention (e.g. sidereal Sun sign at sunrise vs. at sunset, or Sankranti-to-Sankranti) and a Python reference first.

## Also in Python, deliberately out of the C contract

Abhijit, durmuhurtam, choghadiya (`muhurta.py:110`, `126`, `181`), graha longitudes/retrograde, ayanamsa query, Raman ayanamsa. Add only if requested.

## Determinism notes

- `swe_calc_ut` / `swe_rise_trans` results must come from the same Swiss Ephemeris version as the pyswisseph used to generate fixtures; record the version in each fixture (`test/SCHEMA.md`, `meta.swisseph_version`).
- Moshier accuracy: seconds of arc for Sun/Moon (`ephemeris.py:9-12`); tolerances in `test/SCHEMA.md` are set from that.
