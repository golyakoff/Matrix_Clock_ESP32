#pragma once

#include <time.h>

#include <ErriezDS3231.h>

// I decided to store device's setting into the RTC memory used for alarms
// as I do not plan to use alarm's functionality here.
//
// Here is the memory usage map:
//
// 07h-08h: Lowest 11 bits starting from DS3231_REG_ALARM1_SEC are used to store 
//          "off" alarm time as an integer value equals to the 
//          total number of minutes: 0-1439 (11 bits is enough for 0-2 047).
//
//          The next 1 bit is used to store the status of "off" alarm:
//          wheither it is active or not?
//
//          * 4 highest bits: Reserved for future.
//
// 09h:     Lowest 4 bits are used to describe the manual constant brightness
//          of the display: the bigger number - the brighter screen shine.
//
//          The next 1 bit is used for saving the value defining wheither 
//          the auto brightness adjustment feature is enabled (1) or 
//          the constant manual value should ve used instead (0).
//
// 0Ah:     * All 8 bits:  Reserved for future.
//
// OBh-0Ch: Lowest 11 bits starting from DS3231_REG_ALARM1_SEC are used to store 
//          "on" alarm time as an integer value equals to the 
//          total number of minutes: 0-1439.
//
//          The next 1 bit is used to store the status of "on" alarm:
//          wheither it is active or not?
//
//          * 4 highest bits: Reserved for future.
//
// 0Dh:     * All 8 bits:  Reserved for future.
//
// Uncomment the line below for include memory "unit "
//#define DEBUG_RTC_MEMORY

typedef enum {
    rtc_alarm_index_on = 1,
    rtc_alarm_index_off = 2
} rtc_alarm_index_t;

class RealTimeClock
{
    public:
        bool init(int sdaPin, int sclPin, uint32_t frequency);
        bool getTemperature(int8_t *temperature, uint8_t *fraction);

        bool getTime(struct tm *dt_out);
        bool setTime(const struct tm *dt_new);

        bool saveAlarm(rtc_alarm_index_t index, uint8_t hours, uint8_t minutes, bool active);
        bool loadAlarm(rtc_alarm_index_t index, uint8_t *hours, uint8_t *minutes, bool *active);

        bool saveBrightness(bool use_auto_brightness, uint8_t manual_brightness_value);
        bool loadBrightness(bool *use_auto_brightness, uint8_t *manual_brightness_value);
    private:
        ErriezDS3231 _rtc;
        struct tm getBuildTime();
        #ifdef DEBUG_RTC_MEMORY
        void testAlarmSaveLoad(rtc_alarm_index_t index, uint8_t hours, uint8_t minutes, bool active);
        void testBrightnessSaveLoad(bool use_auto_brightness, uint8_t manual_brightness_value);
        #endif
};
