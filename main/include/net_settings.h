#pragma once

#include <stdint.h>
#include <stddef.h>

// Network settings kept in NVS: the WiFi connection and the time zone.

#define NET_SSID_MAX_LEN        (32U)  // bytes, as in the 802.11 standard
#define NET_PASSWORD_MAX_LEN    (64U)  // bytes, WPA2 passphrase (63) or raw PSK in hex (64)
#define NET_TZ_MAX_LEN          (47U)  // bytes of a POSIX TZ string, e.g. "CET-1CEST,M3.5.0,M10.5.0/3"

#define NET_WIFI_PERIOD_MIN     (1U)   // hours
#define NET_WIFI_PERIOD_MAX     (24U)  // hours

// The default time zone, until the app sends the phone's one
#define NET_DEFAULT_TZ          "MSK-3"

/**
 * @brief WiFi mode: the clock connects to the home network as a client only.
 */
typedef enum {
    net_wifi_off = 0,       // never connects (default)
    net_wifi_periodic = 1   // every period_hours: connect, sync time, check for an update, disconnect
} net_wifi_mode_t;

typedef struct {
    uint8_t mode;                           // net_wifi_mode_t
    uint8_t period_hours;                   // NET_WIFI_PERIOD_MIN..NET_WIFI_PERIOD_MAX
    char ssid[NET_SSID_MAX_LEN + 1];
    char password[NET_PASSWORD_MAX_LEN + 1]; // empty for an open network
    char tz[NET_TZ_MAX_LEN + 1];
} net_settings_t;

/**
 * @brief Loads the settings from NVS (defaults when there are none).
 *
 * @return false if NVS could not be opened.
 */
bool net_settings_init();

/**
 * @brief Copies the current settings. Safe from any task.
 */
void net_settings_get(net_settings_t *out);

/**
 * @brief Changes the WiFi settings in RAM; they are written to NVS by net_settings_flush().
 *
 * @param password the new password, or nullptr to keep the stored one.
 * @return false if a value is out of range (nothing is changed then).
 */
bool net_settings_set_wifi(uint8_t mode, uint8_t period_hours, const char *ssid, const char *password);

/**
 * @brief Changes the time zone in RAM; it is written to NVS by net_settings_flush().
 *
 * @return false if the string is empty or too long (nothing is changed then).
 */
bool net_settings_set_tz(const char *tz);

/**
 * @brief Whether there are changes not written to NVS yet.
 */
bool net_settings_is_dirty();

/**
 * @brief Writes the pending changes into NVS. Called from the main loop, which stops the display
 *        timers meanwhile (see main_brightness_schedule_flush()).
 */
bool net_settings_flush();
