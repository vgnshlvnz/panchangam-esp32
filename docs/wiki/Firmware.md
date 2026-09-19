# Firmware

An ESP-IDF v5.5 project in `firmware/` for an ESP32-S3 N16R8 (16 MB flash,
8 MB octal PSRAM). It joins Wi-Fi, syncs the clock over SNTP, computes the
panchangam for the local date at a fixed time each day, and posts it to a Slack
Incoming Webhook. It never posts before SNTP has synced.

The panchangam calculation is `core/panchangam.c`, the same code the host tests
exercise. Run the [host tests](Host-build-and-tests) first; nothing should be
flashed until they pass.

## Prerequisites

- ESP-IDF v5.5 installed and exported (`. $IDF_PATH/export.sh`)
- A Slack Incoming Webhook URL
- The submodule checked out: `git submodule update --init`

## Configure

```sh
cd firmware
idf.py set-target esp32s3
idf.py menuconfig
```

Open **Panchangam Configuration**:

| Option | Default | Notes |
|---|---|---|
| Wi-Fi SSID / password | empty | |
| Slack Incoming Webhook URL | empty | A secret. `sdkconfig` is gitignored |
| Daily post hour / minute (MYT) | 05:30 | |
| Post once right after time sync | off | Turn on to test connectivity |
| Latitude / longitude (millidegrees) | 3141 / 101686 | Kuala Lumpur. A firmware choice; the Python core has no default location |
| Elevation (m) | 56 | Changes sunrise by a few seconds |

Coordinates are integers in millidegrees, so 3.1073 is `3107` and 101.6067 is
`101607`.

Settings that need no secrets are in `sdkconfig.defaults` (16 MB flash, octal
PSRAM, certificate bundle for TLS).

## Build, flash, monitor

```sh
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Use your board's serial port. On boot the log shows the Wi-Fi connection,
`time synced`, then `next post in N s`.

## How it behaves

- The timezone is fixed to MYT (`MYT-8`, UTC+8, no daylight saving).
- One task (`calc`, 32 KB stack) does all Swiss Ephemeris work, because the
  library is not thread-safe.
- The wait loop sleeps in chunks of at most 60 s so a clock correction from
  SNTP is picked up.
- The post is a JSON body with `text` containing vara, sunrise/sunset, each
  anga's index and times, and the three inauspicious periods.

## Testing without waiting until morning

Enable **Post once right after time sync** in menuconfig, rebuild, flash, and
watch the monitor and your Slack channel. Turn it off again afterwards.

## Troubleshooting

- **Nothing posts:** the log will say `waiting for SNTP sync; not posting` if
  the clock is not set. Check Wi-Fi credentials and that `pool.ntp.org` is reachable.
- **`Slack HTTP` not 200:** check the webhook URL.
- **Wrong sunrise:** check latitude, longitude and elevation. Millidegrees are easy to get wrong.
