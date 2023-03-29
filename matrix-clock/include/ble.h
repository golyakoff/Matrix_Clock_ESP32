#ifndef __BLE_H__
#define __BLE_H__

#include "time.h"

//BLE server name
#define BLE_SERVER_NAME                     "Tetris Clock A1"

// The only service
#define MC_SERVICE_UUID                     "5DE498A1-E7A6-4F4A-B323-913741895AD0" // Matrix Clock Service

// MatrixClock time in UINT32 format: number of seconds since 1900 year.
// Time is in local time zone (not UTC, no time zone specificed).
#define MC_TIME_CHAR_UUID                   "D5BD8D18-BD9A-4EF4-B206-8C78FFBE2774" // M Read, Write, Notify

// MatrixClock time in formatted string "YYYY.MM.DD HH:mm:ss", example: "2023.12.31 09:05:42"
// Time is in local time zone (not UTC, no time zone specificed).
#define MC_TIME_STR_CHAR_UUID               "AA063B0F-DB36-47D0-8F19-A70FA97D86DF" // O Read, Notify

// Alarm timers to turn off/on the displaying of the time (to night, for example)
// This alaram event has the higher priority than the manual control.
#define MC_TURN_OFF_ALARM_CHAR_UUID         "84915734-BF86-46E7-B394-22E25B3F9007" // M Read, Write
#define MC_TURN_ON_ALARM_CHAR_UUID          "6BDBD293-B623-411C-BB2A-F429EAF93CF1" // M Read, Write

// Manual control point to turn off/on the displaying of the time
// This manual operation has the lower priority than the alarm.
#define MC_TURN_ON_CONTROL_CHAR_UUID        "2E126C52-37B8-4A7D-9688-28E33104C0E1" // M Read, Write, Notify

void ble_init();
void ble_on_update_time_callback(struct tm *dt);

#endif // __BLE_H__

