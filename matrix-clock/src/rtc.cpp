#include <stdint.h>
#include <Wire.h>
#include <ErriezDS3231.h>

#include "rtc.h"

ErriezDS3231 rtc;

#ifdef DEBUG_RTC_EEPROM
#include <HardwareSerial.h>
#endif

struct tm get_build_tm();

#ifdef DEBUG_RTC_EEPROM
void rtc_eeprom_alarm_test(alarm_index_t index, uint8_t hours, uint8_t minutes, bool is_active);
void rtc_eeprom_bright_test(bool use_auto_brightness, uint8_t manual_brightness_value);
void rtc_eeprom_show_test(bool show);
#endif // DEBUG_RTC_EEPROM

#define RTC_EEPROM_TOTAL_MINUTES_MASK   0b0000011111111111
#define RTC_EEPROM_IS_ACTIVE_MASK       0b0000100000000000
#define RTC_EEPROM_MANUAL_BRIGHT_MASK           0b00001111
#define RTC_EEPROM_IS_AUTO_BRIGHT_MASK          0b00010000
#define RTC_EEPROM_SHOW_MASK                    0b00000001

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

#ifdef DEBUG_RTC_EEPROM
    // RTC alarm tests
    rtc_eeprom_alarm_test(alarm_index_off, 23, 59, true);
    rtc_eeprom_alarm_test(alarm_index_off, 4, 12, false);
    rtc_eeprom_alarm_test(alarm_index_off, 12, 00, true);

    rtc_eeprom_alarm_test(alarm_index_on, 8, 35, false);
    rtc_eeprom_alarm_test(alarm_index_on, 11, 59, true);
    rtc_eeprom_alarm_test(alarm_index_on, 21, 22, false);

    rtc_eeprom_bright_test(true, 10);
    rtc_eeprom_bright_test(false, 15);
    rtc_eeprom_bright_test(true, 0);

    rtc_eeprom_show_test(true);
    rtc_eeprom_show_test(false);
#endif // DEBUG_RTC_EEPROM

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

bool rtc_memory_set_alarm(alarm_index_t index, uint8_t hours, uint8_t minutes, bool is_active)
{
    uint8_t reg_addr;
    switch (index)
    {
        case alarm_index_off:
            reg_addr = DS3231_REG_ALARM1_SEC;
            break;
        case alarm_index_on:
            reg_addr = DS3231_REG_ALARM2_MIN;
            break;
        default:
            return false;
            break;
    }

    uint16_t buffer = minutes + 60 * hours + (is_active ? RTC_EEPROM_IS_ACTIVE_MASK : 0);
    return rtc.writeBuffer(reg_addr, &buffer, sizeof(buffer));
}

bool rtc_memory_get_alarm(alarm_index_t index, uint8_t *hours, uint8_t *minutes, bool *is_active)
{
    uint8_t reg_addr;
    switch (index)
    {
        case alarm_index_off:
            reg_addr = DS3231_REG_ALARM1_SEC;
            break;
        case alarm_index_on:
            reg_addr = DS3231_REG_ALARM2_MIN;
            break;
        default:
            return false;
            break;
    }

    uint16_t buffer;
    if (!rtc.readBuffer(reg_addr, &buffer, sizeof(buffer)))
        return false;

    *is_active = (buffer & RTC_EEPROM_IS_ACTIVE_MASK) > 0;
    *minutes = (buffer & RTC_EEPROM_TOTAL_MINUTES_MASK) % 60;
    *hours = (buffer & RTC_EEPROM_TOTAL_MINUTES_MASK) / 60;

    return true;
}

bool rtc_memory_set_bright(bool use_auto_brightness, uint8_t manual_brightness_value)
{
    uint8_t buffer = 
        (manual_brightness_value & RTC_EEPROM_MANUAL_BRIGHT_MASK) +
        (use_auto_brightness ? RTC_EEPROM_IS_AUTO_BRIGHT_MASK : 0);

    return rtc.writeBuffer(DS3231_REG_ALARM1_HOUR, &buffer, sizeof(buffer));
}

