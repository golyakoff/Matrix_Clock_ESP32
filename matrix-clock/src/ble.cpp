#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <string.h>

#include "time_helper.h"
#include "ble.h"

// BLE Transmission Type: LSO: Least Significant Octet First

// Declare and init BLE characteristics
static BLECharacteristic _mct_char(MC_TIME_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
static BLECharacteristic _mcts_char(MC_TIME_STR_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

static BLECharacteristic _mctfa_char(MC_TURN_OFF_ALARM_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
static BLECharacteristic _mctna_char(MC_TURN_ON_ALARM_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);

static BLECharacteristic _mctnc_char(MC_TURN_ON_CONTROL_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);

// Variables
static bool _device_connected = false;

static struct tm _last_update_dt;

static char _mcts_char_buffer[20];       // string time buffer for covertion
static std::string _mcts_char_value;     // string time
static uint32_t _mct_char_value;         // epoch time

void start_advertising(BLEServer* pServer);

static ble_updat_time_t _update_time_cb = nullptr;

// BLE Server Callbacks class
class MyServerCallbacks: public BLEServerCallbacks
{
    void onConnect(BLEServer* pServer)
    {
        _device_connected = true;
        Serial.println(F("BLE device connected"));
    }
    
    void onDisconnect(BLEServer* pServer)
    {
        _device_connected = false;
        Serial.println(F("BLE device disconnected"));

        start_advertising(pServer);
    }
};
MyServerCallbacks _myServerCallbacks;

// BLE Characteristic Callbacks class
class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic, esp_ble_gatts_cb_param_t* param) {
        if(pCharacteristic->getUUID().equals(_mct_char.getUUID()) && param->write.len == 4)
        {
            _mct_char_value = param->write.value[0];
            _mct_char_value |= param->write.value[1] << 8;
            _mct_char_value |= param->write.value[2] << 16;
            _mct_char_value |= param->write.value[3] << 24;
            
            Serial.printf(
                "Write callback for char (%s), raw array (LSO): 0x%02x 0x%02x 0x%02x 0x%02x, uint32_t: 0x%08x (%d)\n",
                pCharacteristic->getUUID().toString().c_str(),
                param->write.value[3],
                param->write.value[2],
                param->write.value[1],
                param->write.value[0],
                _mct_char_value,
                _mct_char_value);

            if (_update_time_cb != nullptr)
            {
                Serial.println(F("Calling callback _update_time_cb(dt)... "));
                time_t tim = (time_t)_mct_char_value;
                tm *dt = gmtime(&tim);
                _update_time_cb(dt);
                Serial.println(F("OK"));
            }
        }        
    }
};
MyCharacteristicCallbacks _myCharacteristicCallbacks;

void ble_init(ble_updat_time_t ble_update_time_cb)
{
    // init update time callback
    _update_time_cb = ble_update_time_cb;

    // reset last updated date and time
    memset(&_last_update_dt, 0, sizeof(_last_update_dt));

    // Create the BLE Device
    Serial.printf("Initialize BLE Device '%s'... ", BLE_SERVER_NAME);
    BLEDevice::init(BLE_SERVER_NAME);
    Serial.println(F("OK"));

    // Create the BLE Server
    Serial.print(F("Create BLE Server... "));
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(&_myServerCallbacks);
    Serial.println(F("OK"));

    // Start a BLE service with the service UUID.
    Serial.print(F("Create Primary Service... "));
    BLEService *bleService = pServer->createService(MC_SERVICE_UUID);
    Serial.print(F("OK: "));
    Serial.println(bleService->toString().c_str());

    // Add MatrixClock Time characteristic
    Serial.print(F("Add MatrixClock Time char... "));
    bleService->addCharacteristic(&_mct_char);
    _mct_char.addDescriptor(new BLE2902());
    _mct_char.setCallbacks(&_myCharacteristicCallbacks);
    Serial.print(F("OK: "));
    Serial.println(_mct_char.toString().c_str());
    
    // Add MatrixClock Time String characteristic
    Serial.print(F("Add MatrixClock Time String char... "));
    bleService->addCharacteristic(&_mcts_char);
    _mcts_char.addDescriptor(new BLE2902());
    Serial.print(F("OK: "));
    Serial.println(_mcts_char.toString().c_str());

    /*
    // Add MatrixClock Turn Off Alarm characteristic
    Serial.print(F("Add MatrixClock Turn Off Alarm char... "));
    bleService->addCharacteristic(&_mctfa_char);
    Serial.print(F("OK: "));
    Serial.println(_mctfa_char.toString().c_str());

    // Add MatrixClock Turn On Alarm characteristic
    Serial.print(F("Add MatrixClock Turn On Alarm char... "));
    bleService->addCharacteristic(&_mctna_char);
    Serial.print(F("OK: "));
    Serial.println(_mctna_char.toString().c_str());

    // Add MatrixClock Turn On/Off Control characteristic
    Serial.print(F("Add MatrixClock Turn On/Off Control char... "));
    bleService->addCharacteristic(&_mctnc_char);
    _mctnc_char.addDescriptor(new BLE2902());
    Serial.print(F("OK: "));
    Serial.println(_mctnc_char.toString().c_str());
    */

    // Start the service
    Serial.print(F("bleService->start()... "));
    bleService->start();
    Serial.println(F("OK"));

    // Start advertising
    start_advertising(pServer);
}

void ble_update_rtc_time_cb(struct tm *dt)
{
    if (_device_connected && times_are_different(dt, &_last_update_dt)) {
        memcpy(&_last_update_dt, dt, sizeof(struct tm));
        sprintf(
            _mcts_char_buffer,
            "%d-%02d-%02d %02d:%02d:%02d",
            1900 + dt->tm_year,
            dt->tm_mon,
            dt->tm_mday,
            dt->tm_hour,
            dt->tm_min,
            dt->tm_sec);
        
        _mcts_char_value.assign(_mcts_char_buffer, sizeof(_mcts_char_buffer));
        _mcts_char.setValue(_mcts_char_value);
        _mcts_char.notify();

        _mct_char_value = (uint32_t)mktime(dt);
        _mct_char.setValue(_mct_char_value);
        _mct_char.notify();
    }
}

void start_advertising(BLEServer* pServer)
{
    Serial.print(F("Start advertising... "));
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(MC_SERVICE_UUID);
    pServer->getAdvertising()->start();
    Serial.println(F("OK"));
}
