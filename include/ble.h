#ifndef __BLE_H__
#define __BLE_H__

#include "time.h"

// BLE server name
#define BLE_SERVER_NAME                     "Tetris Clock"

// BLE device MTU and OTA data buffer size
#define BLE_DEVICE_MTU                      515
#define BLE_OTA_DATA_BUFFER                 (BLE_DEVICE_MTU - 3)
#define BLE_TASK_STACK_SIZE                 8192 

// The only service
#define MC_SERVICE_UUID                     "5DE498A1-E7A6-4F4A-B323-913741895AD0" // Matrix Clock Service

// MatrixClock time in UINT32 format: number of seconds since 1900 year (Epoch time).
// Time is in local time zone (not UTC, no time zone specified).
// This site can be used to check/set the time https://www.unixtimestamp.com/
#define MC_TIME_CHAR_UUID                   "D5BD8D18-BD9A-4EF4-B206-8C78FFBE2774" // M Read, Write, Notify

// MatrixClock time in formatted string "YYYY.MM.DD HH:mm:ss", example: "2023.12.31 09:05:42"
// Time is in local time zone (not UTC, no time zone specified).
#define MC_TIME_STR_CHAR_UUID               "AA063B0F-DB36-47D0-8F19-A70FA97D86DF" // O Read, Notify

// Alarm timers to turn off/on the displaying of the time (to night, for example)
// This alarm event has the higher priority than the manual control.
#define MC_TURN_OFF_ALARM_CHAR_UUID         "84915734-BF86-46E7-B394-22E25B3F9007" // M Read, Write
#define MC_TURN_ON_ALARM_CHAR_UUID          "6BDBD293-B623-411C-BB2A-F429EAF93CF1" // M Read, Write

// Manual control point to turn off/on the displaying of the time
// This manual operation has the lower priority than the alarm.
#define MC_TURN_ON_CONTROL_CHAR_UUID        "2E126C52-37B8-4A7D-9688-28E33104C0E1" // M Read, Write, Notify

// Control point to switch between auto and manual brightness adjustment value.
// When auto is enabled, brightness is currently driven by the hourly schedule below
// (see MC_HOURLY_BRIGHTNESS_CHAR_UUID) rather than a light sensor.
#define MC_AUTO_BRIGHT_ENABLE_CHAR_UUID     "9B078810-99AB-4423-B3A8-6F2E86A09582" // M Read, Write

// Control point to setup manual brightness adjustment value
#define MC_MANUAL_BRIGHT_VAL_CHAR_UUID      "117ED80D-AF6E-4E4D-B900-48F68725A7D3" // M Read, Write

// Control point to switch the LED matrix pixel color order: 0 = RRGGBB (default), 1 = RRBBGG.
#define MC_COLOR_ORDER_CHAR_UUID            "36B8588D-6F99-4BDC-8B6F-13089F234978" // M Read, Write

// Animation Splash control point: an animated screensaver played at the start of the hour.
// Byte 0: bits0..1 = cadence mode (0 = off, 1 = daily, 2 = every 3 hours, 3 = hourly),
// bits2..3 = playback duration (0 = 10s, 1 = 20s, 2 = 40s, 3 = 60s), bits4..6 = the low 3 bits of
// the animation index, bit7 = "play now" preview command (fires an immediate one-off preview,
// independent of time and mode).
// Byte 1 (since firmware v1.8.0): bit0 = the high bit of the animation index, so the index spans
// 0..15; bits1..7 reserved (write 0). The second byte is optional in both directions: a write of
// one byte is accepted and treated as index bit3 = 0 (what a pre-v1.8.0 app sends), and a client
// that reads only byte 0 still gets a correct mode/duration/preview plus animations 0..7.
#define MC_ANIMATION_SPLASH_CHAR_UUID       "A3F1C2D4-5B6E-47A8-9C0D-1E2F3A4B5C6D" // M Read, Write

// Hourly brightness schedule: 24 brightness nibbles (0..15), one per hour of day (index 0 = 00h..00:59, ... index 23 = 23h..23:59).
// Used as the brightness source when auto brightness is enabled (MC_AUTO_BRIGHT_ENABLE_CHAR_UUID).
#define MC_HOURLY_BRIGHTNESS_CHAR_UUID      "C2C5D9AA-4C0B-4A69-9E9B-9E1D8B7A2F31" // M Read, Write

// RTC chip temperature formatted string "-XX.YY C" with the 0.25 degree precision
#define MC_TEMPERATURE_CHAR_UUID            "13BE1932-508D-4BEB-AFBC-2C21D1397920" // M Read

// Control point to setup aging offset value (8-bit signed integer)
#define MC_AGING_OFFSET_CHAR_UUID           "F89E201D-434F-4675-B60E-2CF682200C50" // M Read, Write

