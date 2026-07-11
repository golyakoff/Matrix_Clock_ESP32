#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Number of hourly brightness slots (0-23).
 */
#define BRIGHTNESS_SCHEDULE_HOURS (24U)

/**
 * @brief Initializes the hourly brightness schedule from NVS (ESP32 flash).
 *        The DS3231 RTC chip has no spare non-volatile storage left for a 24-entry
 *        table (see the memory map comment in rtc.h), so this schedule is kept in
 *        the ESP32's own "nvs" flash partition instead, which is untouched by OTA
 *        updates (app0/app1 swap).
 *
 *        If no schedule was saved before, a default one is generated and saved.
 *
 * @return true if initialization successfully done, otherwise false.
 */
bool brightness_schedule_init();

/**
 * @brief Saves the new hourly brightness table into NVS.
 *
 *        WARNING: writing NVS disables the flash cache for the duration of the erase/write.
 *        Any interrupt that runs code located in flash (e.g. the PxMatrix display timer ISR)
 *        will panic with "Cache disabled but cached memory region accessed" if it fires in that
 *        window. Only call this while such interrupts are stopped, or use
 *        brightness_schedule_set() + brightness_schedule_flush() instead.
 *
 * @param table_24 array of BRIGHTNESS_SCHEDULE_HOURS brightness nibbles (0..15), index = hour of day (0..23).
 * @return true if saving successfully done, otherwise false.
 */
bool brightness_schedule_save(const uint8_t table_24[BRIGHTNESS_SCHEDULE_HOURS]);

/**
 * @brief Updates the in-memory hourly brightness table and marks it as pending for an NVS write.
 *        Touches RAM only, so it is safe to call from a BLE callback: the new values take effect
 *        on the matrix immediately, while the flash write is deferred to brightness_schedule_flush().
 *
 * @param table_24 array of BRIGHTNESS_SCHEDULE_HOURS brightness nibbles (0..15), index = hour of day (0..23).
 */
void brightness_schedule_set(const uint8_t table_24[BRIGHTNESS_SCHEDULE_HOURS]);

/**
 * @brief Gets the value indicating whether the in-memory table differs from the one stored in NVS,
 *        i.e. whether brightness_schedule_flush() has to be called.
 *
 * @return true if there is a pending table waiting to be written into NVS, otherwise false.
 */
bool brightness_schedule_is_dirty();

/**
 * @brief Writes the pending table (see brightness_schedule_set()) into NVS. Does nothing if there
 *        is nothing pending.
 *
 *        WARNING: see the flash cache note on brightness_schedule_save(). Call this from the main
 *        loop only, with the display timers stopped (matrix_pause_timers()).
 *
 * @return true if there was nothing to write or the write succeeded, otherwise false.
 */
bool brightness_schedule_flush();

/**
 * @brief Loads the current hourly brightness table from the in-memory cache.
 *
 * @param table_out_24 pointer to the memory (BRIGHTNESS_SCHEDULE_HOURS bytes) for storing the table.
 */
void brightness_schedule_load(uint8_t table_out_24[BRIGHTNESS_SCHEDULE_HOURS]);

/**
 * @brief Gets the brightness nibble (0..15) configured for the given hour of day.
 *
 * @param hour hour of day, values outside of 0..23 are wrapped with modulo 24.
 * @return brightness nibble value (0..15) for the given hour.
 */
uint8_t brightness_schedule_get_for_hour(uint8_t hour);
