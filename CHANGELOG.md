Release 1.5.0

- [x] Reworked the day-boundary "Day Splash" easter egg (v1.4.0) into a configurable "Animation Splash": the chosen animation can now play at the start of the hour at a selectable cadence — once a day (at 00:00, the old behaviour), every 3 hours (00:00, 03:00, ... 21:00), or every hour — and for a selectable duration (10, 20, 40 or 60 seconds) instead of always exactly one minute. Cadence mode (2 bits), duration (2 bits) and animation index (3 bits) are packed into the same DS3231 RTC byte `0Ah`; the BLE characteristic (`MC_ANIMATION_SPLASH_CHAR_UUID`, formerly the Day Splash UUID, same value) carries mode in bits 0..1, duration in bits 2..3, animation index in bits 4..6 and the "play now" preview command in bit 7. Renamed the feature to "Animation Splash" throughout the code
- [x] Added a second animation, "Russian flag" (animation index 1), alongside Nyan Cat

**Full Changelog**: https://github.com/golyakoff/Matrix_Clock_ESP32/compare/v1.4.0...v1.5.0

Release 1.4.0

- [x] Added "Day Splash": an animated GIF screensaver played over the 00:00 minute at the day boundary as an easter egg. Instead of the usual falling-Tetris layout, the `23:59 -> 00:00` change starts the selected animation looping and it keeps looping for the whole 00:00 minute; at 00:01 the clock returns and the digits are laid out with Tetris as usual. It plays only while the display is on, so it is skipped if the night off-alarm has the panel off at midnight. Controlled over a new BLE characteristic (`MC_DAY_SPLASH_CHAR_UUID`, read/write, 1 byte: bit0 = enabled, bits4..6 = animation index 0..7, bit3 = "play now" preview command) with the enabled flag and selected index persisted in a spare byte of the DS3231 RTC's repurposed alarm memory (byte `0Ah`) and restored on boot. Animations are stored as generated RGB565 headers under `include/animations/` (first one: Nyan Cat), produced from a source GIF by `tools/gen_animation.py`; different animations may have different frame counts and per-frame timings

**Full Changelog**: https://github.com/golyakoff/Matrix_Clock_ESP32/compare/v1.3.0...v1.4.0

Release 1.3.0

- [x] Added a runtime LED matrix color order setting (RRGGBB vs RRBBGG, i.e. green/blue swapped) previously hardcoded at compile time in `matrix.h`. Controlled over a new BLE characteristic (`MC_COLOR_ORDER_CHAR_UUID`, read/write, 1 byte: 0 = RRGGBB default, 1 = RRBBGG) and applied to the display immediately on write. The setting is persisted in a spare bit of the DS3231 RTC's repurposed alarm memory (byte `09h`, alongside the existing brightness settings) rather than flash/NVS, and restored on boot

**Full Changelog**: https://github.com/golyakoff/Matrix_Clock_ESP32/compare/v1.2.4...v1.3.0

Release 1.2.4

- [x] Fixed the "Update failed" screen only flashing briefly on a cancelled OTA update instead of holding for 10 seconds before rebooting (regression from v1.2.3): aborting now also used to wait for the write queue to drain directly on the BLE stack's own task, blocking it for however long that took - the same problem the write queue was added to avoid, just on the cancel path this time. Aborting now just asks the writer task to finish up on its own instead of waiting for it

**Full Changelog**: https://github.com/golyakoff/Matrix_Clock_ESP32/compare/v1.2.3...v1.2.4

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
