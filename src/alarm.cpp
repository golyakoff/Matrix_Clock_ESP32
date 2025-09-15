#include "alarm.h"
#include "HardwareSerial.h"
#include <esp_log.h>

static const char* TAG = "ALARM";

Alarm::Alarm(alarmCallback_t callback)
{
    _callback = callback;
    _hours = 0;
    _minutes = 0;
    _active = false;
    _alreadyFired = false;  // We already called callback for these hour and minute
                            // and should not repeat it until the time has been changed.
                            // We will reset the flag when time is changed.
}

void Alarm::set(uint8_t hours, uint8_t minutes, bool active)
{
    _hours = hours;
    _minutes = minutes;
    _active = active;
    _alreadyFired = false;
    
    #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG,
        "Called set(hours=%02d, minutes=%02d, active=%s)", 
        hours,
        minutes,
        active ? "true" : "false");
    #endif
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

    // We will be here just the first time when hours and minutes are the same.
    if (dt->tm_hour == _hours && dt->tm_min == _minutes && !_alreadyFired)
    {
        // That is why we have to set "_alreadyFired" flag to not fire the callback
        // each tick during this minute
        _alreadyFired = true;
        if (_callback != nullptr)
        {
            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "Calling _callback (%02d:%02d)...", _hours, _minutes);
            #endif

            _callback(_hours, _minutes);
            
            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "OK (callback called)");
            #endif
        }
    }
    
    // When hours or minutes have been changed...
    if ((dt->tm_hour != _hours || dt->tm_min != _minutes) && _alreadyFired)
    {
        // We have to reset the "_alreadyFired" flag to allow the callback
        // to be called the first next time when the alarm time will be the
        // similar to the current time.
        _alreadyFired = false;
    }
}
