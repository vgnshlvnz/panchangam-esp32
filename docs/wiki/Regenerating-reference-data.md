# Regenerating the reference data

`test/reference.json` is produced from the Python reference implementation
(`panchangam_mcp`) so the C port can be checked against it. Its format is
defined in `test/SCHEMA.md`.

## Requirements

- A checkout of the Python package (default `~/projects/panchangam_mcp`)
- Its virtualenv, which provides `pyswisseph` (the reference used 2.10.03, the
  same version as the `third_party/swisseph` submodule)

## Run

```sh
~/projects/panchangam_mcp/.venv/bin/python test/gen_reference.py
```

To use a checkout elsewhere:

```sh
PANCHANGAM_SRC=/path/to/panchangam_mcp/src /path/to/.venv/bin/python test/gen_reference.py
```

This writes:

- `test/reference.json`
- `test/smoke_expected.h` (Sun and Moon longitudes for the smoke test)

It takes about ten seconds.

## What it covers

- Petaling Jaya (3.1073, 101.6067, elevation 0.0, `Asia/Kuala_Lumpur`)
- Every day from 2026-01-01 to 2027-12-31
- 20 random dates from 1950 to 2050 (fixed seed, recorded in the file)

The script forces the Moshier ephemeris in the Python package (both the plain
and sidereal flag constants) before computing anything, and records the
generator's git revision and the Swiss Ephemeris version in `meta`.

## Then check it

```sh
python3 test/validate_reference.py   # structure and invariants
make test                            # C port against the new file
```

## Changing the place or dates

Edit `PLACE` and `pick_dates()` in `test/gen_reference.py`, and update the
list capacities there if you change to a region where more spans fit in a day
(the script asserts they fit the limits in `core/panchangam.h`). Regenerate
before changing tolerances.
