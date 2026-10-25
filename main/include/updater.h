#pragma once

#include <stdint.h>
#include <stddef.h>
#include <sdkconfig.h>

// Firmware update from the internet (GitHub releases), ported from FireLamp_ESP32 (main/updater.c).
//
// The clock reads the release manifest (CONFIG_MC_UPDATE_MANIFEST_URL: the latest GitHub release) on
// every WiFi session and on request, and reports whether a newer version is available. On the beta
// channel (net_settings_t::update_channel) it also reads the manifest of the latest beta
// (CONFIG_MC_UPDATE_BETA_MANIFEST_URL: the "beta" pre-release, which CI updates on every vX.Y.Z-betaN
// tag) and offers whichever version is higher; a beta comes before its release (v2.1.0-beta2 <
// v2.1.0). The update is installed only on request (Command characteristic). The firmware is written
// by the ota module, as a BLE upload is.
//
// Manifest format (tools/make_manifest.py):
// {
//   "version": "v2.0.0",
//   "builds": {
//     "release_4mb":  { "url": "https://...bin", "size": 1234567, "sha256": "<64 hex chars>" },
//     "release_16mb": { ... },
//     "debug_4mb":    { ... }
//   }
// }
// The build key is CONFIG_MC_BUILD_VARIANT. A clock whose build is not in the manifest is reported
// as up to date.

#define UPDATER_MANIFEST_URL CONFIG_MC_UPDATE_MANIFEST_URL
#define UPDATER_BETA_MANIFEST_URL CONFIG_MC_UPDATE_BETA_MANIFEST_URL

typedef enum {
    update_unknown = 0,     // not checked since boot
    update_checking,
    update_up_to_date,
    update_available,
    update_check_failed
} update_state_t;

typedef struct {
    uint8_t state;          // update_state_t
    char version[32];       // the latest released version, empty if never checked
    uint32_t checked_at;    // the last successful check, UTC seconds, 0 if unknown
} update_info_t;

// The progress of an installation from the internet, reported to the app (OTA status characteristic)
typedef enum {
    updater_ota_idle = 0,
    updater_ota_receiving,      // downloading and writing
    updater_ota_success,        // the clock reboots into the new firmware
    updater_ota_error,
    updater_ota_checking,       // checking for an update before installing
    updater_ota_no_update       // nothing newer was released
} updater_ota_state_t;

typedef enum {
    updater_ota_err_none = 0,
    updater_ota_err_busy = 1,       // another update is in progress
    updater_ota_err_flash = 3,      // writing the firmware failed
    updater_ota_err_sha = 5,        // the downloaded file does not match the manifest
    updater_ota_err_download = 8,   // no WiFi, the manifest or the file could not be downloaded
    updater_ota_err_no_build = 9,   // with updater_ota_no_update: a newer release has no build of this
                                    // clock's variant (a custom build), install over BLE
} updater_ota_error_t;

/**
 * @brief Called when the update info changes (from the WiFi task).
 */
typedef void (*updater_info_cb_t)();

/**
 * @brief Called with the installation progress (from the WiFi task).
 */
typedef void (*updater_ota_status_cb_t)(uint8_t state, uint8_t error, uint32_t received, uint32_t total);

/**
 * @brief Prepares the clock for an installation (stops what must not run during flash writes,
 *        shows the progress screen) and starts the ota module for a firmware of the given size.
 *
 * @return false if the ota module could not start (e.g. a BLE upload is running).
 */
typedef bool (*updater_ota_begin_cb_t)(size_t firmware_size);

void updater_init(updater_info_cb_t info_cb, updater_ota_status_cb_t ota_status_cb, updater_ota_begin_cb_t ota_begin_cb);

void updater_get_info(update_info_t *info);

/**
 * @brief Requests a check / an installation: they run in the next WiFi session, which starts right
 *        away.
 *
 * @return false if WiFi is off or not configured, or an update is already running; the failure is
 *         reported through the callbacks right away then.
 */
bool updater_request_check();
bool updater_request_install();

/**
 * @brief Called by the WiFi task while connected: performs the requested or the regular check and
 *        the requested installation.
 *
 * @param regular_check the regular check of a WiFi session is due.
 */
void updater_run(bool regular_check);

/**
 * @brief Called by the WiFi task when it could not connect: fails the pending requests, so the app
 *        does not wait for an answer that never comes.
 */
void updater_fail_pending();
