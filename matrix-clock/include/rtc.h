#ifndef __RTC_H__
#define __RTC_H__

#include <time.h>

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
// 0Dh:     1 lowest bit is used to store "show" flag: the value indicating wheither
//          display was manually switched off (0) or not (1)
//
//          * 7 highest bits: Reserved for future.
//
// Uncomment the line below for include memory "unit "
//#define DEBUG_RTC_MEMORY

#define RTC_SPEED   (400000U)
#define RTC_SCL     (26U)
#define RTC_SDA     (27U)

#define RTC_SQW     (35U)

typedef enum {
    alarm_index_on = 1,
    alarm_index_off = 2
} alarm_index_t;

bool rtc_init();
bool rtc_get_time(struct tm *dt_out);
bool rtc_set_time(const struct tm *dt_new);
bool rtc_get_temperature(int8_t *temperature, uint8_t *fraction);

bool rtc_memory_set_alarm(alarm_index_t index, uint8_t hours, uint8_t minutes, bool is_active);
bool rtc_memory_get_alarm(alarm_index_t index, uint8_t *hours, uint8_t *minutes, bool *is_active);

bool rtc_memory_set_bright(bool use_auto_brightness, uint8_t manual_brightness_value);
bool rtc_memory_get_bright(bool *use_auto_brightness, uint8_t *manual_brightness_value);

bool rtc_memory_set_show(bool show);
bool rtc_memory_get_show(bool *show);

#endif // __RTC_H__
