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
 * @param table_24 array of BRIGHTNESS_SCHEDULE_HOURS brightness nibbles (0..15), index = hour of day (0..23).
 * @return true if saving successfully done, otherwise false.
 */
bool brightness_schedule_save(const uint8_t table_24[BRIGHTNESS_SCHEDULE_HOURS]);

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
