#pragma once

#include <stdint.h>
#include <stddef.h>
#include <time.h>

// WiFi client (station), ported from FireLamp_ESP32 (main/wifi.c). In the periodic mode it connects
// every net_settings_t::period_hours, syncs time by SNTP, runs the update check (updater.h) and
// disconnects. Off by default. Everything runs in its own task.

typedef enum {
    wifi_state_off = 0,         // also between the sessions of the periodic mode
    wifi_state_connecting,
    wifi_state_connected,
    wifi_state_error            // wrong password, network not found; kept until the next attempt
} wifi_state_t;

typedef struct {
    uint8_t state;      // wifi_state_t
    uint8_t ip[4];      // zeros if not connected
    int8_t rssi;        // dBm, 0 if not connected
    uint32_t synced_at; // the last successful time sync, UTC seconds, 0 if none since boot
} wifi_status_t;

typedef enum {
    wifi_scan_none = 0,  // not scanned since boot
    wifi_scan_running,
    wifi_scan_done,
    wifi_scan_error
} wifi_scan_state_t;

typedef enum {
    wifi_security_open = 0,     // also OWE
    wifi_security_wep,
    wifi_security_personal,     // WPA/WPA2/WPA3 personal, WAPI: needs a password
    wifi_security_enterprise    // the clock cannot connect to it
} wifi_security_t;

typedef struct {
    char ssid[33];
    int8_t rssi;        // dBm
    uint8_t security;   // wifi_security_t
    uint8_t channel;
} wifi_network_t;

#define WIFI_SCAN_MAX (64U) // networks kept after a scan (strongest first)

/**
 * @brief Called from the WiFi task when the state or the last sync time changes. Must be fast.
 */
typedef void (*wifi_status_cb_t)();

/**
 * @brief Called from the WiFi task when a scan starts or finishes. Must be fast.
 */
typedef void (*wifi_scan_cb_t)();

/**
 * @brief Called from the WiFi task with the time from SNTP (UTC). The receiver converts it to the
 *        local time of the clock (TZ is set by wifi_init() / wifi_apply_settings()).
 */
typedef void (*wifi_time_cb_t)(time_t utc);

/**
 * @brief Initializes the network interface and the WiFi driver, starts the WiFi task.
 */
bool wifi_init(wifi_status_cb_t status_cb, wifi_scan_cb_t scan_cb, wifi_time_cb_t time_cb);

/**
 * @brief The settings changed (net_settings.h): apply the time zone, reconnect according to the mode.
 */
void wifi_apply_settings();

/**
 * @brief Stops WiFi during a BLE firmware upload and resumes it.
 */
void wifi_pause(bool pause);

/**
 * @brief Something needs the internet now (time sync, update check or install): in the periodic
 *        mode a session starts right away.
 *
 * @return false if WiFi is off or not configured.
 */
bool wifi_request_session();

/**
 * @brief Whether WiFi is configured and on (a session can be requested).
 */
bool wifi_is_usable();

void wifi_get_status(wifi_status_t *status);

/**
 * @brief Starts a scan for networks, asynchronously; works in any mode (the radio is turned on
 *        for the scan only when WiFi is off or between sessions).
 *
 * @return false during a BLE firmware upload.
 */
bool wifi_scan_start();

/**
 * @brief Locks the scan result and gets it: unique SSIDs without hidden networks, strongest first.
 *        Must be followed by wifi_scan_unlock().
 */
wifi_scan_state_t wifi_scan_lock(const wifi_network_t **networks, size_t *count);

void wifi_scan_unlock();
