#pragma once

#include "time.h"
#include "stdbool.h"

/**
 * @brief Compares two time structures.
 * 
 * @param dt1 the first time to compare
 * @param dt2 the second time to compare
 * @return true if seconds, minutes, hours, days, month numbers or years differ, otherwise false.
 */
bool times_are_different(const struct tm* dt1, const struct tm* dt2);

/**
 * @brief Prints the formatted time string to console with some prefix.
 * 
 * @param tag ESP32 logging tag
 * @param prefix to write before the formatted time string
 * @param dt time structure
 */
void log_tm(const char* tag, const char *prefix, const struct tm *dt);
