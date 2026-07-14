Release 1.2.3

- [x] Fixed OTA transfers still dropping the BLE connection mid-way (up to ~87% on some units) even after v1.2.2: BLE characteristic writes are handled directly on the Bluedroid stack's own task, and each received chunk used to be written to flash synchronously right there - a slow flash write (a sector erase can take tens of ms) stalled the BLE stack's own processing along with it. Received chunks are now queued and written to flash from a dedicated task pinned to core 1 (BLE stays on core 0), so the BLE write callback just copies ~512 bytes into a queue and returns immediately instead of waiting on the flash write

**Full Changelog**: https://github.com/golyakoff/Matrix_Clock_ESP32/compare/v1.2.2...v1.2.3

Release 1.2.2

- [x] Fixed OTA updates being unreliable (BLE connection drops during the firmware transfer, sometimes as early as 30-70%) on some units: the display refresh timer now runs at half its normal rate while an OTA update is in progress, leaving more CPU headroom for the BLE stack. The OTA progress screen is a bit less smooth as a result, but a reliably completing update matters more than a perfectly fluid one

**Full Changelog**: https://github.com/golyakoff/Matrix_Clock_ESP32/compare/v1.2.1...v1.2.2

Release 1.2.1

- [x] Removed dead code and stale commented-out snippets (no behavior change): an orphaned `RealTimeClock::getBuildTime()` (only caller was commented out) and a stray unused `disableInterrupts()` forward declaration in `rtc.cpp`, a bogus `touch_100hz_loop_tick()` call in `main.cpp`'s `loop()` (no such function exists), and disabled critical-section/ADC-read snippets in `matrix.cpp`

**Full Changelog**: https://github.com/golyakoff/Matrix_Clock_ESP32/compare/v1.2.0...v1.2.1

Release 1.2.0

- [x] Added an "Updating firmware NN.N%" screen shown on the matrix during OTA updates, instead of a black screen: the matrix no longer fully unloads for the update, it stays powered and its display refresh timer keeps running (only the falling-digit animation is stopped), with the actual flash write briefly pausing that timer per chunk the same way the hourly brightness schedule already does. The shown percentage is scaled to match what the phone app displays (it also accounts for the download-to-phone and reboot/verify phases, not just the BLE transfer)
- [x] Added an "ERROR / Reboot in 10 sec" screen and automatic reboot if an OTA update is aborted or fails validation, instead of leaving the matrix stuck until the power cable is pulled
- [x] Fixed OTA updates silently failing to boot into the new firmware after reaching 100%: `Update.end()` and `esp_ota_set_boot_partition()` write to flash too, same as the per-chunk writes, and were missing the same display-timer pause, so they could race the still-running display refresh ISR

**Full Changelog**: https://github.com/golyakoff/Matrix_Clock_ESP32/compare/v1.1.2...v1.2.0

Release 1.1.2

- [x] Fixed the time characteristic no longer notifying every second: adding the hourly brightness schedule characteristic pushed the GATT table one handle over the budget passed to createService(), so the entries at the end of the table never got a handle

**Full Changelog**: https://github.com/golyakoff/Matrix_Clock_ESP32/compare/v1.1.1...v1.1.2

Release 1.1.1

- [x] Fixed the clock rebooting every time the hourly brightness schedule was written over BLE: the NVS write disabled the flash cache while the display timer ISR was still running code from flash. The schedule is now applied in RAM immediately and committed to NVS from the main loop with the display timers stopped.

**Full Changelog**: https://github.com/golyakoff/Matrix_Clock_ESP32/compare/v1.1.0...v1.1.1

Release 1.1.0

- [x] Added hourly brightness schedule (24-hour table), configurable over BLE, used as the auto-brightness source

**Full Changelog**: https://github.com/golyakoff/Matrix_Clock_ESP32/compare/v1.0.0...v1.1.0

Release 1.0.0

**Official public release!**

- [x] Tetris-style falling digit clock display, driven by an RTC (DS3231) for precise timekeeping
- [x] Full control from the Matrix Clock Android app over BLE: time sync, on/off, brightness
- [x] Two RTC-backed alarms to automatically show/hide the display (e.g. off at night)
- [x] Manual and automatic brightness control
- [x] Touch pad for manual display on/off
- [x] RTC temperature and aging-offset read/write over BLE
- [x] OTA firmware updates pushed from the phone app over BLE, using the ESP32 A/B partition scheme

**Full Changelog**: https://github.com/golyakoff/Matrix_Clock_ESP32/commits/v1.0.0
