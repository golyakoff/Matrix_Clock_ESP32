#include <stdint.h>
#include <Wire.h>
#include <ErriezDS3231.h>

#include "rtc.h"

#ifdef DEBUG_RTC_MEMORY
#include <HardwareSerial.h>
#endif

#define RTC_MEMORY_TOTAL_MINUTES_MASK   0b0000011111111111
#define RTC_MEMORY_IS_ACTIVE_MASK       0b0000100000000000
#define RTC_MEMORY_MANUAL_BRIGHT_MASK           0b00001111
#define RTC_MEMORY_IS_AUTO_BRIGHT_MASK          0b00010000
#define RTC_MEMORY_SHOW_MASK                    0b00000001

bool RealTimeClock::init(int sdaPin, int sclPin, uint32_t frequency)
{
    // Setup DS3231 01101000
    Wire.begin(sdaPin, sclPin, frequency);

    delay(1000);

    bool ok = true;

    // Initialize RTC
    ok &= _rtc.begin();

    // For DEBUG purposes only!
    // Uncomment if you wish to initialize RTC from build date and time.
    //const struct tm build_tm = getBuildTime();
    //ok &= _rtc.write(&build_tm);

    // Enable 1Hz SQW
    ok &= _rtc.setSquareWave(SquareWave1Hz);

#ifdef DEBUG_RTC_MEMORY
    // RTC alarm tests
    testAlarmSaveLoad(rtc_alarm_index_off, 4, 12, false);
    testAlarmSaveLoad(rtc_alarm_index_off, 0,  1, true);

    testAlarmSaveLoad(rtc_alarm_index_on, 21, 22, false);
    testAlarmSaveLoad(rtc_alarm_index_on,  6,  0, true);

    testBrightnessSaveLoad(true,  10);
    testBrightnessSaveLoad(false, 15);
#endif // DEBUG_RTC_MEMORY

    return ok;
}

bool RealTimeClock::getTime(struct tm *dt_out)
{
    return _rtc.read(dt_out);
}

bool RealTimeClock::setTime(const struct tm *dt_new)
{
    return _rtc.write(dt_new);
}

bool RealTimeClock::getTemperature(int8_t *temperature, uint8_t *fraction)
{
    return _rtc.getTemperature(temperature, fraction);
}

bool RealTimeClock::saveAlarm(rtc_alarm_index_t index, uint8_t hours, uint8_t minutes, bool active)
{
    uint8_t reg_addr;
    switch (index)
    {
        case rtc_alarm_index_off:
            reg_addr = DS3231_REG_ALARM1_SEC;
            break;
        case rtc_alarm_index_on:
            reg_addr = DS3231_REG_ALARM2_MIN;
            break;
        default:
            return false;
            break;
    }

    uint16_t buffer = minutes + 60 * hours + (active ? RTC_MEMORY_IS_ACTIVE_MASK : 0);
    return _rtc.writeBuffer(reg_addr, &buffer, sizeof(buffer));
}

bool RealTimeClock::loadAlarm(rtc_alarm_index_t index, uint8_t *hours, uint8_t *minutes, bool *active)
{
    uint8_t reg_addr;
    switch (index)
    {
        case rtc_alarm_index_off:
            reg_addr = DS3231_REG_ALARM1_SEC;
            break;
        case rtc_alarm_index_on:
            reg_addr = DS3231_REG_ALARM2_MIN;
            break;
        default:
            return false;
            break;
    }

    uint16_t buffer;
    if (!_rtc.readBuffer(reg_addr, &buffer, sizeof(buffer)))
        return false;

    *active = (buffer & RTC_MEMORY_IS_ACTIVE_MASK) > 0;
    *minutes = (buffer & RTC_MEMORY_TOTAL_MINUTES_MASK) % 60;
    *hours = (buffer & RTC_MEMORY_TOTAL_MINUTES_MASK) / 60;

    return true;
}

bool RealTimeClock::saveBrightness(bool use_auto_brightness, uint8_t manual_brightness_value)
{
    uint8_t buffer = 
        (manual_brightness_value & RTC_MEMORY_MANUAL_BRIGHT_MASK) +
        (use_auto_brightness ? RTC_MEMORY_IS_AUTO_BRIGHT_MASK : 0);

    return _rtc.writeBuffer(DS3231_REG_ALARM1_HOUR, &buffer, sizeof(buffer));
}

bool RealTimeClock::loadBrightness(bool *use_auto_brightness, uint8_t *manual_brightness_value)
{
    uint8_t buffer;
    if (!_rtc.readBuffer(DS3231_REG_ALARM1_HOUR, &buffer, sizeof(buffer)))
        return false;

    *use_auto_brightness = (buffer & RTC_MEMORY_IS_AUTO_BRIGHT_MASK) > 0;
    *manual_brightness_value = (buffer & RTC_MEMORY_MANUAL_BRIGHT_MASK);

    return true;
}

struct tm RealTimeClock::getBuildTime()
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

#ifdef DEBUG_RTC_MEMORY
// RTC init with debug messages
void RealTimeClock::testAlarmSaveLoad(rtc_alarm_index_t index, uint8_t hours, uint8_t minutes, bool active)
{
    Serial.printf("RTC MEMORY alarm %d test: write and read alarm time %02d:%02d, active:%d ", index, hours, minutes, active);

    uint8_t actual_hours = 0;
    uint8_t actual_minutes = 0;
    bool actual_is_active = false;

    if(!saveAlarm(index, hours, minutes, active))
    {
        Serial.println(F("-> Error: saveAlarm()!"));
        for(;;) { delay(5000); }
    }

    if(!loadAlarm(index, &actual_hours, &actual_minutes, &actual_is_active))
    {
        Serial.println(F("-> Error: loadAlarm()"));
        for(;;) { delay(5000); }
    }

    if (hours != actual_hours || minutes != actual_minutes || active != actual_is_active)
    {
        Serial.printf(
            "->Error: actual hours = %d, actial minutes = %d, actual active = %d \n",
            actual_hours,
            actual_minutes,
            active);
        for(;;) { delay(5000); }
    }

    Serial.println(F("OK"));
}

void RealTimeClock::testBrightnessSaveLoad(bool use_auto_brightness, uint8_t manual_brightness_value)
{
    Serial.printf(
        "RTC MEMORY bright test: use_auto_brightness = %d, manual_brightness_value = %d ",
        use_auto_brightness,
        manual_brightness_value);

    bool actual_use_auto_brightness = false;
    uint8_t actual_manual_brightness_value = 0;

    if(!saveBrightness(use_auto_brightness, manual_brightness_value))
    {
        Serial.println(F("-> Error: saveBrightness()!"));
        for(;;) { delay(5000); }
    }

    if(!loadBrightness(&actual_use_auto_brightness, &actual_manual_brightness_value))
    {
        Serial.println(F("-> Error: loadAlarm()"));
        for(;;) { delay(5000); }
    }

    if (use_auto_brightness != actual_use_auto_brightness || manual_brightness_value != actual_manual_brightness_value)
    {
        Serial.printf(
            "->Error: actual use_auto_brightness = %d, actial manual_brightness_value = %d \n",
            actual_use_auto_brightness,
            actual_manual_brightness_value);

        for(;;) { delay(5000); }
    }

    Serial.println(F("OK"));
}

#endif // DEBUG_RTC_MEMORY