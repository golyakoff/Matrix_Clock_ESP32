# Matrix Clock ESP32 (Tetris Clock)

Firmware for a desk clock built around an ESP32 and an RGB LED matrix panel.
Digits fall into place and lock in with a small "Tetris block" animation on
every time change. The device is controlled from a companion Android app
([Matrix_Clock_Android](https://github.com/golyakoff/Matrix_Clock_Android))
over Bluetooth Low Energy (BLE) — no cloud. Wi-Fi is optional (off by default): with it the
clock syncs its time from the internet and can update its firmware by itself.

## Hardware

- ESP32 (DOIT ESP32 DEVKIT V1)
- RGB LED matrix panel driven via [PxMatrix](https://github.com/2dom/PxMatrix)
- DS3231 RTC chip for precise timekeeping, independent of the ESP32
- Capacitive touch pad to toggle the display on/off
- LDR (light-dependent resistor) input, currently unwired — brightness is
  driven by the hourly schedule instead (see below)

Schematics and datasheets for the hardware are in [docs/](docs/).

## What it does

- Keeps time on the DS3231 RTC and renders it on the matrix with a
  Tetris-style falling digit animation ([components/TetrisAnimation](components/TetrisAnimation))
- Syncs time, and is fully controlled, from the Android app over BLE
  (single GATT service, see `MC_SERVICE_UUID` in [main/include/ble.h](main/include/ble.h))
- Two RTC-backed alarms (`on`/`off`) to automatically show/hide the display,
  e.g. to turn the clock off at night
- Manual or automatic brightness control; automatic mode follows a 24-entry
  hourly brightness table stored in flash (see [main/include/brightness_schedule.h](main/include/brightness_schedule.h))
- Touch pad for manual on/off, independent of the alarms
- RTC temperature and aging-offset read/write over BLE
- Optional Wi-Fi (client only, set up from the app, which picks the network from the ones the
  clock hears): every 1..24 hours the clock connects, syncs time by SNTP into the RTC (local time
  from the phone's time zone, daylight saving time included), checks GitHub for a newer firmware
  and disconnects ([main/wifi.cpp](main/wifi.cpp), [main/updater.cpp](main/updater.cpp))
- Firmware updates (OTA) pushed from the phone app over BLE, or downloaded by the clock itself
  over Wi-Fi when the user asks for it in the app (never automatically), using the
  ESP32's A/B (`app0`/`app1`) partition scheme so a bad update can't brick
  the device

## Project layout

The firmware is an [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) 5.5 project with the
[Arduino core](https://github.com/espressif/arduino-esp32) 3.3 as an ESP-IDF component: the clock
code keeps Arduino's `setup()`/`loop()` and Arduino display libraries, while the build, `sdkconfig`
and the low-level APIs are ESP-IDF's.

```
main/               Application source (one .cpp/.h pair per module, headers in main/include):
  main                setup()/loop(), wires all modules together
  ble                 BLE GATT service, characteristics and callbacks (NimBLE, esp-nimble-cpp)
  matrix              LED matrix driving + Tetris digit animation glue
  rtc                 DS3231 driver wrapper, settings packed into RTC alarm registers
  alarm               On/off alarm scheduling logic
  brightness_schedule NVS-backed 24-hour brightness table
  ota                 OTA firmware writer (BLE upload and the update from the internet)
  net_settings        NVS-backed Wi-Fi settings and time zone
  wifi                Wi-Fi client: periodic sessions, SNTP time sync, network scan
  updater             Firmware update from GitHub releases (manifest.json)
  touch_sensor        Capacitive touch input
  time_helper         Small time/struct tm utilities
  idf_component.yml   Managed components: arduino-esp32, esp-nimble-cpp (exact versions)

components/         Vendored Arduino libraries as ESP-IDF components (upstream commit and the
                    local changes are noted in each CMakeLists.txt)
  TetrisAnimation      Falling-digit rendering (github.com/golyakoff/TetrisAnimation)
  PxMatrix             LED matrix driver
  ErriezDS3231         DS3231 RTC driver
  Adafruit_GFX, Adafruit_BusIO  graphics primitives they build on

sdkconfig.defaults  Common configuration
variants/           Per build variant configuration on top of it (flash size, partition
                    table, optimization, log level)
version.txt         Firmware version, e.g. v2.0.0
partitions/         Flash partition tables (4MB and 16MB boards)
docs/               Datasheets and schematics (PDFs)
tools/              build.sh (build a variant), make_release.sh (release assets),
                    gen_animation.py (Animation Splash frames from GIFs)
.github/workflows/  CI: builds all variants and publishes firmware .bin
                    releases when a `vX.Y.Z` tag is pushed
```

## Building and flashing

Install [ESP-IDF v5.5.6](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32/get-started/)
(or use the VS Code ESP-IDF extension). The managed components are downloaded on the first build.

```bash
. ~/esp/esp-idf-v5.5.6/export.sh         # once per shell
tools/build.sh release_4mb               # build a variant into build/<variant>
tools/build.sh release_4mb flash monitor # build, flash over USB and show the serial log (115200)
tools/build.sh debug_4mb -p COM4 flash   # any idf.py arguments after the variant
```

Variants (see [variants/](variants)):
- `release_4mb` — optimized release build for 4MB flash boards
- `release_16mb` — optimized release build for 16MB flash boards
- `debug_4mb` — verbose serial logging, 4MB boards; for local runs, not published in releases

A plain `idf.py build` (what the VS Code extension runs) builds for a 4MB board with the common
defaults. Pick the variant matching your board's flash size — see
`partitions/4MB.csv` / `partitions/16MB.csv`.

The bootloader rolls back to the previous firmware if a new one does not run for 30 seconds
(crash or reboot loop) — see `main_firmware_confirm()` in [main/main.cpp](main/main.cpp).

## Flashing a prebuilt release

No build environment needed: each [GitHub release](https://github.com/golyakoff/Matrix_Clock_ESP32/releases)
has a zip per board variant with the four flash images and a ready-to-run
`flash_<variant>.bat`. On Windows:

```bat
python -m pip install --upgrade esptool
flash_release_16mb.bat COM4
```

The release page has the step-by-step guide (Python, esptool, USB driver),
Linux/macOS commands and a single merged image for browser-based flashers.
Release assets and the release page text are generated by
[tools/make_release.sh](tools/make_release.sh).

Flash map (both partition tables share it):

| Address   | Image                                   |
|-----------|-----------------------------------------|
| `0x1000`  | bootloader (`*_bootloader.bin`)         |
| `0x8000`  | partition table (`*_pt.bin`)            |
| `0xe000`  | otadata, boot from app0 (`*_boot_app0.bin`) |
| `0x10000` | application (`*_fw.bin`)                |

## Updating firmware over BLE

Once the device is running, further updates don't need a USB cable: bump
[version.txt](version.txt), build, and push the new
`build/<variant>/tetris-clock.bin` from the Android app — it compares
versions over BLE and offers the update if a newer one is available. Tagging
a commit `vX.Y.Z` also triggers CI to build and publish firmware binaries
for all three environments as a GitHub release.

## Related

- Android companion app: https://github.com/golyakoff/Matrix_Clock_Android
