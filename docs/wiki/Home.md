# panchangam-esp32 wiki

A C port of a Python panchangam calculator, running on an ESP32-S3 and posting
the daily panchangam to Slack.

## Pages

- [Host build and tests](Host-build-and-tests) : build the library and check it against the Python reference
- [C API usage](C-API-usage) : call the core from your own code
- [Firmware](Firmware) : configure, build and flash the ESP32-S3
- [Regenerating the reference data](Regenerating-reference-data) : rebuild `reference.json` from Python

## Background

| Term | Meaning |
|---|---|
| Panchangam | The five limbs of a day: tithi, vara, nakshatra, yoga, karana |
| Tithi | Lunar day: the Moon-minus-Sun angle in 12 degree steps (30 per lunation) |
| Nakshatra | One of 27 divisions of the Moon's sidereal longitude |
| Yoga | One of 27 divisions of Sun plus Moon longitude |
| Karana | Half a tithi (60 per lunation) |
| Vara | Weekday, reckoned from sunrise to sunrise |
| Rahu kalam, yamagandam, gulika | Inauspicious eighths of the daylight span, chosen by weekday |

The day runs from sunrise to the next sunrise. An anga (tithi, nakshatra, yoga
or karana) may change during that window, so each is returned as a list of
spans. The first span usually starts before sunrise.

The exact conventions (ayanamsa, sunrise definition, Swiss Ephemeris flags,
time zone handling) are in the repository's `CONVENTIONS.md`.
