#ifndef __BLE_H__
#define __BLE_H__

#include <string>
#include "time.h"
#include "wifi.h"
#include "updater.h"

// BLE server name
#define BLE_SERVER_NAME                     "Tetris Clock"

// BLE device MTU and OTA data buffer size
#define BLE_DEVICE_MTU                      515
#define BLE_OTA_DATA_BUFFER                 (BLE_DEVICE_MTU - 3)
#define BLE_TASK_STACK_SIZE                 8192 

// The only service
#define MC_SERVICE_UUID                     "5DE498A1-E7A6-4F4A-B323-913741895AD0" // Matrix Clock Service

// MatrixClock time in UINT32 format: the local date and time of the clock counted as if it were UTC, i.e.
// seconds since 1970-01-01 00:00:00 of the clock's calendar fields (no time zone applied in either direction).
// This site can be used to check/set the time https://www.unixtimestamp.com/
#define MC_TIME_CHAR_UUID                   "D5BD8D18-BD9A-4EF4-B206-8C78FFBE2774" // M Read, Write, Notify

// MatrixClock time in formatted string "YYYY-MM-DD HH:mm:ss", example: "2023-12-31 09:05:42", padded with
// zero bytes to 20 bytes (the month was 0-based before firmware v2.0.0)
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

// --- Network characteristics (firmware v2.0.0+). Optional for the app: older firmware lacks them, and
// the app hides WiFi and the update from the internet then. All integers are little-endian; a "str8"
// is a length byte followed by that many UTF-8 bytes (no terminating zero).

// POSIX TZ string of the clock's time zone, up to 47 bytes, e.g. "MSK-3" or
// "CET-1CEST,M3.5.0,M10.5.0/3" (note the inverted sign: UTC+3 is "-3"). The RTC keeps local time;
// the time zone converts the UTC time from the internet (SNTP) to it, daylight saving time included.
// The app writes the phone's zone. Default "MSK-3".
#define MC_TIME_ZONE_CHAR_UUID              "C9F0E1D2-3B4A-5C6D-7E8F-A0B1C2D3E4F7" // O Read, Write

// WiFi settings. Write: u8 mode (0 = off, 1 = periodic: every <period> hours connect, sync time, check
// for an update, disconnect), u8 period 1..24 hours, str8 SSID (up to 32 bytes), str8 password (up to
// 64 bytes; length 0xFF = keep the stored one). Like every characteristic but the version, it needs a
// bonded phone (see ble_open_pairing_window()). Read: the same, but the password is never returned: its length
// byte is 0 (no password stored) or 0xFF (a password is stored), followed by no bytes.
#define MC_WIFI_SETTINGS_CHAR_UUID          "C9F0E1D2-3B4A-5C6D-7E8F-A0B1C2D3E4F8" // O Read, Write (encrypted)

// WiFi status: u8 state (0 = off, also between sessions, 1 = connecting, 2 = connected, 3 = error: wrong
// password or network not found, kept until the next attempt), u8[4] IPv4 address (zeros if not
// connected), i8 RSSI dBm (0 if not connected), u32 the last time sync from the internet, UTC seconds
// (0 if none since boot). Notified on every change.
#define MC_WIFI_STATUS_CHAR_UUID            "C9F0E1D2-3B4A-5C6D-7E8F-A0B1C2D3E4F9" // O Read, Notify

// WiFi networks the clock hears (2.4 GHz only), for choosing the SSID; the scan is started by Command
// 0x04. Unique SSIDs, no hidden networks, strongest first, up to 64. Read: one page starting at the
// selected network (the first one after connecting and after every scan): u8 scan state (0 = not
// scanned since boot, 1 = scanning, 2 = done, 3 = error), u8 total networks T, u8 index of the first
// network on this page, u8 networks on this page N, then N x (str8 SSID, i8 RSSI dBm, u8 security:
// 0 = open, 1 = WEP, 2 = WPA/WPA2/WPA3 personal, 3 = enterprise (unusable), u8 channel). Write: u8 index
// of the first network of the next page to read. Notify: the 4-byte header (N = 0) when a scan starts
// and finishes.
#define MC_WIFI_NETWORKS_CHAR_UUID          "C9F0E1D2-3B4A-5C6D-7E8F-A0B1C2D3E4FA" // O Read, Write, Notify

// Command, write u8: 0x01 = sync time from the internet now, 0x02 = check for a firmware update now
// (result in Update info), 0x03 = install the firmware update from the internet if a newer one is
// released (progress in OTA status), 0x04 = scan for WiFi networks (see WiFi networks). 0x01..0x03 need
// WiFi configured; without it 0x02 reports a failed check and 0x03 an OTA error 8.
#define MC_COMMAND_CHAR_UUID                "C9F0E1D2-3B4A-5C6D-7E8F-A0B1C2D3E4FB" // O Write

// The last update check from the internet (on every WiFi session and on Command 0x02): u8 state
// (0 = not checked since boot, 1 = checking, 2 = up to date, 3 = update available, 4 = check failed),
// str8 the latest released version (empty if never checked), u32 time of the last successful check,
// UTC seconds (0 if unknown). Notified on every change.
#define MC_UPDATE_INFO_CHAR_UUID            "C9F0E1D2-3B4A-5C6D-7E8F-A0B1C2D3E4FC" // O Read, Notify

