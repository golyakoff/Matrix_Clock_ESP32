#include <stdint.h>
#include <Wire.h>
#include <ErriezDS3231.h>

#include "rtc.h"

ErriezDS3231 rtc;

struct tm get_build_tm();

bool rtc_init()
{
    // Setup DS3231 01101000
    Wire.begin(RTC_SDA, RTC_SCL, RTC_SPEED);

    delay(1000);

    bool ok = true;

    // Initialize RTC
    ok &= rtc.begin();

    // For DEBUG purposes only!
    // Uncomment if you wish to initialize RTC from build date and time.
    //const struct tm build_tm = get_build_tm();
    //ok &= rtc.write(&build_tm);

    // Enable 1Hz SQW
    ok &= rtc.setSquareWave(SquareWave1Hz);

    return ok;
}

bool rtc_get_time(struct tm *dt_out)
{
    return rtc.read(dt_out);
}

bool rtc_set_time(const struct tm *dt_new)
{
    return rtc.write(dt_new);
}

bool rtc_get_temperature(int8_t *temperature, uint8_t *fraction)
{
    return rtc.getTemperature(temperature, fraction);
}

struct tm get_build_tm()
{
    char s_month[5];
    int year;
    struct tm t;
    static const char month_names[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    sscanf(__DATE__, "%s %d %d", s_month, &t.tm_mday, &year);
    sscanf(__TIME__, "%2d %*c %2d %*c %2d", &t.tm_hour, &t.tm_min, &t.tm_sec);
    // Find where is s_month in month_names. Deduce month value.
    t.tm_mon = (strstr(month_names, s_month) - month_names) / 3 + 1;    
    t.tm_year = year - 1900;

    // Add period before build started and flashing is done
    t.tm_sec += 28;

    // Normalize time
    mktime(&t);
    return t;
}