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

// Declare and init BLE descriptors
BLEDescriptor _mctDescriptor(BLEUUID((uint16_t)0x2903));
BLEDescriptor _mctsDescriptor(BLEUUID((uint16_t)0x2903));
BLEDescriptor _mctncDescriptor(BLEUUID((uint16_t)0x2903));

// Variables
bool _deviceConnected = false;
struct tm _lastUpdatedDateTime;
char _mctsCharBuffer[20];
std::string _mctsCharValue;

bool dtAreDifferent(struct tm *dt1, struct tm *dt2);
void startAdvertising(BLEServer* pServer);

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

void ble_init()
{
    // reset last updated date and time
    memset(&_lastUpdatedDateTime, 0, sizeof(_lastUpdatedDateTime));

    // Create the BLE Device
    Serial.print("BLEDevice::init(BLE_SERVER_NAME)... ");
    BLEDevice::init(BLE_SERVER_NAME);
    Serial.println("OK");

    // Create the BLE Server
    Serial.print("BLEServer *pServer = BLEDevice::createServer()... ");
    BLEServer *pServer = BLEDevice::createServer();
    Serial.println("OK");

    pServer->setCallbacks(new MyServerCallbacks());

    // Start a BLE service with the service UUID.
    Serial.print("BLEService *bleService = pServer->createService(MC_SERVICE_UUID)... ");
    BLEService *bleService = pServer->createService(MC_SERVICE_UUID);
    Serial.println("OK");

    // Add MatrixClock Time characteristic
    Serial.print("Add MatrixClock Time characteristic... ");
    bleService->addCharacteristic(&_mctChar);
    _mctDescriptor.setValue("MatrixClock Time");
    _mctChar.addDescriptor(new BLE2902());
    Serial.println("OK");

    // Add MatrixClock Time String characteristic
    Serial.print("Add MatrixClock Time String characteristic... ");
    bleService->addCharacteristic(&_mctsChar);
    _mctsDescriptor.setValue("MatrixClock Time String");
    _mctsChar.addDescriptor(new BLE2902());
    Serial.println("OK");

    // Add MatrixClock Turn Off Alarm characteristic
    Serial.print("Add MatrixClock Turn Off Alarm characteristic... ");
    bleService->addCharacteristic(&_mctfaChar);
    Serial.println("OK");

    // Add MatrixClock Turn On Alarm characteristic
    Serial.print("Add MatrixClock Turn On Alarm characteristic... ");
    bleService->addCharacteristic(&_mctnaChar);
    Serial.println("OK");

    // Add MatrixClock Turn On/Off Control characteristic
    Serial.print("Add MatrixClock Turn On/Off Control characteristic... ");
    bleService->addCharacteristic(&_mctncChar);
    _mctncDescriptor.setValue("MatrixClock Turn On/Off Control");
    _mctncChar.addDescriptor(new BLE2902());
    Serial.println("OK");

    // Start the service
    Serial.print("bleService->start()... ");
    bleService->start();
    Serial.println("OK");

    // Start advertising
    startAdvertising(pServer);
}

void ble_on_update_time_callback(struct tm *dt)
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
        _mctChar.setValue(_mctsCharValue);
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
