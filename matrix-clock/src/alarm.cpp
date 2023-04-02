#include "alarm.h"
#include "HardwareSerial.h"

Alarm::Alarm(alarmCallback_t callback)
{
    _callback = callback;
    _hours = 0;
    _minutes = 0;
    _active = false;
    _alreadyFired = false;  // We already called callback for this hour and minute
                            // and should not repeat it until the time has been changed.
                            // After time changed we reset the flag
}

void Alarm::set(uint8_t hours, uint8_t minutes, bool active)
{
    _hours = hours;
    _minutes = minutes;
    _active = active;
    _alreadyFired = false;
}

void Alarm::get(uint8_t *hours, uint8_t *minutes, bool *active)
{
    *hours = _hours;
    *minutes = _minutes;
    *active = _active;
}

void Alarm::tick(const tm *dt)
{
    if (!_active)
        return;

    Serial.printf("ALARM::tick() at %02d:%02d (wait for %02d:%02d)\n", dt->tm_hour, dt->tm_min, _hours, _minutes);

    if (dt->tm_hour == _hours && dt->tm_min == _minutes && !_alreadyFired)
    {
        Serial.printf("ALARM _callback(%02d:%02d)\n", _hours, _minutes);
        _alreadyFired = true;
        if (_callback != nullptr)
        {
            Serial.printf("ALARM _callback(%02d:%02d)\n", _hours, _minutes);
            _callback(_hours, _minutes);
        }
    }
    
    if ((dt->tm_hour != _hours || dt->tm_min != _minutes) && _alreadyFired)
    {
        Serial.println("ALARM _alreadyFired reset");
        // After time change we reset the flag
        _alreadyFired = false;
    }
}
