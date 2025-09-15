#ifndef __OTA_H__
#define __OTA_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Event callbacks
typedef void (*ota_progress_cb_t)(size_t received, size_t total);
typedef void (*ota_status_cb_t)(const char* status, bool is_error);
typedef void (*ota_complete_cb_t)(bool success, const char* message);

/**
 * @brief Begins the OTA update process and prepares for firmware data reception.
 * 
 * @return true if OTA process started successfully.
 * @return false if OTA process failed to start or another update is already in progress.
 */
bool ota_begin(
    size_t firmware_size,
    ota_progress_cb_t on_ota_progress_cb,
    ota_status_cb_t on_ota_status_cb,
    ota_complete_cb_t on_ota_complete_cb
);

/**
 * @brief Writes firmware data to the OTA update partition.
 * 
 * @param data Pointer to the firmware data buffer to be written.
 * @param length Size of the data buffer in bytes.
 * @return true if data was written successfully.
 * @return false if write operation failed or OTA process is not active.
 */
bool ota_write(uint8_t* data, size_t length);

/**
 * @brief Finalizes the OTA update process and validates the written firmware.
 * 
 * @return true if OTA process completed successfully and firmware is valid.
 * @return false if OTA completion failed or firmware validation failed.
 */
bool ota_end();

/**
 * @brief Aborts the current OTA update process and cleans up resources.
 *        Any partially written firmware will be discarded.
 */
void ota_abort();

/**
 * @brief Retrieves the current firmware version running on the device.
 * 
 * @return const char* Pointer to a null-terminated string containing the version.
 *         The string is statically allocated and should not be freed.
 */
const char* ota_get_current_version();

/**
 * @brief Validates the integrity and authenticity of the received firmware.
 * 
 * @return true if firmware is valid and can be booted.
 * @return false if firmware is corrupted, invalid, or fails verification checks.
 */
bool ota_validate_firmware();

/**
 * @brief Checks if an OTA update process is currently in progress.
 * 
 * @return true if OTA update is active and receiving data.
 * @return false if no OTA update is in progress.
 */
bool ota_is_in_progress();

/**
 * @brief Gets the total number of bytes received in the current OTA update.
 * 
 * @return size_t Number of bytes received so far.
 *         Returns 0 if no OTA update is in progress.
 */
size_t ota_get_bytes_received();

/**
 * @brief Gets the total expected size of the firmware being received.
 * 
 * @return size_t Total size of the firmware in bytes.
 *         Returns 0 if total size is unknown or no OTA update is in progress.
 */
size_t ota_get_total_size();

/**
 * @brief Emits a status message through the configured callback.
 * 
 * @param status Null-terminated status message.
 */
void ota_emit_status(const char* status, bool is_error);

/**
 * @brief Emits progress update through the configured callback.
 * 
 * @param received Number of bytes received.
 * @param total Total number of bytes expected.
 */
void ota_emit_progress(size_t received, size_t total);

/**
 * @brief Emits completion event through the configured callback.
 * 
 * @param success true if operation was successful.
 * @param message Completion message.
 */
void ota_emit_complete(bool success, const char* message);

#endif // __OTA_H__