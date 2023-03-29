/*******************************************************************
    Tetris clock that fetches its time Using the EzTimeLibrary

    For use with the ESP32
 *                                                                 *
    Written by Andrei Golyakoff using the code of Brian Lough
 *******************************************************************/

// Standard Libraries
#include <Arduino.h>
#include <Wire.h>

// Additional Libraries
#include <ErriezDS3231.h>

// Project includes
#include "rtc.h"
#include "matrix.h"
#include "ble.h"

struct tm rtcDateTime;

unsigned long oneSecondLoopDue = 0;

void main_rtc_init();
void main_matrix_init();
void main_ble_init();

extern struct tm _matrixDateTime;
struct tm _matrixDateTime_prev;

void setup()
{
    Serial.begin(115200);
    
    main_rtc_init();
    main_ble_init();
    delay(500);
    main_matrix_init();
}

void loop()
{
    unsigned long now = millis();
    if (now > oneSecondLoopDue) {
        matrix_100hz_loop();
        oneSecondLoopDue = now + 10;

        if (_matrixDateTime.tm_sec != _matrixDateTime_prev.tm_sec)
        {
            ble_on_update_time_callback(&_matrixDateTime);
        }
    }
}

// RTC init with debug messages
void main_rtc_init()
{
    if (!rtc_init())
    {
        for(;;) {
            Serial.println("Error: rtc_init(). RTC was not initialized!");
            delay(5000);
        }
    }    
    Serial.println("rtc_init(): OK");
    
    // Write out RTC chip temperature
    int8_t rtc_t;
    uint8_t rtc_f;
    if (!rtc_getTemperature(&rtc_t, &rtc_f))
    {
        for(;;) {
            Serial.println("Error: rtc_getTemperature(). Cannot read temperature from RTC!");
            delay(5000);
        }
    }
    Serial.printf("rtc_getTemperature(): OK : %d.%d\n", rtc_t, rtc_f);

    // Write out RTC date and time
    if (!rtc_getDateTime(&rtcDateTime))
    {
        for(;;) {
            Serial.println("Error: rtc_getDateTime(). Cannot read time from RTC!");
            delay(5000);
        }
    }
    Serial.printf(
        "%s: %d-%02d-%02d %02d:%02d:%02d\n",
        "rtc_getDateTime(): OK ",
        rtcDateTime.tm_year,
        rtcDateTime.tm_mon,
        rtcDateTime.tm_mday,
        rtcDateTime.tm_hour,
        rtcDateTime.tm_min,
        rtcDateTime.tm_sec);

    attachInterrupt(RTC_SQW, matrix_1hz_isr_loop, FALLING);
}

// Matrix init with debug message
void main_matrix_init()
{
    matrix_init(rtcDateTime);
    Serial.println("matrix_init(): OK");
}

// BLE init
void main_ble_init()
{
    ble_init();
}