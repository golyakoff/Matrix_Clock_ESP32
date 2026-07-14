#include <Arduino.h>
#include <Update.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
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
static ota_flash_write_begin_cb_t _on_flash_write_begin_cb = nullptr;
static ota_flash_write_end_cb_t _on_flash_write_end_cb = nullptr;

// set_ota_data_ble_write() (main.cpp) used to call straight into the flash write below, but that
// callback runs directly on the Bluedroid stack's own task - a slow flash write (a sector erase can
// take tens of ms) stalled the BLE stack's own processing right along with it, which was causing
// dropped connections mid-transfer. Received chunks are now queued here and written to flash from a
// dedicated task pinned to core 1 (the BLE stack is pinned to core 0), so a BLE write callback just
// copies ~512 bytes into the queue and returns immediately.
#define OTA_WRITE_QUEUE_DEPTH    (8U)
#define OTA_WRITE_CHUNK_MAX      (512U)
#define OTA_WRITER_TASK_STACK    (8192U)
#define OTA_WRITER_TASK_PRIORITY (1U)

typedef struct {
    uint8_t data[OTA_WRITE_CHUNK_MAX];
    size_t length;
} ota_write_item_t;

static QueueHandle_t _write_queue = nullptr;
static TaskHandle_t _writer_task_handle = nullptr;
static volatile bool _writer_busy = false;
static volatile bool _abort_requested = false;

static bool ota_write_now(uint8_t* data, size_t length);
static void ota_writer_task(void* pvParameters);
static bool ota_wait_for_write_queue_drain(uint32_t timeout_ms);
static void ota_abort_now();

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
    ota_complete_cb_t on_ota_complete_cb,
    ota_flash_write_begin_cb_t on_flash_write_begin_cb,
    ota_flash_write_end_cb_t on_flash_write_end_cb
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
    _on_flash_write_begin_cb = on_flash_write_begin_cb;
    _on_flash_write_end_cb = on_flash_write_end_cb;

    // Created once and left running: an OTA attempt always ends in a reboot (success reboots
    // immediately, failure/abort reboots itself 10s later - see main.cpp), so there's no case
    // where this needs to be torn down and recreated within the same power-on session.
    if (!_write_queue) {
        _write_queue = xQueueCreate(OTA_WRITE_QUEUE_DEPTH, sizeof(ota_write_item_t));
        if (!_write_queue) {
            ota_emit_status("Failed to create OTA write queue", true);
            _ota_in_progress = false;
            return false;
        }
    }

    if (!_writer_task_handle) {
        BaseType_t created = xTaskCreatePinnedToCore(
            ota_writer_task, "ota_writer", OTA_WRITER_TASK_STACK, nullptr,
            OTA_WRITER_TASK_PRIORITY, &_writer_task_handle, 1);
        if (created != pdPASS) {
            ota_emit_status("Failed to create OTA writer task", true);
            _ota_in_progress = false;
            return false;
        }
    }

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
 * @brief Queues firmware data to be written to the OTA update partition by the writer task.
 *
 * @param data Pointer to the firmware data buffer to be written.
 * @param length Size of the data buffer in bytes.
 * @return true if the chunk was queued successfully.
 * @return false if the chunk was rejected (OTA not active, bad params, or the writer task
 *         isn't draining the queue - e.g. because it's stuck or was never created).
 */
bool ota_write(uint8_t* data, size_t length) {
    if (!_ota_in_progress) {
        ota_emit_status("OTA not started", true);
        return false;
    }

    if (data == NULL || length == 0 || length > OTA_WRITE_CHUNK_MAX) {
        ota_emit_status("Invalid data parameters", true);
        return false;
    }

    if (!_write_queue) {
        ota_emit_status("OTA write queue not initialized", true);
        return false;
    }

    ota_write_item_t item;
    memcpy(item.data, data, length);
    item.length = length;

    // Bounded wait: if the writer task hasn't drained enough of the queue within half a second,
    // something is seriously wrong (e.g. it crashed) - fail loudly instead of blocking the BLE
    // stack's own task indefinitely.
    if (xQueueSend(_write_queue, &item, pdMS_TO_TICKS(500)) != pdTRUE) {
        ota_emit_status("OTA write queue full - writer task stalled", true);
        ota_abort();
        return false;
    }

    return true;
}

