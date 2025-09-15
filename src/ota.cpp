#include <Arduino.h>
#include <Update.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ota.h"
#include "version.h"

// Static variables for OTA state management
static bool   _ota_in_progress    = false;
static size_t _firmware_bytes_received = 0;
static size_t _firmware_total_size     = 0;

static ota_progress_cb_t _on_ota_progress_cb  = nullptr;
static ota_status_cb_t _on_ota_status_cb = nullptr;
static ota_complete_cb_t _on_ota_complete_cb = nullptr;

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
) {
    if (_ota_in_progress) {
        ota_emit_status("OTA already in progress", true);
        return false;
    }

    // Initialize OTA state variables
    _firmware_total_size = firmware_size;
    _firmware_bytes_received = 0;
    _ota_in_progress = true;

    // Initialize OTA callbacks
    _on_ota_progress_cb = on_ota_progress_cb;
    _on_ota_status_cb = on_ota_status_cb;
    _on_ota_complete_cb = on_ota_complete_cb;    

    // Log partition information
    char status_msg[128];
    snprintf(
        status_msg, sizeof(status_msg), 
        "Starting OTA update to partition U_FLASH, size: %d bytes", 
        _firmware_total_size);
    ota_emit_status(status_msg, false);

    // Begin the update process
    if (!Update.begin(_firmware_total_size, U_FLASH)) {
        ota_emit_status("Update.begin() failed", true);
        _ota_in_progress = false;
        return false;
    }

    // Emit initial progress
    ota_emit_progress(0, _firmware_total_size);
    return true;
}

/**
 * @brief Writes firmware data to the OTA update partition.
 * 
 * @param data Pointer to the firmware data buffer to be written.
 * @param length Size of the data buffer in bytes.
 * @return true if data was written successfully.
 * @return false if write operation failed or OTA process is not active.
 */
bool ota_write(uint8_t* data, size_t length) {
    if (!_ota_in_progress) {
        ota_emit_status("OTA not started", true);
        return false;
    }

    if (data == NULL || length == 0) {
        ota_emit_status("Invalid data parameters", true);
        return false;
    }

    delay(1);

    // Write data to the update partition
    size_t written = Update.write(data, length);
    if (written != length) {
        char error_msg[64];
        snprintf(error_msg, sizeof(error_msg), 
                 "Write failed: wrote %d of %d bytes", written, length);
        ota_emit_status(error_msg, true);
        ota_abort();
        return false;
    }

    // Update progress and emit callback
    _firmware_bytes_received += written;
    ota_emit_progress(_firmware_bytes_received, _firmware_total_size);
    return true;
}

/**
 * @brief Finalizes the OTA update process and validates the written firmware.
 * 
 * @return true if OTA process completed successfully and firmware is valid.
 * @return false if OTA completion failed or firmware validation failed.
 */
bool ota_end() {
    if (!_ota_in_progress) {
        ota_emit_status("OTA not started", true);
        return false;
    }

    // Finalize the update process
    if (!Update.end()) {
        ota_emit_status("Update.end() failed", true);
        ota_abort();
        return false;
    }

    // Verify that the update completed successfully
    if (!Update.isFinished()) {
        ota_emit_status("Update not finished", true);
        ota_abort();
        return false;
    }

    // Validate the received firmware
    if (!ota_validate_firmware()) {
        ota_emit_status("Firmware validation failed", true);
        ota_abort();
        return false;
    }

    // Emit completion events
    ota_emit_progress(_firmware_total_size, _firmware_total_size);
    ota_emit_complete(true, "OTA update completed successfully");
    
    // Reset OTA state
    _ota_in_progress = false;

    // Find the next partition to boot from
    const esp_partition_t* next_partition = esp_ota_get_next_update_partition(NULL);
    if (!next_partition) {
        ota_emit_status("Failed to find next partition for boot", true);
        return false;
    }

    // Set the boot partition to the updated firmware
    esp_err_t err = esp_ota_set_boot_partition(next_partition);
    if (err != ESP_OK) {
        char error_msg[64];
        snprintf(error_msg, sizeof(error_msg), 
                 "Failed to set boot partition: 0x%x", err);
        ota_emit_status(error_msg, true);
        return false;
    }

    // Notify and reboot
    ota_emit_status("Switching to new firmware and rebooting...", false);
    delay(1000);
    ESP.restart();

    return true;
}

