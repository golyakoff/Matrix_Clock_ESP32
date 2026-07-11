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
