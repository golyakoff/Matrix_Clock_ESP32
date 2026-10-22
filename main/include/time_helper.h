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
 * @brief Seconds since 1970-01-01 00:00:00 of the calendar fields, as if they were UTC: like timegm().
 *        The clock keeps local time with no time zone, while mktime() would shift it by the zone (TZ is
 *        set for the time from the internet) and by daylight saving time.
 *
 * @param dt time structure; fields out of their ranges (61 seconds, the 13th month...) are carried over
 * @return the seconds
 */
time_t tm_to_seconds(const struct tm *dt);

/**
 * @brief Brings the fields of a time structure into their ranges (61 seconds -> the next minute 01) and
 *        sets the day of the week and of the year, independently of the time zone: see tm_to_seconds().
 *
 * @param dt time structure to normalize
 */
void tm_normalize(struct tm *dt);

/**
 * @brief Prints the formatted time string to console with some prefix.
 * 
 * @param tag ESP32 logging tag
 * @param prefix to write before the formatted time string
 * @param dt time structure
 */
void log_tm(const char* tag, const char *prefix, const struct tm *dt);