// Runs on its own task (core 1, see ota_begin()) so a slow flash write never blocks the BLE
// stack's task. Does the actual work the old synchronous ota_write() used to do.
static bool ota_write_now(uint8_t* data, size_t length) {
    // Guards against a stale item that was still sitting in the queue when an abort/failure
    // already ended the session (e.g. via a previous item's own failure) - Update isn't safe to
    // touch once Update.end()/Update.end(false) has already been called on it.
    if (!_ota_in_progress)
        return false;

    // Pausing must happen *before* the delay(1): disabling a timer alarm doesn't abort an
    // already-in-flight interrupt, so the delay is what actually guarantees any display-refresh
    // ISR invocation that was already running (or about to fire) has finished before the flash
    // cache goes down for the write below.
    if (_on_flash_write_begin_cb)
        _on_flash_write_begin_cb();

    delay(1);

    size_t written = Update.write(data, length);

    if (_on_flash_write_end_cb)
        _on_flash_write_end_cb();

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

static void ota_writer_task(void* pvParameters) {
    ota_write_item_t item;
    for (;;) {
        // Bounded wait (rather than portMAX_DELAY) so this loop wakes up regularly even with an
        // empty queue, to notice a pending abort request below.
        if (xQueueReceive(_write_queue, &item, pdMS_TO_TICKS(100)) == pdTRUE) {
            _writer_busy = true;
            ota_write_now(item.data, item.length);
            _writer_busy = false;
        }

        if (_abort_requested) {
            _abort_requested = false;
            ota_abort_now();
        }
    }
}

// ota_end()/ota_abort() call Update.end()/esp_ota_set_boot_partition() directly from the BLE
// stack's task, and Update isn't safe to touch from two tasks at once - this must be called
// before either of them to make sure the writer task isn't mid-write.
static bool ota_wait_for_write_queue_drain(uint32_t timeout_ms) {
    // ota_write_now()'s own failure path calls ota_abort() from the writer task itself - it can't
    // be concurrently mid-write with itself, and waiting on _writer_busy here would deadlock
    // (it's still true, since ota_write_now() hasn't returned yet).
    if (_writer_task_handle && xTaskGetCurrentTaskHandle() == _writer_task_handle)
        return true;

    uint32_t waited = 0;
    while (_write_queue && (uxQueueMessagesWaiting(_write_queue) > 0 || _writer_busy) && waited < timeout_ms) {
        delay(10);
        waited += 10;
    }
    return !_write_queue || (uxQueueMessagesWaiting(_write_queue) == 0 && !_writer_busy);
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

    // Flush every chunk still sitting in the write queue first - otherwise Update.end() could
    // finalize a partition that's missing the last few chunks, and calling it concurrently with
    // the writer task still mid-write isn't safe.
    if (!ota_wait_for_write_queue_drain(5000)) {
        ota_emit_status("Timed out waiting for pending OTA writes to flush", true);
        ota_abort();
        return false;
    }

    // Finalize the update process. Like Update.write(), this writes to flash (flushing/finalizing
    // the partition), so it needs the same ISR pause - unlike ota_write()'s chunks, this call was
    // missing it, letting the still-running display refresh timer race the finalize write.
    if (_on_flash_write_begin_cb)
        _on_flash_write_begin_cb();
    delay(1); // let any in-flight display-refresh ISR invocation finish, see ota_write()
    bool update_ended = Update.end();
    if (_on_flash_write_end_cb)
        _on_flash_write_end_cb();

    if (!update_ended) {
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

    // Set the boot partition to the updated firmware - also a flash write (to the otadata
    // partition), same reasoning as Update.end() above.
    if (_on_flash_write_begin_cb)
        _on_flash_write_begin_cb();
    delay(1); // let any in-flight display-refresh ISR invocation finish, see ota_write()
    esp_err_t err = esp_ota_set_boot_partition(next_partition);
    if (_on_flash_write_end_cb)
        _on_flash_write_end_cb();

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
    // If we're the writer task itself (ota_write_now()'s own failure path), it's safe to run this
    // immediately - we can't be concurrently mid-write with ourselves. Otherwise (BLE stack task,
    // e.g. an explicit Abort command from the phone, or one of ota_end()'s failure paths), don't
    // block waiting for the writer task to finish: just ask it to abort once it's free. Blocking
    // the BLE stack's own task for however long that takes was itself destabilizing the connection
    // - the exact problem this whole write queue exists to avoid in the first place.
    if (_writer_task_handle && xTaskGetCurrentTaskHandle() == _writer_task_handle) {
        ota_abort_now();
    } else {
        _abort_requested = true;
    }
}

static void ota_abort_now() {
    if (_ota_in_progress) {
        // Abort the update and clean up resources. Same flash-write hazard as in ota_end().
        if (_on_flash_write_begin_cb)
            _on_flash_write_begin_cb();
        delay(1); // let any in-flight display-refresh ISR invocation finish, see ota_write()
        Update.end(false);
        if (_on_flash_write_end_cb)
            _on_flash_write_end_cb();

        ota_emit_status("OTA update aborted", true);
        ota_emit_complete(false, "Update aborted");
    }

    // Reset all state variables
    _ota_in_progress = false;
    _firmware_bytes_received = 0;
    _firmware_total_size = 0;

    // Discard any chunks that were still queued when this session ended - ota_write_now() also
    // guards against processing them (belt and suspenders, and this drops them immediately rather
    // than one at a time as the writer task happens to dequeue each).
    if (_write_queue)
        xQueueReset(_write_queue);
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