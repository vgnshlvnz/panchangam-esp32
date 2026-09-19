# panchangam-esp32

A C port of a Python panchangam (Hindu almanac) calculator, built to run
standalone on an ESP32-S3 (N16R8). The firmware computes the day's panchangam
and posts it to a Slack Incoming Webhook every morning at 05:30 MYT.

For each civil day and place it computes:

- sunrise and sunset
- **vara** (weekday, sunrise to sunrise)
- **tithi**, **nakshatra**, **yoga**, **karana**, each with its index and exact start and end times
- **rahu kalam**, **yamagandam** and **gulika**

The Tamil month is not computed, because the Python reference does not compute it.

## How it fits together

| Path | What it is |
|---|---|
| `core/panchangam.h` | The C API contract |
| `core/panchangam.c` | Implementation. Pure C99, no ESP-IDF includes, builds on the host |
| `CONVENTIONS.md` | The astronomical conventions (ayanamsa, sunrise definition, flags), each cited to a Python line |
| `test/SCHEMA.md` | Format of the reference test data |
| `test/` | Reference generator, validator, smoke test, compare harness, `reference.json` |
| `firmware/` | ESP-IDF v5.5 project: Wi-Fi, SNTP, Slack post |
| `third_party/swisseph` | Swiss Ephemeris (git submodule) |
| `docs/wiki/` | Usage guides (see below) |

Fixed choices: Swiss Ephemeris in **Moshier mode only** (no `.se1` data files),
**Lahiri** ayanamsa, sunrise as the **upper limb with refraction**. Times are
Julian Days in UT (`double`). The full list is in [CONVENTIONS.md](CONVENTIONS.md).

## Quick start (host)

```sh
git clone --recurse-submodules https://github.com/vgnshlvnz/panchangam-esp32.git
cd panchangam-esp32
make test                          # C port vs test/reference.json
make -C test smoke && ./test/smoke # Swiss Ephemeris vs Python, 3 dates
```

`make test` compares the C port with 750 reference cases from the Python
implementation. All angas and timings must agree within 2 seconds. See the
[Host build and tests](docs/wiki/Host-build-and-tests.md) page.

## Using the C API

```c
panch_query_t q = {
    .year = 2026, .month = 9, .day = 6,
    .day_start_jd_ut      = /* JD (UT) of local midnight that starts the date */,
    .next_day_start_jd_ut = /* JD (UT) of local midnight that starts the next date */,
    .place = { .latitude_deg = 3.1073, .longitude_deg = 101.6067, .elevation_m = 0.0 },
};
static panch_day_t day;
int rc = panch_compute_day(&q, &day);
if (rc != PANCH_OK) { /* panch_strerror(rc) */ }
```

The core has no time zone database, so the caller supplies the two local
midnights. Details and a complete example are in
[C API usage](docs/wiki/C-API-usage.md).

## Firmware

Configure with `idf.py menuconfig` under **Panchangam Configuration**, then build
and flash. See [Firmware](docs/wiki/Firmware.md). Nothing should be flashed until
the host tests pass.

## Documentation

- [Home](docs/wiki/Home.md)
- [Host build and tests](docs/wiki/Host-build-and-tests.md)
- [C API usage](docs/wiki/C-API-usage.md)
- [Firmware](docs/wiki/Firmware.md)
- [Regenerating the reference data](docs/wiki/Regenerating-reference-data.md)
- [Conventions](CONVENTIONS.md), [test schema](test/SCHEMA.md)

`docs/wiki/` is written so the pages can be copied unchanged into the GitHub
wiki repository (`panchangam-esp32.wiki.git`).

## Constraints

- All `swe_*` calls must stay in one thread or task; Swiss Ephemeris is not thread-safe.
- Use `double`, never `float`, for times and longitudes.
- `core/` stays pure C99 with no ESP-IDF includes.

## Licensing

Swiss Ephemeris is distributed by Astrodienst under the AGPL-3.0 or a
commercial licence (see `third_party/swisseph/LICENSE`). This project has no
licence file of its own yet; choose one with that in mind before distributing.
