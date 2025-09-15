#pragma once

#include "stdint.h"
#include "esp32-hal-touch.h"

/**
 * @brief Type of a function used for callback function when sensor touched.
 */
typedef void (*touchCallback_t)();

/**
 * @brief Touch sensor class.
 * 
 */
class TouchSensor
{
private:
    /**
     * @brief GPIO touch pin.
     */
    uint8_t _touch_pin;
public:
    /**
     * @brief Initializes a new TouchSensor object.
     * 
     * @param touch_pin GPIO pin number.
     * @param callback A method that should be called when sensor touched.
     * @param threshold Values from touchRead below this threshold will be dropped.
     */
    TouchSensor(uint8_t touch_pin, touchCallback_t callback, touch_value_t threshold);

    /**
     * @brief Disables ISR.
     */
    void Unload();
};
