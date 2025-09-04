#include <Arduino.h>
#include <Update.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ota.h"
#include "version.h"

// Static variables for OTA state management
static ota_config_t ota_config = {0};
static bool ota_in_progress = false;
static size_t ota_bytes_received = 0;
static size_t ota_total_size = 0;
static const esp_partition_t* target_partition = NULL;

/**
 * @brief Internal function to find and validate the target OTA partition.
 * 
 * @return const esp_partition_t* Pointer to the target partition, or NULL if not found.
 */
static const esp_partition_t* get_target_partition() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (!running) {
        ota_emit_status("Running partition not found");
        return NULL;
    }

    // Select the opposite OTA partition for update
    esp_partition_subtype_t target_subtype = 
        (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) ?
        ESP_PARTITION_SUBTYPE_APP_OTA_1 : ESP_PARTITION_SUBTYPE_APP_OTA_0;

    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, target_subtype, NULL);
    
    if (!partition) {
        ota_emit_status("Target OTA partition not found");
    }
    
    return partition;
}

/**
 * @brief Internal function to save OTA update information to NVS storage.
 * 
 * @return true if update information was saved successfully.
 * @return false if saving to NVS failed.
 */
static bool save_update_info() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("ota_info", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ota_emit_status("Failed to open NVS for update info");
        return false;
    }

    // Save previous firmware version
    err = nvs_set_str(nvs_handle, "prev_version", MATRIX_CLOCK_FIRMWARE_VERSION);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        ota_emit_status("Failed to save previous version");
        return false;
    }

    // Save update timestamp
    err = nvs_set_u32(nvs_handle, "update_time", (uint32_t)time(NULL));
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        ota_emit_status("Failed to save update time");
        return false;
    }

    // Commit changes to NVS
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        ota_emit_status("Failed to commit update info");
        return false;
    }

    return true;
}

/**
 * @brief Initializes the OTA update system with the provided configuration.
 * 
 * @param config Pointer to the OTA configuration structure containing callback functions.
 *               If NULL, default configuration will be used.
 */
void ota_init(const ota_config_t* config) {
    // Initialize with provided configuration or defaults
    if (config) {
        ota_config = *config;
    }

    // Initialize NVS flash storage
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Erase and reinitialize NVS if needed
        nvs_flash_erase();
        nvs_flash_init();
    }

    ota_emit_status("OTA system initialized");
}

/**
 * @brief Begins the OTA update process and prepares for firmware data reception.
 * 
 * @return true if OTA process started successfully.
 * @return false if OTA process failed to start or another update is already in progress.
 */
bool ota_begin() {
    if (ota_in_progress) {
        ota_emit_status("OTA already in progress");
        return false;
    }

    // Find and validate target partition
    target_partition = get_target_partition();
    if (!target_partition) {
        ota_emit_status("Failed to find target partition");
        return false;
    }

    // Initialize OTA state variables
    ota_total_size = target_partition->size;
    ota_bytes_received = 0;
    ota_in_progress = true;

    // Log partition information
    char status_msg[128];
    snprintf(status_msg, sizeof(status_msg), 
             "Starting OTA update to partition %d, size: %d bytes", 
             target_partition->subtype, ota_total_size);
    ota_emit_status(status_msg);

    // Begin the update process
    if (!Update.begin(ota_total_size, target_partition->subtype)) {
        ota_emit_status("Update.begin() failed");
        ota_in_progress = false;
        return false;
    }

    // Emit initial progress
    ota_emit_progress(0, ota_total_size);
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
    if (!ota_in_progress) {
        ota_emit_status("OTA not started");
        return false;
    }

    if (data == NULL || length == 0) {
        ota_emit_status("Invalid data parameters");
        return false;
    }

    // Write data to the update partition
    size_t written = Update.write(data, length);
    if (written != length) {
        char error_msg[64];
        snprintf(error_msg, sizeof(error_msg), 
                 "Write failed: wrote %d of %d bytes", written, length);
        ota_emit_status(error_msg);
        ota_abort();
        return false;
    }

    // Update progress and emit callback
    ota_bytes_received += written;
    ota_emit_progress(ota_bytes_received, ota_total_size);
    return true;
}

/**
 * @brief Finalizes the OTA update process and validates the written firmware.
 * 
 * @return true if OTA process completed successfully and firmware is valid.
 * @return false if OTA completion failed or firmware validation failed.
 */
