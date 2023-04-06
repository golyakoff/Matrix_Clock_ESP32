#pragma once

#include "stdint.h"
#include "stdbool.h"
#include "time.h"

typedef void (*alarmCallback_t)(const uint8_t hours, const uint8_t minutes);

class Alarm
{
    public:
        Alarm(alarmCallback_t callback);
        void set(const uint8_t hours, const uint8_t minutes, const bool active);
        void get(uint8_t *hours, uint8_t *minutes, bool *active);
        void tick(const struct tm *dt);
    private:
        uint8_t _hours;
        uint8_t _minutes;
        bool _active;
        alarmCallback_t _callback;
        bool _alreadyFired;
};
