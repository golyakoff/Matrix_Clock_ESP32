#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <string.h>

#include "ble.h"

// BLE Transmission Type: LSO: Least Significant Octet First

// Declare and init BLE characteristics
BLECharacteristic _mctChar(MC_TIME_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
BLECharacteristic _mctsChar(MC_TIME_STR_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

BLECharacteristic _mctfaChar(MC_TURN_OFF_ALARM_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
BLECharacteristic _mctnaChar(MC_TURN_ON_ALARM_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);

BLECharacteristic _mctncChar(MC_TURN_ON_CONTROL_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);

// Variables
bool _deviceConnected = false;

struct tm _lastUpdatedDateTime;

char _mctsCharBuffer[20];       // string time buffer for covertion
std::string _mctsCharValue;     // string time
uint32_t _mctCharValue;         // epoch time

bool dtAreDifferent(struct tm *dt1, struct tm *dt2);
void startAdvertising(BLEServer* pServer);

ble_updat_time_t ble_update_time = nullptr;

// BLE Server Callbacks class
class MyServerCallbacks: public BLEServerCallbacks
{
    void onConnect(BLEServer* pServer)
    {
        _deviceConnected = true;
        Serial.println("BLE device connected");
    }
    
    void onDisconnect(BLEServer* pServer)
    {
        _deviceConnected = false;
        Serial.println("BLE device disconnected");

        startAdvertising(pServer);
    }
};
MyServerCallbacks _myServerCallbacks;

// BLE Characteristic Callbacks class
class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic, esp_ble_gatts_cb_param_t* param) {
        if(pCharacteristic->getUUID().equals(_mctChar.getUUID()) && param->write.len == 4)
        {
            _mctCharValue = param->write.value[0];
            _mctCharValue |= param->write.value[1] << 8;
            _mctCharValue |= param->write.value[2] << 16;
            _mctCharValue |= param->write.value[3] << 24;
            
            Serial.printf(
                "Write callback for char (%s), raw array (LSO): 0x%02x 0x%02x 0x%02x 0x%02x, uint32_t: 0x%08x (%d)\n",
                pCharacteristic->getUUID().toString().c_str(),
                param->write.value[3],
                param->write.value[2],
                param->write.value[1],
                param->write.value[0],
                _mctCharValue,
                _mctCharValue);

            if (ble_update_time != nullptr)
            {
                Serial.println("Calling callback ble_update_time(dt)... ");
                time_t tim = (time_t)_mctCharValue;
                tm *dt = gmtime(&tim);
                ble_update_time(dt);
                Serial.println("OK");
            }
        }        
    }
};
MyCharacteristicCallbacks _myCharacteristicCallbacks;

void ble_init()
{
    // reset last updated date and time
    memset(&_lastUpdatedDateTime, 0, sizeof(_lastUpdatedDateTime));

    // Create the BLE Device
    Serial.printf("Initialize BLE Device '%s'... ", BLE_SERVER_NAME);
    BLEDevice::init(BLE_SERVER_NAME);
    Serial.println("OK");

    // Create the BLE Server
    Serial.print("Create BLE Server... ");
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(&_myServerCallbacks);
    Serial.println("OK");

    // Start a BLE service with the service UUID.
    Serial.print("Create Primary Service... ");
    BLEService *bleService = pServer->createService(MC_SERVICE_UUID);
    Serial.print("OK: ");
    Serial.println(bleService->toString().c_str());

    // Add MatrixClock Time characteristic
    Serial.print("Add MatrixClock Time char... ");
    bleService->addCharacteristic(&_mctChar);
    _mctChar.addDescriptor(new BLE2902());
    _mctChar.setCallbacks(&_myCharacteristicCallbacks);
    Serial.print("OK: ");
    Serial.println(_mctChar.toString().c_str());
    
    // Add MatrixClock Time String characteristic
    Serial.print("Add MatrixClock Time String char... ");
    bleService->addCharacteristic(&_mctsChar);
    _mctsChar.addDescriptor(new BLE2902());
    Serial.print("OK: ");
    Serial.println(_mctsChar.toString().c_str());

    /*
    // Add MatrixClock Turn Off Alarm characteristic
    Serial.print("Add MatrixClock Turn Off Alarm char... ");
    bleService->addCharacteristic(&_mctfaChar);
    Serial.print("OK: ");
    Serial.println(_mctfaChar.toString().c_str());

    // Add MatrixClock Turn On Alarm characteristic
    Serial.print("Add MatrixClock Turn On Alarm char... ");
    bleService->addCharacteristic(&_mctnaChar);
    Serial.print("OK: ");
    Serial.println(_mctnaChar.toString().c_str());

    // Add MatrixClock Turn On/Off Control characteristic
    Serial.print("Add MatrixClock Turn On/Off Control char... ");
    bleService->addCharacteristic(&_mctncChar);
    _mctncChar.addDescriptor(new BLE2902());
    Serial.print("OK: ");
    Serial.println(_mctncChar.toString().c_str());
    */

    // Start the service
    Serial.print("bleService->start()... ");
    bleService->start();
    Serial.println("OK");

    // Start advertising
    startAdvertising(pServer);
}

void ble_update_rtc_time_cb(struct tm *dt)
{
    if (_deviceConnected && dtAreDifferent(dt, &_lastUpdatedDateTime)) {
        memcpy(&_lastUpdatedDateTime, dt, sizeof(struct tm));

        sprintf(
            _mctsCharBuffer,
            "%d-%02d-%02d %02d:%02d:%02d",
            1900 + dt->tm_year,
            dt->tm_mon,
            dt->tm_mday,
            dt->tm_hour,
            dt->tm_min,
            dt->tm_sec);
        
        _mctsCharValue.assign(_mctsCharBuffer, sizeof(_mctsCharBuffer));
        _mctsChar.setValue(_mctsCharValue);
        _mctsChar.notify();

        _mctCharValue = (uint32_t)mktime(dt);
        _mctChar.setValue(_mctCharValue);
        _mctChar.notify();
    }
}

bool dtAreDifferent(struct tm *dt1, struct tm *dt2)
{
    return dt1->tm_sec  != dt2->tm_sec
        || dt1->tm_min  != dt2->tm_min
        || dt1->tm_hour != dt2->tm_hour
        || dt1->tm_mday != dt2->tm_mday
        || dt1->tm_mon  != dt2->tm_mon
        || dt1->tm_year != dt2->tm_year;
}

void startAdvertising(BLEServer* pServer)
{
    Serial.print("Start advertising... ");
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(MC_SERVICE_UUID);
    pServer->getAdvertising()->start();
    Serial.println("OK");
}