// Physical flash chip size in whole megabytes (1 byte, e.g. 4 or 16). Read from the flash chip's
// JEDEC id at boot (ESP.getFlashChipSize()), so it reflects the hardware regardless of which build
// is flashed. The companion app shows it in the System card and uses it to pick the matching
// release binary (4mb vs 16mb) for OTA updates.
#define MC_FLASH_SIZE_CHAR_UUID             "C9F0E1D2-3B4A-5C6D-7E8F-A0B1C2D3E4F5" // M Read

// Number of Animation Splash animations this firmware build ships (1 byte). The 16MB build may
// ship more than the 4MB build; the app reads this to show exactly the animations the connected
// clock actually has (rather than assuming a fixed set). Set once at boot via ble_set_animation_count().
#define MC_ANIMATION_COUNT_CHAR_UUID        "C9F0E1D2-3B4A-5C6D-7E8F-A0B1C2D3E4F6" // M Read

// Firmware update characteristics
#define MC_VERSION_CHAR_UUID                "BEB5483E-36E1-4688-B7F5-EA07361B26A0" // M Read, Notify
#define MC_OTA_CONTROL_CHAR_UUID            "BEB5483E-36E1-4688-B7F5-EA07361B26A1" // M Write
#define MC_OTA_DATA_CHAR_UUID               "BEB5483E-36E1-4688-B7F5-EA07361B26A2" // M Write

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
 * @brief Type of a function for setting the LED matrix color order to the device when it came from BLE.
 */
typedef void (*ble_set_color_order_on_ble_write_t)(const bool use_rrbbgg);

/**
 * @brief Type of a function for getting the LED matrix color order from the device when it requested by BLE.
 */
typedef bool (*ble_get_color_order_on_ble_read_t)();

/**
 * @brief Type of a function for setting the Animation Splash configuration when it came from BLE.
 *
 * @param mode the cadence mode (0 = off, 1 = daily, 2 = every 3 hours, 3 = hourly).
 * @param duration the playback duration (0 = 10s, 1 = 20s, 2 = 40s, 3 = 60s).
 * @param index the selected animation index (0..15).
 * @param preview_now true if an immediate one-off preview of the selected animation was requested.
 */
typedef void (*ble_set_animation_splash_on_ble_write_t)(const uint8_t mode, const uint8_t duration, const uint8_t index, const bool preview_now);

/**
 * @brief Type of a function for getting the Animation Splash configuration when it requested by BLE.
 *
 * @param mode the pointer to the memory for storing the cadence mode (0..3).
 * @param duration the pointer to the memory for storing the playback duration (0..3).
 * @param index the pointer to the memory for storing the selected animation index (0..15).
 */
typedef void (*ble_get_animation_splash_on_ble_read_t)(uint8_t* mode, uint8_t* duration, uint8_t* index);

/**
 * @brief Type of a function for setting the hourly brightness schedule to the device when it came from BLE.
 *
 * @param table pointer to the raw bytes written by the BLE client (expected to be 24 brightness nibbles, one per hour).
 * @param length number of bytes written by the BLE client.
 */
typedef void (*ble_set_hourly_brightness_on_ble_write_t)(const uint8_t* table, size_t length);

/**
 * @brief Type of a function for getting the hourly brightness schedule from the device when it requested by BLE.
 *
 * @param table_out pointer to the memory (24 bytes) for storing the hourly brightness schedule.
 */
typedef void (*ble_get_hourly_brightness_on_ble_read_t)(uint8_t* table_out);

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
 * @brief Type of a function for getting "RTC temperature" value from the device when it requested by BLE.
 */
typedef int8_t (*ble_get_rtc_temperature_ble_read_t)();

/**
 * @brief Type of a function for getting "RTC temperature" value from the device when it requested by BLE.
 */
typedef int8_t (*ble_get_rtc_aging_offset_ble_read_t)();

/**
 * @brief Type of a function for getting "RTC temperature" value from the device when it requested by BLE.
 */
typedef void (*ble_set_rtc_aging_offset_ble_write_t)(const int8_t aging_offset);

/**
 * @brief Type of a function for getting "Firmware Version" value from the device when it requested by BLE.
 */
typedef std::string (*ble_get_fw_ver_ble_read_t)();

/**
 * @brief Type of a function for controlling a firmware update process when it requested by BLE.
 */
typedef void (*ble_set_ota_control_ble_write_t)(const uint8_t* data, size_t length);

/**
 * @brief Type of a function for a new firmware data upload process when it requested by BLE.
 */
