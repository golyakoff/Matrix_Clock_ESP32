#include "rtc.h"

#include <stdint.h>
#include <Wire.h>
#include <ErriezDS3231.h>

ErriezDS3231 rtc;

bool rtc_init()
{
    // Setup DS3231 01101000
    Wire.begin(I2C_SDA, I2C_SCL, I2C_SPEED);

    delay(1000);

    // Initialize RTC
    if(!rtc.begin()) {
        return false;
    }

    rtc.setSquareWave(SquareWave1Hz);
}

void printTemperature()
{
    int8_t temperature = 0;
    uint8_t fraction = 0;
    
    // Force temperature conversion
    // Without this call, it takes 64 seconds before the temperature is updated.
    if (!rtc.startTemperatureConversion()) {
        //Serial.println(F("Error: Start temperature conversion failed"));
    }
    // Read temperature
    if (!rtc.getTemperature(&temperature, &fraction)) {
        //Serial.println(F("Error: Get temperature failed"));
    }
    // Print temperature. The output below is for example: 28.25C
    //   Serial.print(temperature);
    //   Serial.print(F("."));
    //   Serial.print(fraction);
    //   Serial.println(F("C"));
}

void printDateTime()
{
    struct tm dt;
    if (!rtc.read(&dt)) {
        //Serial.println(F("RTC time read failed"));
        return;
    }

    //Serial.println(asctime(&dt));  
}