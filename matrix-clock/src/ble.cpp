#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <string.h>

#include "ble.h"

// BLE Transmission Type: LSO: Least Significant Octet First

// Declare and init BLE characteristics
BLECharacteristic _dtfChar(DEVICE_TIME_FEATURE_CHAR_UUID, BLECharacteristic::PROPERTY_READ);
BLECharacteristic _dtpChar(DEVICE_TIME_PARAMETERS_CHAR_UUID, BLECharacteristic::PROPERTY_READ);
BLECharacteristic _dtChar(DEVICE_TIME_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
BLECharacteristic _dtcpChar(DEVICE_TIME_CONTROL_POINT_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);

// Declare and init BLE descriptor
BLEDescriptor _dtDescriptor(BLEUUID((uint16_t)0x2903));

// Date Time Features characteristic value
uint16_t _dtf = (uint16_t)DTF_EPOCH_YEAR_1900 | (uint16_t)DTF_DISPLAYED_FORMATS;

// Date Time Properties characteristic value
// Displayed Date Format (LSO) 0x07 = YYYY.MM.DD ("2016.12.31")
// Displayed Time Format (MSO – Lower nibble) 1110b = 24h fixed length with seconds ("09:05:42", "22:15:26")
// Displayed Date Field Separator (MSO – Upper nibble) 0010b = Hyphen ("31-Dec-2016")
struct dtp_t _dtp = { 
    .rtc_resolution = 0xffff,
    .dt_format = ((uint16_t)0b0010 << 12) | ((uint16_t)0b1110 << 8) | (uint16_t)0x07
};

// Device connected state
bool _deviceConnected = true;

// Temporary variables
struct tm _lastUpdatedDateTime;
char _dtValue[20];
std::string _str_dtValue;

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

        // Start advertising
        Serial.print("Start advertising... ");
        BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
        pAdvertising->addServiceUUID(DT_SERVICE_UUID);
        pServer->getAdvertising()->start();
        Serial.println("OK");
    }
};

bool dtAreDifferent(struct tm *dt1, struct tm *dt2);

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

    Serial.print("pServer->setCallbacks(new MyServerCallbacks())... ");
    pServer->setCallbacks(new MyServerCallbacks());
    Serial.println("OK");

    // Start a BLE service with the service UUID.
    Serial.print("BLEService *bleService = pServer->createService(DT_SERVICE_UUID)... ");
    BLEService *bleService = pServer->createService(DT_SERVICE_UUID);
    Serial.println("OK");

    // Add Device Time Feature characteristic
    Serial.print("Add Device Time Feature characteristic... ");
    bleService->addCharacteristic(&_dtfChar);
    _dtfChar.setValue((uint8_t*)(&_dtf), sizeof(_dtf));
    Serial.println("OK");

    // Add Device Time Parameters characteristic
    Serial.print("Add Device Time Parameters characteristic... ");
    bleService->addCharacteristic(&_dtpChar);
    _dtpChar.setValue((uint8_t*)(&_dtp), sizeof(_dtp));
    Serial.println("OK");

    // Add Device Time Parameters characteristic
    Serial.print("Add Device Time characteristic... ");
    bleService->addCharacteristic(&_dtChar);
    _dtDescriptor.setValue("Device Time");
    _dtChar.addDescriptor(new BLE2902());
    Serial.println("OK");

    // Start the service
    Serial.print("bleService->start()... ");
    bleService->start();
    Serial.println("OK");

    // Start advertising
    Serial.print("Start advertising... ");
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(DT_SERVICE_UUID);
    pServer->getAdvertising()->start();
    Serial.println("OK");
}

void ble_update_time(struct tm *dt)
{
    if (_deviceConnected && dtAreDifferent(dt, &_lastUpdatedDateTime)) {
        memcpy(&_lastUpdatedDateTime, dt, sizeof(struct tm));
        //Set temperature Characteristic value and notify connected client
        sprintf(
            _dtValue,
            "%d-%02d-%02d %02d:%02d:%02d",
            1900 + dt->tm_year,
            dt->tm_mon,
            dt->tm_mday,
            dt->tm_hour,
            dt->tm_min,
            dt->tm_sec);
        
        _str_dtValue.assign(_dtValue, sizeof(_dtValue));
        _dtChar.setValue(_str_dtValue);
        _dtChar.notify();
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
