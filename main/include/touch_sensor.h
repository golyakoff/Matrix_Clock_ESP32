#pragma once

#include <Arduino.h>

typedef void (*touchCallback_t)();

class TouchSensor
{
private:
    /**
     * @brief GPIO touch pin.
     */
    uint8_t _touch_pin;

    /**
     * @brief The function called on touch (and on release, see IsPressed()).
     */
    touchCallback_t _callback;

    /**
     * @brief Touch threshold: drop of the pad reading below its untouched level, percent.
     */
    float _threshold_percent;
public:
    /**
     * @brief Initializes a new TouchSensor object. The sensor starts working on Init().
     * 
     * @param touch_pin GPIO pin number.
     * @param callback A method that should be called when the sensor is touched or released
     *        (check IsPressed() in it). Called from an ISR.
     * @param threshold_percent How much the pad reading must drop below its untouched level
     *        (measured on Init()) to count as a touch, percent.
     */
    TouchSensor(uint8_t touch_pin, touchCallback_t callback, float threshold_percent);

    /**
     * @brief Calibrates the pad and enables the ISR. Call from setup(), with the pad untouched.
     */
    void Init();

    /**
     * @brief Whether the last ISR was a touch (true) or a release (false).
     */
    bool IsPressed();

    /**
     * @brief Disables ISR.
     */
    void Unload();
};
