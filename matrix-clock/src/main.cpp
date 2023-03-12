/*******************************************************************
    Tetris clock that fetches its time Using the EzTimeLibrary

    For use with the ESP32
 *                                                                 *
    Written by Andrei Golyakoff using code of Brian Lough
 *******************************************************************/

// Standard Libraries
#include <Arduino.h>
#include <Wire.h>

// Additional Libraries
#include <ErriezDS3231.h>

// Project includes
#include "rtc.h"
#include "matrix.h"

unsigned long oneSecondLoopDue = 0;

void setup() {
    Serial.begin(115200);

    rtc_init();
    matrix_init();
}

void loop() {
    unsigned long now = millis();
    if (now > oneSecondLoopDue) {
        matrix_loop_every_second();
        oneSecondLoopDue = now + 1000;
    }
}
