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

// Control point to switch between auto and manual brightness adjustment value
#define MC_AUTO_BRIGHT_ENABLE_CHAR_UUID     "9B078810-99AB-4423-B3A8-6F2E86A09582" // M Read, Write

// Control point to setup manual brightness adjustment value
#define MC_MANUAL_BRIGHT_VAL_CHAR_UUID      "117ED80D-AF6E-4E4D-B900-48F68725A7D3" // M Read, Write

/**
 * @brief Type of a function for setting a new time to the device when it came from BLE.
 */
typedef void (*ble_set_time_on_ble_write_t)(const struct tm *dt);

/**
 * @brief Type of a function for setting "show" state to the device when it came from BLE.
 */
typedef void (*ble_set_show_on_ble_write_t)(const bool show);

/**
 * @brief Type of a function for getting "show" state from the device when it requested by BLE.
 */
typedef bool (*ble_get_show_on_ble_read_t)();

/**
 * @brief Type of a function for setting "auto brightness" state to the device when it came from BLE.
 */
typedef void (*ble_set_auto_bright_en_on_ble_write_t)(const bool show);

/**
 * @brief Type of a function for getting "auto brightness" state from the device when it requested by BLE.
 */
typedef bool (*ble_get_auto_bright_en_on_ble_read_t)();

/**
 * @brief Type of a function for setting "manual brightness" value to the device when it came from BLE.
 */
typedef void (*ble_set_manual_bright_val_on_ble_write_t)(const uint8_t manual_brightness);

/**
 * @brief Type of a function for getting "manual brightness" value from the device when it requested by BLE.
 */
typedef uint8_t (*ble_get_manual_bright_val_on_ble_read_t)();

/**
 * @brief Alarm index used to choose the alarm we work with in the methods below: On/Off.
 */
typedef enum {
    ble_alarm_index_on = 1,
    ble_alarm_index_off = 2
} ble_alarm_index_t;

/**
 * @brief Type of a function for setting 1 of 2 alarms' settings to the device when it came from BLE.
 */
typedef void (*ble_set_alarm_on_ble_write_t)(
    const ble_alarm_index_t index,
    const uint8_t hours,
    const uint8_t minutes,
    const bool active);

/**
 * @brief Type of a function for getting 1 of 2 alarms' settings from the device when it requested by BLE.
 */
typedef void (*ble_get_alarm_on_ble_read_t)(
    ble_alarm_index_t index,
    uint8_t *hours,
    uint8_t *minutes,
    bool *active);

/**
 * @brief Initializes the BLE module settings.
 * 
 * @param ble_set_time_on_ble_write_cb Callback function for setting a new time to the device when it came from BLE.
 * @param ble_set_show_on_ble_write_cb Callback function for setting "show" state to the device when it came from BLE.
 * @param ble_get_show_on_ble_read_cb Callback function for getting "show" state from the device when it requested by BLE.
 * @param ble_set_auto_bright_en_on_ble_write_cb Callback function for setting "auto brightness" state to the device when it came from BLE.
 * @param ble_get_auto_bright_en_on_ble_read_cb Callback function for getting "auto brightness" state from the device when it requested by BLE.
 * @param ble_set_manual_bright_val_on_ble_write_cb Callback function for setting "manual brightness" value to the device when it came from BLE.
 * @param ble_get_manual_bright_val_on_ble_read_cb Callback function for getting "manual brightness" value from the device when it requested by BLE.
 * @param ble_set_alarm_on_ble_write_cb Callback function for setting 1 of 2 alarms' settings to the device when it came from BLE.
 * @param ble_get_alarm_on_ble_read_cb Callback function for getting 1 of 2 alarms' settings from the device when it requested by BLE.
 */
void ble_init(
    ble_set_time_on_ble_write_t ble_set_time_on_ble_write_cb,
    ble_set_show_on_ble_write_t ble_set_show_on_ble_write_cb,
    ble_get_show_on_ble_read_t ble_get_show_on_ble_read_cb,
    ble_set_auto_bright_en_on_ble_write_t ble_set_auto_bright_en_on_ble_write_cb,
    ble_get_auto_bright_en_on_ble_read_t ble_get_auto_bright_en_on_ble_read_cb,
    ble_set_manual_bright_val_on_ble_write_t ble_set_manual_bright_val_on_ble_write_cb,
    ble_get_manual_bright_val_on_ble_read_t ble_get_manual_bright_val_on_ble_read_cb,
    ble_set_alarm_on_ble_write_t ble_set_alarm_on_ble_write_cb,
    ble_get_alarm_on_ble_read_t ble_get_alarm_on_ble_read_cb);

/**
 * @brief A method that is called when time updated outside of the BLE module
 *        and we have to reflect this changes in BLE module (each second).
 *        It is used to push notification to the connected BLE device about the time update.
 * 
 * @param dt a new time structure.
 */
void ble_update_rtc_time(struct tm *dt);

/**
 * @brief A method that is called when the matrix "show" state updated outside of the BLE module
 *        and we have to reflect this changes in BLE module (on alarm fire, for example).
 *        It is used to push notification to the connected BLE device about the matrix "show" state update.
 * 
 * @param show The new value of the matrix "show" state.
 */
void ble_update_matrix_show(const bool show);

#endif // __BLE_H__

