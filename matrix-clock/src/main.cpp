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
#include "time_helper.h"
#include "rtc.h"
#include "matrix.h"
#include "ble.h"
#include "alarm.h"

static unsigned long _10ms_loop_due = 0;

static struct tm _rtc_dt;
static struct tm _matrix_dt;
static struct tm _matrix_dt_prev;

void alarm_off_callback(const uint8_t hours, const uint8_t minutes);
static Alarm alarm_off = Alarm(&alarm_off_callback);

void alarm_on_callback(const uint8_t hours, const uint8_t minutes);
static Alarm alarm_on = Alarm(&alarm_on_callback);

void main_rtc_init();
void main_matrix_init();
void main_ble_init();

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
    if (now > _10ms_loop_due) {
        matrix_100hz_loop();
        _10ms_loop_due = now + 10;

        matrix_get_time(&_matrix_dt);
        if (times_are_different(&_matrix_dt, &_matrix_dt_prev))
        {
            ble_update_rtc_time_cb(&_matrix_dt);
            memcpy(&_matrix_dt_prev, &_matrix_dt, sizeof(struct tm));
            alarm_on.tick(&_matrix_dt);
            alarm_off.tick(&_matrix_dt);
        }
    }
}

// RTC init with debug messages
void main_rtc_init()
{
    if (!rtc_init())
    {
        for(;;) {
            Serial.println(F("Error: rtc_init(). RTC was not initialized!"));
            delay(5000);
        }
    }    
    Serial.println(F("rtc_init(): OK"));
    
    // Write out RTC chip temperature
    int8_t rtc_t;
    uint8_t rtc_f;
    if (!rtc_get_temperature(&rtc_t, &rtc_f))
    {
        for(;;) {
            Serial.println(F("Error: rtc_get_temperature(). Cannot read temperature from RTC!"));
            delay(5000);
        }
    }
    Serial.printf("rtc_get_temperature(): OK : %d.%d\n", rtc_t, rtc_f);

    // Write out RTC date and time
    if (!rtc_get_time(&_rtc_dt))
    {
        for(;;) {
            Serial.println(F("Error: rtc_get_time(). Cannot read time from RTC!"));
            delay(5000);
        }
    }
    println_tm("_matrix_dt: ", &_rtc_dt);
    Serial.printf(
        "%s: %d-%02d-%02d %02d:%02d:%02d\n",
        F("rtc_get_time(): OK "),
        _rtc_dt.tm_year + 1900,
        _rtc_dt.tm_mon,
        _rtc_dt.tm_mday,
        _rtc_dt.tm_hour,
        _rtc_dt.tm_min,
        _rtc_dt.tm_sec);

    attachInterrupt(RTC_SQW, matrix_1hz_isr_loop, FALLING);

    // Initialize runtime alarms from RTC
    uint8_t hours;
    uint8_t minutes;
    bool active;
    rtc_memory_get_alarm(rtc_alarm_index_on, &hours, &minutes, &active);
    alarm_on.set(hours, minutes, active);
    rtc_memory_get_alarm(rtc_alarm_index_off, &hours, &minutes, &active);
    alarm_off.set(hours, minutes, active);
}

// Matrix init with debug message
void main_matrix_init()
{
    matrix_init(&_rtc_dt);
    Serial.println(F("matrix_init(): OK"));
}

// BLE init
void set_rtc_time_on_ble_write(const struct tm *dt)
{
    // update rtc
    if (rtc_set_time(dt))
    {
        // update matrix
        matrix_set_time(dt, true);
        println_tm("-> Runtime date and time updated with the value", dt);
        return;
    }

    Serial.println(F("-> Error: Runtime date and time update failed"));
}

void set_matrix_show_on_ble_write(bool show)
{
    matrix_set_show(show);
    Serial.printf("-> Matrix update show state: %d\n", show);
}

bool get_matrix_show_on_ble_read()
{
    return matrix_get_show();
}

void set_matrix_auto_brightness_on_ble_write(bool auto_brightness)
{
    matrix_set_auto_brightness(auto_brightness);
    Serial.printf("-> Matrix update auto brightness state: %d\n", auto_brightness);
}

bool get_matrix_auto_brightness_on_ble_read()
{
    return matrix_get_auto_brightness();
}

void set_matrix_manual_brightness_on_ble_write(uint8_t manual_brightness)
{
    matrix_set_manual_brightness(manual_brightness);
    Serial.printf("-> Matrix update manual brightness value: %d\n", manual_brightness);
}

uint8_t get_matrix_manual_brightness_on_ble_read()
{
    return matrix_get_manual_brightness();
}

void set_matrix_alarm_on_ble_write(ble_alarm_index_t index, uint8_t hours, uint8_t minutes, bool active)
{
    Serial.printf("Call set_matrix_alarm_on_ble_write(%d, %02d, %02d, %d)", index, hours, active);
    switch (index)
    {
        case ble_alarm_index_off:
            alarm_off.set(hours, minutes, active);
            if (!rtc_memory_set_alarm(rtc_alarm_index_off, hours, minutes, active))
                Serial.println(F("Error: set_matrix_alarm_on_ble_write(ble_alarm_index_off...) > rtc_memory_set_alarm(rtc_alarm_index_off...)"));
            break;
        case ble_alarm_index_on:
            alarm_on.set(hours, minutes, active);
            if (!rtc_memory_set_alarm(rtc_alarm_index_on, hours, minutes, active))
                Serial.println(F("Error: set_matrix_alarm_on_ble_write(ble_alarm_index_on...) > rtc_memory_set_alarm(ble_alarm_index_on...)"));
            break;
        default:
            break;
            Serial.printf("Error: Unknown alarm index '%d' in set_matrix_alarm_on_ble_write()", index);
    }
}

void get_matrix_alarm_on_ble_read(ble_alarm_index_t index, uint8_t *hours, uint8_t *minutes, bool *active)
{
    switch (index)
    {
        case ble_alarm_index_off:
            alarm_off.get(hours, minutes, active);
            break;
        case ble_alarm_index_on:
            alarm_on.get(hours, minutes, active);
            break;
        default:
            break;
            Serial.printf("Error: Unknown alarm index '%d' in get_matrix_alarm_on_ble_read()", index);
    }
}

void main_ble_init()
{
    ble_init(
        &set_rtc_time_on_ble_write,
        &set_matrix_show_on_ble_write,
        &get_matrix_show_on_ble_read,
        &set_matrix_auto_brightness_on_ble_write,
        &get_matrix_auto_brightness_on_ble_read,
        &set_matrix_manual_brightness_on_ble_write,
        &get_matrix_manual_brightness_on_ble_read,
        &set_matrix_alarm_on_ble_write,
        &get_matrix_alarm_on_ble_read);
}

 void alarm_off_callback(const uint8_t hours, const uint8_t minutes)
 {
    matrix_set_show(false);
    ble_update_matrix_show_cb(false);
 }

 void alarm_on_callback(const uint8_t hours, const uint8_t minutes)
 {
    matrix_set_show(true);
    ble_update_matrix_show_cb(true);
 }