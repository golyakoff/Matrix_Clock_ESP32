#include "touch_sensor.h"

TouchSensor::TouchSensor(uint8_t touch_pin, touchCallback_t callback, float threshold_percent)
    : _touch_pin(touch_pin), _callback(callback), _threshold_percent(threshold_percent)
{
}

void TouchSensor::Init()
{
    // The threshold is relative to the pad's own level measured at attach time (its "benchmark"):
    // the pad reads lower when touched, and the ISR fires when it drops by this many percent.
    touchSetDefaultThreshold(_threshold_percent);
    touchAttachInterrupt(_touch_pin, _callback, 0);
}

bool TouchSensor::IsPressed()
{
    return touchInterruptGetLastStatus(_touch_pin);
}

void TouchSensor::Unload()
{
    touchDetachInterrupt(_touch_pin);
}