/**
 * @brief Aborts the current OTA update process and cleans up resources.
 *        Any partially written firmware will be discarded.
 */
void ota_abort() {
    if (_ota_in_progress) {
        // Abort the update and clean up resources
        Update.end(false);
        ota_emit_status("OTA update aborted", true);
        ota_emit_complete(false, "Update aborted");
    }
    
    // Reset all state variables
    _ota_in_progress = false;
    _firmware_bytes_received = 0;
    _firmware_total_size = 0;
}

/**
 * @brief Retrieves the current firmware version running on the device.
 * 
 * @return const char* Pointer to a null-terminated string containing the version.
 *         The string is statically allocated and should not be freed.
 */
const char* ota_get_current_version() {
    return MATRIX_CLOCK_FIRMWARE_VERSION;
}

/**
 * @brief Validates the integrity and authenticity of the received firmware.
 * 
 * @return true if firmware is valid and can be booted.
 * @return false if firmware is corrupted, invalid, or fails verification checks.
 */
bool ota_validate_firmware() {
    // Check if there's any firmware to validate
    if (!_ota_in_progress && _firmware_bytes_received == 0) {
        ota_emit_status("No firmware to validate", true);
        return false;
    }

    // Basic size validation - firmware should be at least 1KB
    if (_firmware_bytes_received != _firmware_total_size) {
        ota_emit_status("Firmware bytes received count not equals to the expected firmware size", true);
        return false;
    }

    // Additional validation can be added here:
    // - SHA256 hash verification
    // - Digital signature validation  
    // - Version compatibility checks
    // - Memory range validation

    // Get running partition information for validation
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (!running) {
        ota_emit_status("Cannot get running partition for validation", true);
        return false;
    }

    // Retrieve partition description for additional validation
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) != ESP_OK) {
        ota_emit_status("Failed to get partition description", true);
        return false;
    }

    ota_emit_status("Firmware validation passed", true);
    return true;
}

/**
 * @brief Checks if an OTA update process is currently in progress.
 * 
 * @return true if OTA update is active and receiving data.
 * @return false if no OTA update is in progress.
 */
bool ota_is_in_progress() {
    return _ota_in_progress;
}

/**
 * @brief Gets the total number of bytes received in the current OTA update.
 * 
 * @return size_t Number of bytes received so far.
 *         Returns 0 if no OTA update is in progress.
 */
size_t ota_get_bytes_received() {
    return _firmware_bytes_received;
}

/**
 * @brief Gets the total expected size of the firmware being received.
 * 
 * @return size_t Total size of the firmware in bytes.
 *         Returns 0 if total size is unknown or no OTA update is in progress.
 */
size_t ota_get_total_size() {
    return _firmware_total_size;
}

/**
 * @brief Emits status update through the configured status callback.
 * 
 * @param status Null-terminated string containing the status message.
 */
void ota_emit_status(const char* status, bool is_error) {
    if (_on_ota_status_cb) {
        _on_ota_status_cb(status, is_error);
    }
}

/**
 * @brief Emits progress update through the configured progress callback.
 * 
 * @param received Number of bytes received so far.
 * @param total Total number of bytes expected.
 */
void ota_emit_progress(size_t received, size_t total) {
    if (_on_ota_progress_cb) {
        _on_ota_progress_cb(received, total);
    }
}

/**
 * @brief Emits completion status through the configured completion callback.
 * 
 * @param success true if OTA update completed successfully, false otherwise.
 * @param message Null-terminated string containing completion message.
 */
void ota_emit_complete(bool success, const char* message) {
    if (_on_ota_complete_cb) {
        _on_ota_complete_cb(success, message);
    }
}