bool rtc_memory_get_bright(bool *use_auto_brightness, uint8_t *manual_brightness_value)
{
    uint8_t buffer;
    if (!rtc.readBuffer(DS3231_REG_ALARM1_HOUR, &buffer, sizeof(buffer)))
        return false;

    *use_auto_brightness = (buffer & RTC_EEPROM_IS_AUTO_BRIGHT_MASK) > 0;
    *manual_brightness_value = (buffer & RTC_EEPROM_MANUAL_BRIGHT_MASK);

    return true;
}

bool rtc_memory_set_show(bool show)
{
    uint8_t buffer = (show ? RTC_EEPROM_SHOW_MASK : 0);
    return rtc.writeBuffer(DS3231_REG_ALARM2_DD, &buffer, sizeof(buffer));
}

bool rtc_memory_get_show(bool *show)
{
    uint8_t buffer;
    if (!rtc.readBuffer(DS3231_REG_ALARM2_DD, &buffer, sizeof(buffer)))
        return false;

    *show = (buffer & RTC_EEPROM_SHOW_MASK) > 0;

    return true;
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

#ifdef DEBUG_RTC_EEPROM
// RTC init with debug messages
void rtc_eeprom_alarm_test(alarm_index_t index, uint8_t hours, uint8_t minutes, bool is_active)
{
    Serial.printf("RTC EEPROM alarm %d test: write and read alarm time %02d:%02d, is_active:%d ", index, hours, minutes, is_active);

    uint8_t actual_hours = 0;
    uint8_t actual_minutes = 0;
    bool actual_is_active = false;

    if(!rtc_memory_set_alarm(index, hours, minutes, is_active))
    {
        Serial.println(F("-> Error: rtc_memory_set_alarm()!"));
        for(;;) { delay(5000); }
    }

    if(!rtc_memory_get_alarm(index, &actual_hours, &actual_minutes, &actual_is_active))
    {
        Serial.println(F("-> Error: rtc_memory_get_alarm()"));
        for(;;) { delay(5000); }
    }

    if (hours != actual_hours || minutes != actual_minutes || is_active != actual_is_active)
    {
        Serial.printf(
            "->Error: actual hours = %d, actial minutes = %d, actual is_active = %d \n",
            actual_hours,
            actual_minutes,
            is_active);
        for(;;) { delay(5000); }
    }

    Serial.println(F("OK"));
}

void rtc_eeprom_bright_test(bool use_auto_brightness, uint8_t manual_brightness_value)
{
    Serial.printf(
        "RTC EEPROM bright test: use_auto_brightness = %d, manual_brightness_value = %d ",
        use_auto_brightness,
        manual_brightness_value);

    bool actual_use_auto_brightness = false;
    uint8_t actual_manual_brightness_value = 0;

    if(!rtc_memory_set_bright(use_auto_brightness, manual_brightness_value))
    {
        Serial.println(F("-> Error: rtc_memory_set_bright()!"));
        for(;;) { delay(5000); }
    }

    if(!rtc_memory_get_bright(&actual_use_auto_brightness, &actual_manual_brightness_value))
    {
        Serial.println(F("-> Error: rtc_memory_get_alarm()"));
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
void rtc_eeprom_show_test(bool show)
{
    Serial.printf("RTC EEPROM show test: show = %d ", show);

    bool actual_show = false;

    if(!rtc_memory_set_show(show))
    {
        Serial.println(F("-> Error: rtc_memory_set_show()!"));
        for(;;) { delay(5000); }
    }

    if(!rtc_memory_get_show(&actual_show))
    {
        Serial.println(F("-> Error: rtc_memory_get_show()"));
        for(;;) { delay(5000); }
    }

    if (show != actual_show)
    {
        Serial.printf("->Error: actual show = %d \n", show);
        for(;;) { delay(5000); }
    }

    Serial.println(F("OK"));
}
#endif // DEBUG_RTC_EEPROM