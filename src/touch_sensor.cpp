#include "touch_sensor.h"

TouchSensor::TouchSensor(uint8_t touch_pin, touchCallback_t callback, touch_value_t threshold)
{
    touchAttachInterrupt(touch_pin, callback, threshold);
}

void TouchSensor::Unload()
{
    touchDetachInterrupt(_touch_pin);
}