typedef void (*ble_set_ota_data_ble_write_t)(const uint8_t* data, size_t length);

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
 * @param ble_set_color_order_on_ble_write_cb Callback function for setting the LED matrix color order to the device when it came from BLE.
 * @param ble_get_color_order_on_ble_read_cb Callback function for getting the LED matrix color order from the device when it requested by BLE.
 * @param ble_set_animation_splash_on_ble_write_cb Callback function for setting the Animation Splash configuration to the device when it came from BLE.
 * @param ble_get_animation_splash_on_ble_read_cb Callback function for getting the Animation Splash configuration from the device when it requested by BLE.
 * @param ble_set_hourly_brightness_on_ble_write_cb Callback function for setting the hourly brightness schedule to the device when it came from BLE.
 * @param ble_get_hourly_brightness_on_ble_read_cb Callback function for getting the hourly brightness schedule from the device when it requested by BLE.
 * @param ble_set_alarm_on_ble_write_cb Callback function for setting 1 of 2 alarms' settings to the device when it came from BLE.
 * @param ble_get_alarm_on_ble_read_cb Callback function for getting 1 of 2 alarms' settings from the device when it requested by BLE.
 * @param ble_get_rtc_temperature_ble_read_cb Callback function for getting RTC temperature from the device when it requested by BLE.
 * @param ble_get_rtc_aging_offset_ble_read_cb Callback function for getting RTC aging offset from the device when it requested by BLE.
 * @param ble_set_rtc_aging_offset_ble_write_cb Callback function for setting RTC aging offset to the device when it came from BLE.
 * @param ble_get_fw_ver_ble_read_cb Callback function for getting Firmware Version from the device when it requested by BLE.
 * @param ble_set_ota_control_ble_write_cb Callback function for setting OTA Control to the device when it came from BLE.
 * @param ble_set_ota_data_ble_write_cb  Callback function for setting OTA Firmware Data to the device when it came from BLE.
 */
void ble_init(
    ble_set_time_on_ble_write_t ble_set_time_on_ble_write_cb,
    ble_set_show_on_ble_write_t ble_set_show_on_ble_write_cb,
    ble_get_show_on_ble_read_t ble_get_show_on_ble_read_cb,
    ble_set_auto_bright_en_on_ble_write_t ble_set_auto_bright_en_on_ble_write_cb,
    ble_get_auto_bright_en_on_ble_read_t ble_get_auto_bright_en_on_ble_read_cb,
    ble_set_manual_bright_val_on_ble_write_t ble_set_manual_bright_val_on_ble_write_cb,
    ble_get_manual_bright_val_on_ble_read_t ble_get_manual_bright_val_on_ble_read_cb,
    ble_set_color_order_on_ble_write_t ble_set_color_order_on_ble_write_cb,
    ble_get_color_order_on_ble_read_t ble_get_color_order_on_ble_read_cb,
    ble_set_animation_splash_on_ble_write_t ble_set_animation_splash_on_ble_write_cb,
    ble_get_animation_splash_on_ble_read_t ble_get_animation_splash_on_ble_read_cb,
    ble_set_hourly_brightness_on_ble_write_t ble_set_hourly_brightness_on_ble_write_cb,
    ble_get_hourly_brightness_on_ble_read_t ble_get_hourly_brightness_on_ble_read_cb,
    ble_set_alarm_on_ble_write_t ble_set_alarm_on_ble_write_cb,
    ble_get_alarm_on_ble_read_t ble_get_alarm_on_ble_read_cb,
    ble_get_rtc_temperature_ble_read_t ble_get_rtc_temperature_ble_read_cb,
    ble_get_rtc_aging_offset_ble_read_t ble_get_rtc_aging_offset_ble_read_cb,
    ble_set_rtc_aging_offset_ble_write_t ble_set_rtc_aging_offset_ble_write_cb,
    ble_get_fw_ver_ble_read_t ble_get_fw_ver_ble_read_cb,
    ble_set_ota_control_ble_write_t ble_set_ota_control_ble_write_cb,
    ble_set_ota_data_ble_write_t ble_set_ota_data_ble_write_cb);

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

/**
 * @brief A method that is called after the BLE stack has been initialized and the advertising has started.
 *        It is used to push notification to the connected BLE device about the firmware version.
 */
void ble_update_firmware_version();

/**
 * @brief Sets the value of the animation-count characteristic (MC_ANIMATION_COUNT_CHAR_UUID) to the
 *        number of Animation Splash animations this build ships. Call once at boot after ble_init().
 *        The source is matrix_get_animation_count(); ble.cpp can't include the matrix animation
 *        registry itself, so main.cpp pushes the value in.
 *
 * @param count number of animations (0..8).
 */
void ble_set_animation_count(uint8_t count);

/**
 * @brief Asks the connected central for OTA-friendly connection parameters - chiefly a long (6 s)
 *        supervision timeout - so the fast firmware stream's flash-write stalls don't cause the
 *        link to drop mid-update (GATT status 8). Call once when an OTA update starts.
 *        No-op if no device is currently connected.
 */
void ble_request_ota_conn_params();

#endif // __BLE_H__

