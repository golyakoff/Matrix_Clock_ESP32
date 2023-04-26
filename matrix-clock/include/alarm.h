#pragma once

#include "stdint.h"
#include "stdbool.h"
#include "time.h"

/**
 * @brief Type of a function used for callback function when alarm should fire.
 */
typedef void (*alarmCallback_t)(const uint8_t hours, const uint8_t minutes);

/**
 * @brief RTC Alarm class.
 */
class Alarm
{
    public:
        /**
         * @brief Construct a new Alarm object
         * 
         * @param callback is the callback alarm function.
         *                 It calls when hours and minutes match the time passed in tick() method.
         */
        Alarm(alarmCallback_t callback);

        /**
         * @brief Sets the hours, minutes, and state (active/incative) of the alaram.
         * 
         * @param hours is the integer hours value (0-23).
         * @param minutes is the integer minutes value (0-59).
         * @param active is the state, true for active, otherwise false.
         */
        void set(const uint8_t hours, const uint8_t minutes, const bool active);

        /**
         * @brief Gets the actual alaram settings of the alarm: hours, minutes, and state.
         * 
         * @param hours the pointer to the memory for storing the integer hours value (0-23).
         * @param minutes the pointer to the memory for storing the integer minutes value (0-59).
         * @param active the pointer to the memory for storing the boolean value of the state.
         *               (true for active, otherwise false).
         */
        void get(uint8_t *hours, uint8_t *minutes, bool *active);

        /**
         * @brief Method for checking if the passed time structure matches the hours and minutes.
         *        If it is and the state is active, then the callback() method
         *        passed to constructor is calling. Otherwise nothing happens.
         * 
         * @param dt Time structure to compare the value of hours and minutes with the set up alarm time.
         */
        void tick(const struct tm *dt);
    private:

        /**
         * @brief Hours integer value (0-23).
         */
        uint8_t _hours;

        /**
         * @brief Minutes integer value (0-23).
         */
        uint8_t _minutes;

        /**
         * @brief Active state boolean value.
         */
        bool _active;

        /**
         * @brief Callback to call when there will be a time for alarm.
         */
        alarmCallback_t _callback;

        /**
         * @brief As we have ability to call the tick() method more ofthen than once a second,
         *        and have to call the callback() only the first time when time is matches,
         *        then we have to store the flag indicating that we already fired the alaram
         *        for the first time. The next times for the same hours and minutes it will be skipped.
         *        The flag will be automatically reset after the first time tick() call with the
         *        different hours and minutes in the passed time structure.
         */
        bool _alreadyFired;
};
