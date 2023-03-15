#ifndef __BLE_H__
#define __BLE_H__

#include "time.h"

//BLE server name
#define BLE_SERVER_NAME "Tetris Clock A1"

#define SERVICE_UUID "91bad492-b950-4226-aa2b-4ede9fa42f59"
#define TIME_CHAR_UUID "ca73b3ba-39f6-4ab3-91ae-186dc9577d99"

void ble_init();
void ble_update_time(struct tm *dt);

#endif // __BLE_H__

