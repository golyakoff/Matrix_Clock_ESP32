# Matrix Clock ESP32 (Tetris Clock)

Firmware for a desk clock built around an ESP32 and an RGB LED matrix panel.
Digits fall into place and lock in with a small "Tetris block" animation on
every time change. The device is controlled from a companion Android app
([Matrix_Clock_Android](https://github.com/golyakoff/Matrix_Clock_Android))
over Bluetooth Low Energy (BLE) — no cloud, no Wi-Fi required for normal use.

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
  Tetris-style falling digit animation ([lib/TetrisAnimation](lib/TetrisAnimation))
- Syncs time, and is fully controlled, from the Android app over BLE
  (single GATT service, see `MC_SERVICE_UUID` in [include/ble.h](include/ble.h))
- Two RTC-backed alarms (`on`/`off`) to automatically show/hide the display,
  e.g. to turn the clock off at night
- Manual or automatic brightness control; automatic mode follows a 24-entry
  hourly brightness table stored in flash (see [include/brightness_schedule.h](include/brightness_schedule.h))
- Touch pad for manual on/off, independent of the alarms
- RTC temperature and aging-offset read/write over BLE
- Firmware updates (OTA) pushed from the phone app over BLE, using the
  ESP32's A/B (`app0`/`app1`) partition scheme so a bad update can't brick
  the device

## Project layout

```
src/, include/      Application source (one .cpp/.h pair per module):
  main              setup()/loop(), wires all modules together
  ble                 BLE GATT service, characteristics and callbacks
  matrix              LED matrix driving + Tetris digit animation glue
  rtc                 DS3231 driver wrapper, settings packed into RTC alarm registers
  alarm               On/off alarm scheduling logic
  brightness_schedule NVS-backed 24-hour brightness table
  ota                 BLE-triggered OTA firmware update
  touch_sensor        Capacitive touch input
  time_helper         Small time/struct tm utilities

lib/                Vendored/third-party libraries pulled in via platformio.ini
  TetrisAnimation      Falling-digit rendering (github.com/golyakoff/TetrisAnimation)
  PxMatrix LED MATRIX library   LED matrix driver
  ErriezDS3231          DS3231 RTC driver
  Adafruit GFX Library, Adafruit BusIO, ErriezSerialTerminal  supporting libs

partitions/         Flash partition tables (4MB and 16MB boards)
docs/               Datasheets and schematics (PDFs)
test/               PlatformIO unit test placeholder
.github/workflows/  CI: builds all environments and publishes firmware .bin
                    releases when a `vX.Y.Z` tag is pushed
```

## Building and flashing

The project uses [PlatformIO](https://platformio.org/). With the PlatformIO
CLI available:

```bash
pio run                      # build the default env (esp32_4mb_debug)
pio run -e esp32_4mb         # release build, 4MB flash boards
pio run -e esp32_16mb        # release build, 16MB flash boards
pio run -t upload            # build and flash over USB (set upload_port in platformio.ini)
pio device monitor           # serial log at 115200 baud
```

Environments (see [platformio.ini](platformio.ini)):
- `esp32_4mb_debug` — default, debug build with verbose logging
- `esp32_4mb` — optimized release build for 4MB flash boards
- `esp32_16mb` — optimized release build for 16MB flash boards

Pick the partition table matching your board's flash size — see
`partitions/4MB.csv` / `partitions/16MB.csv`.

## Updating firmware over BLE

Once the device is running, further updates don't need a USB cable: bump
`MATRIX_CLOCK_FIRMWARE_VERSION` in [include/version.h](include/version.h),
build, and push the new `firmware.bin` from the Android app — it compares
versions over BLE and offers the update if a newer one is available. Tagging
a commit `vX.Y.Z` also triggers CI to build and publish firmware binaries
for all three environments as a GitHub release.

## Related

- Android companion app: https://github.com/golyakoff/Matrix_Clock_Android