// Progress of a firmware installation from the internet (Command 0x03): u8 state (0 = idle,
// 1 = downloading and writing, 2 = success: the clock reboots, 3 = error, 4 = checking for an update,
// 5 = no update available), u8 error (state 3: 1 = busy, 3 = flash write error, 5 = SHA-256 mismatch,
// 8 = download failed / no WiFi; state 5: 9 = a newer release has no build for this clock, a custom
// build: update over BLE), u32 bytes written, u32 total size. Notified every 4 KB.
#define MC_OTA_STATUS_CHAR_UUID             "C9F0E1D2-3B4A-5C6D-7E8F-A0B1C2D3E4FD" // O Read, Notify

// The clock's BLE name (advertised in the scan response, also the GAP device name), up to 20 bytes of
// UTF-8 without control characters. Read: the name in use. Write: a new name, applied to the next
// advertising (after the phone disconnects) and kept in NVS; an empty write restores the default
// "Tetris Clock XXXX", XXXX being the last two bytes of the Bluetooth MAC address in hex, so that
// several clocks can be told apart when pairing.
#define MC_DEVICE_NAME_CHAR_UUID            "C9F0E1D2-3B4A-5C6D-7E8F-A0B1C2D3E4FE" // O Read, Write

// Update channel, u8: 0 = releases only (default), 1 = also betas (vX.Y.Z-betaN pre-releases): the
// update from the internet then offers whichever of the latest release and the latest beta is
// higher. Kept in NVS; takes effect on the next check (Command 0x02).
#define MC_UPDATE_CHANNEL_CHAR_UUID         "C9F0E1D2-3B4A-5C6D-7E8F-A0B1C2D3E4FF" // O Read, Write

// Firmware update characteristics
#define MC_VERSION_CHAR_UUID               "BEB5483E-36E1-4688-B7F5-EA07361B26A0" // M Read, Notify
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
 * @brief Callbacks of the network characteristics (time zone, WiFi, update from the internet).
 *        They are called from the BLE stack's task and must be fast.
 */
typedef struct {
    // Time zone: get into a buffer of at least NET_TZ_MAX_LEN + 1 bytes; set returns false if invalid
    void (*get_tz)(char *tz_out);
    bool (*set_tz)(const char *tz);
    // WiFi settings; password nullptr = keep the stored one. set returns false if invalid.
    void (*get_wifi)(uint8_t *mode, uint8_t *period_hours, char *ssid_out, bool *password_stored);
    bool (*set_wifi)(uint8_t mode, uint8_t period_hours, const char *ssid, const char *password);
    void (*get_wifi_status)(wifi_status_t *status);
    void (*get_update_info)(update_info_t *info);
    // Command characteristic (0x01..0x04)
    void (*command)(uint8_t command);
    // BLE name: get the name in use into a buffer of at least NET_NAME_MAX_LEN + 1 bytes (the default
    // one when none is set); set an empty name for the default, returns false if invalid
    void (*get_name)(char *name_out);
    bool (*set_name)(const char *name);
    // Update channel (net_update_channel_t); set returns false if unknown
    uint8_t (*get_update_channel)();
    bool (*set_update_channel)(uint8_t channel);
} ble_network_callbacks_t;

/**
 * @brief Type of a function called when the phone disconnects. Called from the BLE stack's task: must be fast.
 */
typedef void (*ble_on_disconnect_t)();

/**
 * @brief Sets the function called when the phone disconnects (nullptr: none).
 */
void ble_set_disconnect_callback(ble_on_disconnect_t callback);

// Pairing. Every characteristic but MC_VERSION_CHAR_UUID needs an encrypted link from a bonded phone
// ("Just Works" pairing, no PIN; up to CONFIG_BT_NIMBLE_MAX_BONDS phones). A new phone can pair only
// while the pairing window is open: BLE_PAIRING_WINDOW_S after power on and after
// ble_open_pairing_window() (a long touch). Outside it a new pairing is undone and the phone dropped,
// and a full bond store does not make room by forgetting the oldest phone. A phone that is not let in
// within BLE_AUTH_TIMEOUT_S of connecting is dropped.
#define BLE_PAIRING_WINDOW_S                120
#define BLE_AUTH_TIMEOUT_S                  30
// A connection with no reads, writes or subscriptions for this long is dropped, so the clock
// advertises again: Android keeps the link to a just bonded clock open with no app using it, and the
// clock (one connection at most) cannot advertise while connected
#define BLE_IDLE_TIMEOUT_S                  300

/**
 * @brief Opens the pairing window for BLE_PAIRING_WINDOW_S (ble_init() opens it too).
 */
void ble_open_pairing_window();

/**
 * @brief Type of a function called when a new phone has paired. Called from the BLE stack's task: must be fast.
 */
typedef void (*ble_on_new_phone_t)();

/**
 * @brief Sets the function called when a new phone has paired (nullptr: none).
 */
void ble_set_new_phone_callback(ble_on_new_phone_t callback);

/**
 * @brief Enables the network characteristics. Call before ble_init(); without it they are not created.
 */
void ble_set_network_callbacks(const ble_network_callbacks_t *callbacks);

/**
 * @brief Notifies the WiFi status (MC_WIFI_STATUS_CHAR_UUID). Safe from any task.
 */
void ble_update_wifi_status();

/**
 * @brief Notifies a WiFi scan start / end (MC_WIFI_NETWORKS_CHAR_UUID) and resets the page cursor.
 */
void ble_update_wifi_scan();

/**
 * @brief Notifies the update info (MC_UPDATE_INFO_CHAR_UUID). Safe from any task.
 */
void ble_update_update_info();

/**
 * @brief Notifies the progress of an installation from the internet (MC_OTA_STATUS_CHAR_UUID).
 */
void ble_update_ota_status(uint8_t state, uint8_t error, uint32_t received, uint32_t total);

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

