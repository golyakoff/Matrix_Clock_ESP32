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
//          whether it is active or not?
//
//          * 4 highest bits: Reserved for future.
//
// 09h:     Lowest 4 bits are used to describe the manual constant brightness
//          0 means the minimum visible brightness
//          15 means the maximum visible brightness
//
//          The next 1 bit is used for saving the value defining whether
//          the auto brightness adjustment feature is enabled (1) or
//          the constant manual value should ve used instead (0).
//
//          The next 1 bit is used to store the LED matrix color order:
//          0 = RRGGBB (default), 1 = RRBBGG i.e. green/blue swapped, red unchanged
//          (see RealTimeClock::saveColorOrder()/loadColorOrder()).
//
//          * 2 highest bits: Reserved for future.
//
// 0Ah:     * All 8 bits:  Reserved for future.
//
// OBh-0Ch: Lowest 11 bits starting from DS3231_REG_ALARM1_SEC are used to store
//          "on" alarm time as an integer value equals to the 
//          total number of minutes: 0-1439.
//
//          The next 1 bit is used to store the status of "on" alarm:
//          whether it is active or not?
//
//          * 4 highest bits: Reserved for future.
//
// 0Dh:     * All 8 bits:  Reserved for future.
//
// Uncomment the line below for timer initialization
//#define DEBUG_RTC_MEMORY

/**
 * @brief Alarm index used to choose the memory slot: On/Off.
 */
typedef enum {
    rtc_alarm_index_on = 1,
    rtc_alarm_index_off = 2
} rtc_alarm_index_t;

/**
 * @brief Real time clock wrapper with the minimum required function set.
 */
class RealTimeClock
{
    public:
        /**
         * @brief Initializes the new instance.
         * 
         * @param sdaPin I2C Data GPIO
         * @param sclPin I2C Clock GPIO 
         * @param frequency I2C frequency
         * @return true if initialization successfully done, otherwise false
         */
        bool init(int sdaPin, int sclPin, uint32_t frequency);

         /**
         * @brief Get crystal aging offset value.
         * 
         * It provides an 8-bit code to add to the codes in the capacitance array 
         * registers. The code is encoded in two’s complement. One LSB represents one small 
         * capacitor to be switched in or out of the capacitance array at the crystal pins.
         * The offset register is added to the capacitance array register under the following 
         * conditions: during a normal temperature conversion, if the temperature changes from 
         * the previous conversion, or during a manual user conversion (setting the CONV bit).
         * 
         * @return 8-bit code of aging offset register.
         */
        int8_t getAgingOffset();

        /**
         * @brief Set the Aging Offset value
         * 
         * The aging offset register capacitance value is added or subtracted from the
         * capacitance value that the device calculates for each temperature compensation.
         * 
         * Positive aging values add capacitance to the array,slowing the oscillator frequency.
         * Negative values remove capacitance from the array, increasing the oscillator frequency.
         * 
         * @param agingOffset
         * @return true if saving successfully done, otherwise false
         */
        bool setAgingOffset(int8_t agingOffset);

        /**
         * @brief Get the internal temperature of the RTC chip (Celsius degrees)
         * 
         * @param temperature the pointer to the memory for storing the integer part of temperature.
         * @param fraction the pointer to the memory for storing the fraction part of temperature.
         * @return true if reading successfully done, otherwise false
         */
        bool getTemperature(int8_t *temperature, uint8_t *fraction);

        /**
         * @brief Get the current time structure from RTC.
         * 
         * @param dt_out the pointer to the memory for storing for storing the read value.
         * @return true if reading successfully done, otherwise false
         */
        bool getTime(struct tm *dt_out);

        /**
         * @brief Set the new time in RTC
         * 
         * @param dt_new the value of the new time
         * @return true if writing successfully done, otherwise false
         */
        bool setTime(const struct tm *dt_new);

        /**
         * @brief Saves the alarm time into one of two memory slots (depends on index).
         * 
         * @param index On/Off RTC index
         * @param hours the integer value of hours (0-23)
         * @param minutes the integer value of minutes (0-59)
         * @param active the state of saved alarm (active/inactive)
         * @return true if saving successfully done, otherwise false
         */
        bool saveAlarm(rtc_alarm_index_t index, uint8_t hours, uint8_t minutes, bool active);

        /**
         * @brief Loads the alarm from one of two memory slots (depends on index).
         * 
         * @param index On/Off RTC index
         * @param hours the pointer to the memory for storing the integer value of hours (0-23)
         * @param minutes  the pointer to the memory for storing the integer value of minutes (0-59)
         * @param active  the pointer to the memory for storing the state of saved alarm (active/inactive)
         * @return true if loading successfully done, otherwise false
         */
        bool loadAlarm(rtc_alarm_index_t index, uint8_t *hours, uint8_t *minutes, bool *active);

        /**
         * @brief Saves the brightness settings into memory
         * 
         * @param use_auto_brightness whether we should use auto-adjusted brightness value depending on light resistor or not?
         * @param manual_brightness_value the value of the manually set brightness (0-15),
         *        used when use_auto_brightness = false.
         * @return true if saving successfully done, otherwise false
         */
        bool saveBrightness(bool use_auto_brightness, uint8_t manual_brightness_value);

        /**
         * @brief Saves the brightness settings into memory
         * 
         * @param use_auto_brightness the pointer to the memory for storing the value define
         *        whether we should use auto-adjusted brightness value depending on light resistor or not?
         * @param manual_brightness_value the pointer to the memory for storing the value define
         *        the value of the manually set brightness (0-15), used when use_auto_brightness = false.
         * @return true if saving successfully done, otherwise false
         */
        bool loadBrightness(bool *use_auto_brightness, uint8_t *manual_brightness_value);

        /**
         * @brief Saves the LED matrix color order setting into memory.
         *
         * @param use_rrbbgg whether the matrix should be driven with the RRBBGG pixel order (true)
         *        instead of the default RRGGBB (false).
         * @return true if saving successfully done, otherwise false
         */
        bool saveColorOrder(bool use_rrbbgg);

        /**
         * @brief Loads the LED matrix color order setting from memory.
         *
         * @param use_rrbbgg the pointer to the memory for storing the value defining whether
         *        the matrix should be driven with the RRBBGG pixel order (true) instead of
         *        the default RRGGBB (false).
         * @return true if loading successfully done, otherwise false
         */
        bool loadColorOrder(bool *use_rrbbgg);

    private:
        /**
         * @brief internal RTC object
         * 
         */
        ErriezDS3231 _rtc;

        #ifdef DEBUG_RTC_MEMORY
        void testAlarmSaveLoad(rtc_alarm_index_t index, uint8_t hours, uint8_t minutes, bool active);
        void testBrightnessSaveLoad(bool use_auto_brightness, uint8_t manual_brightness_value);
        #endif
};
