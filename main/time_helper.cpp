#include "HardwareSerial.h"

#include "time_helper.h"
#include <esp_log.h>
#include <stdint.h>

bool times_are_different(const struct tm* dt1, const struct tm* dt2)
{
    return 
        dt1->tm_sec != dt2->tm_sec ||
        dt1->tm_min != dt2->tm_min ||
        dt1->tm_hour != dt2->tm_hour ||
        dt1->tm_mday != dt2->tm_mday ||
        dt1->tm_mon != dt2->tm_mon ||
        dt1->tm_year != dt2->tm_year;
}

// Days since 1970-01-01 of a proleptic Gregorian date, month 1..12 (H. Hinnant's days_from_civil)
static int64_t days_from_civil(int64_t y, int64_t m, int64_t d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

time_t tm_to_seconds(const struct tm *dt)
{
    int64_t year = 1900LL + dt->tm_year + dt->tm_mon / 12;
    int64_t mon = dt->tm_mon % 12;
    if (mon < 0)
    {
        mon += 12;
        year--;
    }
    int64_t days = days_from_civil(year, mon + 1, 1) + dt->tm_mday - 1;
    return (time_t)(days * 86400 + dt->tm_hour * 3600LL + dt->tm_min * 60LL + dt->tm_sec);
}

void tm_normalize(struct tm *dt)
{
    time_t seconds = tm_to_seconds(dt);
    gmtime_r(&seconds, dt);
}

void log_tm(const char* tag, const char *prefix, const struct tm *dt)
{
    ESP_LOGI(
        tag,
        "%s: %d-%02d-%02d %02d:%02d:%02d",
        prefix,
        dt->tm_year + 1900,
        dt->tm_mon + 1,
        dt->tm_mday,
        dt->tm_hour,
        dt->tm_min,
        dt->tm_sec);
}
