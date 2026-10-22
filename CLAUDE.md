# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware of the Tetris Clock: an ESP32 (DOIT DEVKIT V1) driving a 64x32 RGB LED matrix with a DS3231
RTC, controlled over BLE from the Android app (https://github.com/golyakoff/Matrix_Clock_Android,
checked out next to this repo as `../Matrix_Clock_Android`). Optional WiFi syncs time and downloads
firmware updates. User-facing overview: README.md.

## Build

ESP-IDF 5.5.6 project with the Arduino core 3.3.12 as a component (setup()/loop() via
CONFIG_AUTOSTART_ARDUINO). No PlatformIO.

```bash
. ~/esp/esp-idf-v5.5.6/export.sh
tools/build.sh release_4mb            # build/<variant>; variants/*.defaults over sdkconfig.defaults
tools/build.sh release_4mb size       # any idf.py arguments after the variant
tools/make_release.sh vX.Y.Z          # release assets + manifest.json, after building release_4mb and release_16mb
```

The Linux dev box has ~4 GB RAM: run builds in a memory-capped scope
(`systemd-run --user --scope -q -p MemoryMax=1900M tools/build.sh ...`) and never at the same time as
a Gradle build. A clean build takes a few minutes (the Arduino component compiles all its dependencies
— Zigbee, RainMaker, Modbus... — though CONFIG_ARDUINO_SELECTIVE_* keeps them out of the firmware).

The version is `version.txt` (`vX.Y.Z`, PROJECT_VER, read back with esp_app_get_description()); the
app compares it with the GitHub release tags. Bump it together with a new section of CHANGELOG.md;
CI refuses a tag that does not match it.

## Flash budget

The 4MB board's app slot is 1.875 MB (partitions/4MB.csv, two OTA slots). Release builds are
optimized for size (the display code — main, TetrisAnimation, Adafruit_GFX — keeps -O2 for the
refresh ISR timing), and sdkconfig.defaults drops what a WiFi client talking HTTPS to GitHub does not
need. Check `tools/build.sh release_4mb size` after adding anything; the animations (~566 KB) are the
only big thing that can still give way (`tools/gen_animation.py --min-flash-mb 16` gates one to the
16MB build). IRAM is ~80% used: WiFi/lwIP code is kept out of IRAM.

## Layout and conventions

- `main/` — one .cpp/.h pair per module, headers in `main/include`. `main.cpp` wires everything:
  callbacks are passed into the modules (ble.cpp knows no other module).
- `components/` — vendored Arduino libraries with their own CMakeLists; local changes are marked
  "Matrix Clock" and listed in each CMakeLists.txt.
- BLE (esp-nimble-cpp): one service, one characteristic per value; the byte layouts are documented
  next to the UUIDs in `main/include/ble.h` and must stay in sync with the app
  (`TetrisClockBleManager.kt`, `protocol/NetworkCodec.kt` + its unit tests). Never change an
  existing characteristic incompatibly: add a new one (optional for the app).
- Flash writes (NVS, OTA, otadata) go through `matrix_pause_timers()`/`matrix_resume_timers()` from
  the main loop; settings changed over BLE are kept in RAM and flushed by `loop()`.
- The BLE stack can't be called from an ISR (touch pad, RTC 1 Hz): ISRs set flags, `loop()` acts.
- WiFi (`wifi.cpp`, `updater.cpp`) is ported from FireLamp_ESP32 and runs in its own task; time from
  SNTP reaches the RTC through `loop()` (`main_net_time_apply()`).
- An update from the internet is installed only on request from the app (Command 0x03), never
  automatically. A new firmware is confirmed after 30 s of running (bootloader rollback).

## Hardware testing

There is no board attached to the dev box. Changes to the display timing, the touch pad, BLE or OTA
need a test on the clock by the user before a release: build test images with `make_release.sh`
into the scratchpad and send them.
