#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include "ble.h"

#define SERVICE_UUID "91bad492-b950-4226-aa2b-4ede9fa42f59"

BLECharacteristic timeCharacteristics(TIME_CHAR_UUID, BLECharacteristic::PROPERTY_NOTIFY);
BLEDescriptor timeDescriptor(BLEUUID((uint16_t)0x2903));

bool deviceConnected = true;

class MyServerCallbacks: public BLEServerCallbacks
{
    void onConnect(BLEServer* pServer) 
    {
        deviceConnected = true;
    }
    
    void onDisconnect(BLEServer* pServer)
    {
        deviceConnected = false;
    }
};

void ble_init()
{
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
    Serial.print("BLEService *bleService = pServer->createService(SERVICE_UUID)... ");
    BLEService *bleService = pServer->createService(SERVICE_UUID);
    Serial.println("OK");

    // Add time characteristic
    Serial.print("Add time characteristic... ");
    bleService->addCharacteristic(&timeCharacteristics);
    timeDescriptor.setValue("Current Date & Time");
    timeCharacteristics.addDescriptor(new BLE2902());
    Serial.println("OK");

    // Start the service
    Serial.print("bleService->start()... ");
    bleService->start();
    Serial.println("OK");

    // Start advertising
    Serial.print("Start advertising... ");
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pServer->getAdvertising()->start();
    Serial.println("OK");
}

void ble_update_time(struct tm *dt)
{
    if (deviceConnected) {
        //Set temperature Characteristic value and notify connected client
        timeCharacteristics.setValue(dt->tm_sec);
        timeCharacteristics.notify();
    }
}
