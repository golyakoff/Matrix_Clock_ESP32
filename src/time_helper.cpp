#include "HardwareSerial.h"

#include "time_helper.h"
#include <esp_log.h>

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

void log_tm(const char* tag, const char *prefix, const struct tm *dt)
{
    ESP_LOGI(
        tag,
        "%s: %d-%02d-%02d %02d:%02d:%02d",
        prefix,
        dt->tm_year + 1900,
        dt->tm_mon,
        dt->tm_mday,
        dt->tm_hour,
        dt->tm_min,
        dt->tm_sec);
}