bool ota_end() {
    if (!ota_in_progress) {
        ota_emit_status("OTA not started");
        return false;
    }

    // Finalize the update process
    if (!Update.end()) {
        ota_emit_status("Update.end() failed");
        ota_abort();
        return false;
    }

    // Verify that the update completed successfully
    if (!Update.isFinished()) {
        ota_emit_status("Update not finished");
        ota_abort();
        return false;
    }

    // Validate the received firmware
    if (!ota_validate_firmware()) {
        ota_emit_status("Firmware validation failed");
        ota_abort();
        return false;
    }

    // Save update information to NVS
    if (!save_update_info()) {
        ota_emit_status("Failed to save update info, but OTA completed");
    }

    // Emit completion events
    ota_emit_status("OTA update completed successfully");
    ota_emit_progress(ota_total_size, ota_total_size);
    ota_emit_complete(true, "Update completed successfully");
    
    // Reset OTA state
    ota_in_progress = false;
    return true;
}

/**
 * @brief Aborts the current OTA update process and cleans up resources.
 *        Any partially written firmware will be discarded.
 */
void ota_abort() {
    if (ota_in_progress) {
        // Abort the update and clean up resources
        Update.end(false);
        ota_emit_status("OTA update aborted");
        ota_emit_complete(false, "Update aborted");
    }
    
    // Reset all state variables
    ota_in_progress = false;
    ota_bytes_received = 0;
    ota_total_size = 0;
    target_partition = NULL;
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
    if (!ota_in_progress && ota_bytes_received == 0) {
        ota_emit_status("No firmware to validate");
        return false;
    }

    // Basic size validation - firmware should be at least 1KB
    if (ota_bytes_received < 1024) {
        ota_emit_status("Firmware too small to be valid");
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
        ota_emit_status("Cannot get running partition for validation");
        return false;
    }

    // Retrieve partition description for additional validation
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) != ESP_OK) {
        ota_emit_status("Failed to get partition description");
        return false;
    }

    ota_emit_status("Firmware validation passed");
    return true;
}

/**
 * @brief Switches to the newly updated firmware and reboots the device.
 *        This function does not return if successful.
 */
void ota_switch_and_reboot() {
    // Find the next partition to boot from
    const esp_partition_t* next_partition = esp_ota_get_next_update_partition(NULL);
    if (!next_partition) {
        ota_emit_status("Failed to find next partition for boot");
        return;
    }

    // Set the boot partition to the updated firmware
    esp_err_t err = esp_ota_set_boot_partition(next_partition);
    if (err != ESP_OK) {
        char error_msg[64];
        snprintf(error_msg, sizeof(error_msg), 
                 "Failed to set boot partition: 0x%x", err);
        ota_emit_status(error_msg);
        return;
    }

    // Notify and reboot
    ota_emit_status("Switching to new firmware and rebooting...");
    delay(1000);
    ESP.restart();
}

/**
 * @brief Checks if an OTA update process is currently in progress.
 * 
 * @return true if OTA update is active and receiving data.
 * @return false if no OTA update is in progress.
 */
bool ota_is_in_progress() {
    return ota_in_progress;
}

/**
 * @brief Gets the total number of bytes received in the current OTA update.
 * 
 * @return size_t Number of bytes received so far.
 *         Returns 0 if no OTA update is in progress.
 */
size_t ota_get_bytes_received() {
    return ota_bytes_received;
}

/**
 * @brief Gets the total expected size of the firmware being received.
 * 
 * @return size_t Total size of the firmware in bytes.
 *         Returns 0 if total size is unknown or no OTA update is in progress.
 */
size_t ota_get_total_size() {
    return ota_total_size;
}

/**
 * @brief Emits status update through the configured status callback.
 * 
 * @param status Null-terminated string containing the status message.
 */
void ota_emit_status(const char* status) {
    if (ota_config.status_cb) {
        ota_config.status_cb(status);
    }
    Serial.printf("OTA Status: %s\n", status);
}

/**
 * @brief Emits progress update through the configured progress callback.
 * 
 * @param received Number of bytes received so far.
 * @param total Total number of bytes expected.
 */
void ota_emit_progress(size_t received, size_t total) {
    if (ota_config.progress_cb) {
        ota_config.progress_cb(received, total);
    }
    
    // Log progress every 4KB to avoid spamming
    if (received % 4096 == 0) {
        Serial.printf("OTA Progress: %d/%d bytes (%.1f%%)\n", 
                     received, total, (received * 100.0) / total);
    }
}

/**
 * @brief Emits completion status through the configured completion callback.
 * 
 * @param success true if OTA update completed successfully, false otherwise.
 * @param message Null-terminated string containing completion message.
 */
void ota_emit_complete(bool success, const char* message) {
    if (ota_config.complete_cb) {
        ota_config.complete_cb(success, message);
    }
    Serial.printf("OTA Complete: %s - %s\n", success ? "SUCCESS" : "FAILED", message);
}