#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <string.h>
#include <esp_log.h>

#include "time_helper.h"
#include "ble.h"

#define BLE_ALARM_TOTAL_MINUTES_MASK   0b0000011111111111
#define BLE_ALARM_IS_ACTIVE_MASK       0b0000100000000000

static const char* TAG = "BLE";

// BLE Transmission Type: LSO: Least Significant Octet First

// Declare and init BLE characteristics
static BLECharacteristic _mct_char(MC_TIME_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
static BLECharacteristic _mcts_char(MC_TIME_STR_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

static BLECharacteristic _mctofa_char(MC_TURN_OFF_ALARM_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
static BLECharacteristic _mctona_char(MC_TURN_ON_ALARM_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);

static BLECharacteristic _mctnc_char(MC_TURN_ON_CONTROL_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);

static BLECharacteristic _mcabe_char(MC_AUTO_BRIGHT_ENABLE_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
static BLECharacteristic _mcmbv_char(MC_MANUAL_BRIGHT_VAL_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);

static BLECharacteristic _mctt_char(MC_TEMPERATURE_CHAR_UUID, BLECharacteristic::PROPERTY_READ);
static BLECharacteristic _mctao_char(MC_AGING_OFFSET_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);

static BLECharacteristic _mcfv_char(MC_VERSION_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
static BLECharacteristic _mcotac_char(MC_OTA_CONTROL_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
static BLECharacteristic _mcotad_char(MC_OTA_DATA_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE /*| BLECharacteristic::PROPERTY_WRITE_NR*/);

// Variables
static bool _device_connected = false;

static struct tm _last_update_dt;

static char _mcts_char_buffer[20];       // string time buffer for conversion
static std::string _mcts_char_value;     // string time
static uint32_t _mct_char_value;         // epoch time

void start_advertising(BLEServer* pServer);

static ble_set_time_on_ble_write_t _set_time_on_ble_write = nullptr;

static ble_set_show_on_ble_write_t _set_show_on_ble_write = nullptr;
static ble_get_show_on_ble_read_t _get_show_on_ble_read = nullptr;

static ble_get_auto_bright_en_on_ble_read_t _get_auto_bright_en_on_ble_read = nullptr;
static ble_set_auto_bright_en_on_ble_write_t _set_auto_bright_en_on_ble_write = nullptr;

static ble_get_manual_bright_val_on_ble_read_t _get_manual_bright_val_on_ble_read = nullptr;
static ble_set_manual_bright_val_on_ble_write_t _set_manual_bright_val_on_ble_write = nullptr;

static ble_set_alarm_on_ble_write_t _set_alarm_on_ble_write = nullptr;
static ble_get_alarm_on_ble_read_t _get_alarm_on_ble_read = nullptr;

static ble_get_rtc_temperature_ble_read_t _get_rtc_temperature_ble_read = nullptr;

static ble_get_rtc_aging_offset_ble_read_t _get_rtc_aging_offset_ble_read = nullptr;
static ble_set_rtc_aging_offset_ble_write_t _set_rtc_aging_offset_ble_write = nullptr;

static ble_get_fw_ver_ble_read_t _get_fw_ver_ble_read = nullptr;
static ble_set_ota_control_ble_write_t _set_ota_control_ble_write = nullptr;
static ble_set_ota_data_ble_write_t _set_ota_data_ble_write = nullptr;

// BLE Server Callbacks class
class MyServerCallbacks: public BLEServerCallbacks
{
    void onConnect(BLEServer* pServer)
    {
        _device_connected = true;
        ESP_LOGI(TAG, "BLE device connected");

        // Translate the firmware version
        ble_update_firmware_version();
    }
    
    void onDisconnect(BLEServer* pServer)
    {
        _device_connected = false;
        ESP_LOGI(TAG, "BLE device disconnected");

        start_advertising(pServer);
    }
};
MyServerCallbacks _myServerCallbacks;

// BLE Characteristic Callbacks class
class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic, esp_ble_gatts_cb_param_t* param) {
        if(pCharacteristic->getUUID().equals(_mct_char.getUUID()) && param->write.len == 4)
        {
            if (_set_time_on_ble_write == nullptr)
            {
                ESP_LOGE(TAG, "Error: callback _set_time_on_ble_write is nullptr.");
                return;
            }

            _mct_char_value = param->write.value[0];
            _mct_char_value |= param->write.value[1] << 8;
            _mct_char_value |= param->write.value[2] << 16;
            _mct_char_value |= param->write.value[3] << 24;
            
            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG,
                "Write callback for char (%s), raw array (LSO): 0x%02x 0x%02x 0x%02x 0x%02x, uint32_t: 0x%08x (%d)",
                pCharacteristic->getUUID().toString().c_str(),
                param->write.value[3],
                param->write.value[2],
                param->write.value[1],
                param->write.value[0],
                _mct_char_value,
                _mct_char_value);
            ESP_LOGD(TAG, "Calling callback _set_time_on_ble_write(dt)...");
            #endif

            time_t tim = (time_t)_mct_char_value;

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "OK");
            #endif

            tm *dt = gmtime(&tim);
            _set_time_on_ble_write(dt);
            return;
        }
        
        if(pCharacteristic->getUUID().equals(_mctnc_char.getUUID()) && param->write.len == 1)
        {
            if (param->write.value[0] > 1)
            {
                ESP_LOGE(TAG, "Error, value of _mctnc_char provided is out of range [0, 1].");
                return;
            }

            if (_set_time_on_ble_write == nullptr)
            {
                ESP_LOGE(TAG, "Error: callback _set_show_on_ble_write is nullptr.");
                return;
            }

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "Calling callback _set_show_on_ble_write(bool)...");
            #endif

            _set_show_on_ble_write(param->write.value[0] == 1);

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "OK");
            #endif

            return;
        }

        if(pCharacteristic->getUUID().equals(_mcabe_char.getUUID()) && param->write.len == 1)
        {
            if (param->write.value[0] > 1)
            {
                ESP_LOGE(TAG, "Error, value of _mcabe_char provided is out of range [0, 1].");
                return;
            }

            if (_set_auto_bright_en_on_ble_write == nullptr)
            {
                ESP_LOGE(TAG, "Error: callback _set_auto_bright_en_on_ble_write is nullptr.");
                return;
            }

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "Calling callback _set_auto_bright_en_on_ble_write(bool)...");
            #endif

            _set_auto_bright_en_on_ble_write(param->write.value[0] == 1);

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "OK");
            #endif

            return;
        }

        if(pCharacteristic->getUUID().equals(_mcmbv_char.getUUID()) && param->write.len == 1)
        {
            if (_set_manual_bright_val_on_ble_write == nullptr)
            {
                ESP_LOGE(TAG, "Error: callback _set_manual_bright_val_on_ble_write is nullptr.");
                return;
            }

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "Calling callback _set_manual_bright_val_on_ble_write(uint8_t)...");
            #endif

            _set_manual_bright_val_on_ble_write(param->write.value[0]);

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "OK");
            #endif
            
            return;
        }

        if ((pCharacteristic->getUUID().equals(_mctofa_char.getUUID()) || 
             pCharacteristic->getUUID().equals(_mctona_char.getUUID())) &&
            param->write.len == 2)
        {
            if (_set_alarm_on_ble_write == nullptr)
            {
                ESP_LOGE(TAG, "Error: callback _set_alarm_on_ble_write is nullptr.");
                return;
            }

            uint16_t result = ((uint16_t)(param->write.value[1]) << 8) + param->write.value[0];
            
            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, 
                "BLE::onWrite for alarm: Bytes: h = %02x l = %02x, int16_t result = %d", 
                param->write.value[1],
                param->write.value[0],
                result);
            #endif
           
            bool active = (result & BLE_ALARM_IS_ACTIVE_MASK) > 0;
            uint8_t hours = (result & BLE_ALARM_TOTAL_MINUTES_MASK) / 60;
            uint8_t minutes = (result & BLE_ALARM_TOTAL_MINUTES_MASK) % 60;
            ble_alarm_index_t index = pCharacteristic->getUUID().equals(_mctofa_char.getUUID())
                ? ble_alarm_index_off
                : ble_alarm_index_on;

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "Calling callback _set_alarm_on_ble_write()...");
            #endif

            _set_alarm_on_ble_write(index, hours, minutes, active);
            
            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "OK");
            #endif
            
            return;
        }

        if (pCharacteristic->getUUID().equals(_mctao_char.getUUID()))
        {
            if (_set_rtc_aging_offset_ble_write == nullptr)
            {
                ESP_LOGE(TAG, "Error: callback _set_rtc_aging_offset_ble_write is nullptr.");
                return;
            }

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "BLE::onWrite for RTC aging offset: Byte: %02x", param->write.value[0]);
            #endif

            int8_t result = (int8_t)param->write.value[0];
            
            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "BLE::onWrite for RTC aging offset: = %d", result);
            ESP_LOGD(TAG, "Calling callback _set_rtc_aging_offset_ble_write()...");
            #endif

            _set_rtc_aging_offset_ble_write(result);
            
            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "OK");
            #endif
            
            return;
        }

        if(pCharacteristic->getUUID().equals(_mcotac_char.getUUID())) {
            if (_set_ota_control_ble_write == nullptr)
            {
                ESP_LOGE(TAG, "Error: callback _set_ota_control_ble_write is nullptr.");
                return;
            }

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "Calling callback _set_ota_control_ble_write()...");
            #endif

            _set_ota_control_ble_write(param->write.value, param->write.len);

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "OK");
            #endif

            return;
        }

        if(pCharacteristic->getUUID().equals(_mcotad_char.getUUID())) {
            if (_set_ota_data_ble_write == nullptr)
            {
                ESP_LOGE(TAG, "Error: callback _set_ota_data_ble_write is nullptr.");
                return;
            }

            delay(2);

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "Calling callback _set_ota_data_ble_write()...");
            #endif
            
            _set_ota_data_ble_write(param->write.value, param->write.len);

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "OK");
            #endif
            
            return;
        }
    }

    void onRead(BLECharacteristic* pCharacteristic, esp_ble_gatts_cb_param_t* param)
    {
        if(pCharacteristic->getUUID().equals(_mctnc_char.getUUID()))
        {
            bool show = _get_show_on_ble_read();
            uint8_t data_val[1] = { static_cast<uint8_t>(show ? 1U : 0U) };
            pCharacteristic->setValue(data_val, sizeof(data_val));
            return;
        }

        if(pCharacteristic->getUUID().equals(_mcabe_char.getUUID()))
        {
            bool enable_auto_brightness = _get_auto_bright_en_on_ble_read();
            uint8_t data_val[1] = { static_cast<uint8_t>(enable_auto_brightness ? 1U : 0U) };
            pCharacteristic->setValue(data_val, sizeof(data_val));
            return;
        }

        if(pCharacteristic->getUUID().equals(_mcmbv_char.getUUID()))
        {
            uint8_t manual_brightness_value = _get_manual_bright_val_on_ble_read();
            uint8_t data_val[1] = { manual_brightness_value };
            pCharacteristic->setValue(data_val, sizeof(data_val));
            return;
        }

        if(pCharacteristic->getUUID().equals(_mctofa_char.getUUID()) ||
           pCharacteristic->getUUID().equals(_mctona_char.getUUID()))
        {
            ble_alarm_index_t index = pCharacteristic->getUUID().equals(_mctofa_char.getUUID())
                ? ble_alarm_index_off
                : ble_alarm_index_on;
            uint8_t hours;
            uint8_t minutes;
            bool active;
            _get_alarm_on_ble_read(index, &hours, &minutes, &active);

            uint16_t data_val = minutes + hours * 60 + (active ? BLE_ALARM_IS_ACTIVE_MASK : 0);

            pCharacteristic->setValue(data_val);
            return;
        }

        if(pCharacteristic->getUUID().equals(_mctt_char.getUUID()))
        {
            int8_t temperature = _get_rtc_temperature_ble_read();
            uint8_t data_val[1] = { static_cast<uint8_t>(temperature) };
            pCharacteristic->setValue(data_val, sizeof(data_val));
            return;
        }

        if(pCharacteristic->getUUID().equals(_mctao_char.getUUID()))
        {
            int8_t aging_offset = _get_rtc_aging_offset_ble_read();
            uint8_t data_val[1] = { static_cast<uint8_t>(aging_offset) };
            pCharacteristic->setValue(data_val, sizeof(data_val));
            return;
        }
    }
};
MyCharacteristicCallbacks _myCharacteristicCallbacks;

void ble_init(
    ble_set_time_on_ble_write_t ble_set_time_on_ble_write_cb,
    ble_set_show_on_ble_write_t ble_set_show_on_ble_write_cb,
    ble_get_show_on_ble_read_t ble_get_show_on_ble_read_cb,
    ble_set_auto_bright_en_on_ble_write_t ble_set_auto_bright_en_on_ble_write_cb,
    ble_get_auto_bright_en_on_ble_read_t ble_get_auto_bright_en_on_ble_read_cb,
    ble_set_manual_bright_val_on_ble_write_t ble_set_manual_bright_val_on_ble_write_cb,
    ble_get_manual_bright_val_on_ble_read_t ble_get_manual_bright_val_on_ble_read_cb,
    ble_set_alarm_on_ble_write_t ble_set_alarm_on_ble_write_cb,
    ble_get_alarm_on_ble_read_t ble_get_alarm_on_ble_read_cb,
    ble_get_rtc_temperature_ble_read_t ble_get_rtc_temperature_ble_read_cb,
    ble_get_rtc_aging_offset_ble_read_t ble_get_rtc_aging_offset_ble_read_cb,
    ble_set_rtc_aging_offset_ble_write_t ble_set_rtc_aging_offset_ble_write_cb,
    ble_get_fw_ver_ble_read_t ble_get_fw_ver_ble_read_cb,
    ble_set_ota_control_ble_write_t ble_set_ota_control_ble_write_cb,
    ble_set_ota_data_ble_write_t ble_set_ota_data_ble_write_cb)
{
    // init update callbacks
    _set_time_on_ble_write = ble_set_time_on_ble_write_cb;

    _set_show_on_ble_write = ble_set_show_on_ble_write_cb;
    _get_show_on_ble_read = ble_get_show_on_ble_read_cb;

    _set_auto_bright_en_on_ble_write = ble_set_auto_bright_en_on_ble_write_cb;
    _get_auto_bright_en_on_ble_read = ble_get_auto_bright_en_on_ble_read_cb;

    _set_manual_bright_val_on_ble_write = ble_set_manual_bright_val_on_ble_write_cb;
    _get_manual_bright_val_on_ble_read = ble_get_manual_bright_val_on_ble_read_cb;

    _set_alarm_on_ble_write = ble_set_alarm_on_ble_write_cb;
    _get_alarm_on_ble_read = ble_get_alarm_on_ble_read_cb;

    _get_rtc_temperature_ble_read = ble_get_rtc_temperature_ble_read_cb;
    _get_rtc_aging_offset_ble_read = ble_get_rtc_aging_offset_ble_read_cb;
    _set_rtc_aging_offset_ble_write = ble_set_rtc_aging_offset_ble_write_cb;

    _get_fw_ver_ble_read = ble_get_fw_ver_ble_read_cb;
    _set_ota_control_ble_write = ble_set_ota_control_ble_write_cb;
    _set_ota_data_ble_write = ble_set_ota_data_ble_write_cb;

    // reset last updated date and time
    memset(&_last_update_dt, 0, sizeof(_last_update_dt));

    // Create the BLE Device
    ESP_LOGI(TAG, "Initialize BLE device '%s'...", BLE_SERVER_NAME);
    BLEDevice::init(BLE_SERVER_NAME);
    ESP_LOGI(TAG, "OK (BLE device initialized)");

    ESP_LOGI(TAG, "Set MTU to '%d'...", BLE_DEVICE_MTU);
    BLEDevice::setMTU(BLE_DEVICE_MTU);
    ESP_LOGI(TAG, "OK (actual MTU value now: %d)", BLEDevice::getMTU());
    
    // Create the BLE Server
    ESP_LOGI(TAG, "Create BLE server...");
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(&_myServerCallbacks);
    ESP_LOGI(TAG, "OK (BLE server created)");

    ESP_LOGI(TAG, "Create primary service...");
    BLEService *bleService = pServer->createService(BLEUUID(MC_SERVICE_UUID), 30U, 0U);
    ESP_LOGI(TAG, "OK (primary service '%s' created)", bleService->toString().c_str());

    ESP_LOGI(TAG, "Add MatrixClock Time characteristic...");
    bleService->addCharacteristic(&_mct_char);
    _mct_char.addDescriptor(new BLE2902());
    _mct_char.setCallbacks(&_myCharacteristicCallbacks);
    ESP_LOGI(TAG, "OK: %s", _mct_char.toString().c_str());
    
    ESP_LOGI(TAG, "Add MatrixClock Time String characteristic...");
    bleService->addCharacteristic(&_mcts_char);
    _mcts_char.addDescriptor(new BLE2902());
    ESP_LOGI(TAG,  "OK: %s", _mcts_char.toString().c_str());

    ESP_LOGI(TAG, "Add MatrixClock Turn Off Alarm characteristic...");
    bleService->addCharacteristic(&_mctofa_char);
    _mctofa_char.setCallbacks(&_myCharacteristicCallbacks);
    ESP_LOGI(TAG, "OK: %s", _mctofa_char.toString().c_str());

    ESP_LOGI(TAG, "Add MatrixClock Turn On Alarm characteristic...");
    bleService->addCharacteristic(&_mctona_char);
    _mctona_char.setCallbacks(&_myCharacteristicCallbacks);
    ESP_LOGI(TAG, "OK: %s", _mctona_char.toString().c_str());
    
    ESP_LOGI(TAG, "Add MatrixClock Turn On/Off Control characteristic...");
    bleService->addCharacteristic(&_mctnc_char);
    _mctnc_char.addDescriptor(new BLE2902());
    _mctnc_char.setCallbacks(&_myCharacteristicCallbacks);
    ESP_LOGI(TAG, "OK: %s", _mctnc_char.toString().c_str());

    // Add MatrixClock Turn On Auto Brightness Control characteristic
    ESP_LOGI(TAG, "Add MatrixClock Turn On Auto Brightness characteristic...");
    bleService->addCharacteristic(&_mcabe_char);
    _mcabe_char.setCallbacks(&_myCharacteristicCallbacks);
    ESP_LOGI(TAG, "OK: %s", _mcabe_char.toString().c_str());

    ESP_LOGI(TAG, "Add MatrixClock Manual Brightness Value characteristic...");
    bleService->addCharacteristic(&_mcmbv_char);
    _mcmbv_char.setCallbacks(&_myCharacteristicCallbacks);
    ESP_LOGI(TAG, "OK: %s", _mcmbv_char.toString().c_str());

    ESP_LOGI(TAG, "Add MatrixClock RTC Temperature Value characteristic...");
    bleService->addCharacteristic(&_mctt_char);
    _mctt_char.setCallbacks(&_myCharacteristicCallbacks);
    ESP_LOGI(TAG, "OK: %s", _mctt_char.toString().c_str());

    ESP_LOGI(TAG, "Add MatrixClock RTC Aging Offset Value characteristic...");
    bleService->addCharacteristic(&_mctao_char);
    _mctao_char.setCallbacks(&_myCharacteristicCallbacks);
    ESP_LOGI(TAG, "OK: %s", _mctao_char.toString().c_str());

    ESP_LOGI(TAG, "Add MatrixClock Firmware Version characteristic...");
    bleService->addCharacteristic(&_mcfv_char);
    _mcfv_char.addDescriptor(new BLE2902());
    ESP_LOGI(TAG, "OK: %s", _mcfv_char.toString().c_str());

    ESP_LOGI(TAG, "Add MatrixClock OTA Control characteristic...");
    bleService->addCharacteristic(&_mcotac_char);
    _mcotac_char.setCallbacks(&_myCharacteristicCallbacks);
    ESP_LOGI(TAG, "OK: %s", _mcotac_char.toString().c_str());

    ESP_LOGI(TAG, "Add MatrixClock OTA Data characteristic...");
    bleService->addCharacteristic(&_mcotad_char);
    uint8_t init_buffer[BLE_OTA_DATA_BUFFER] = {0};
    _mcotad_char.setValue(init_buffer, sizeof(init_buffer));
    _mcotad_char.setCallbacks(&_myCharacteristicCallbacks);
    ESP_LOGI(TAG, "OK: %s", _mcotad_char.toString().c_str());

    // Start the service
    ESP_LOGI(TAG, "bleService->start()...");
    bleService->start();
    ESP_LOGI(TAG, "OK (bleService successfully started)");

    // Start advertising
    start_advertising(pServer);
}

void ble_update_rtc_time(struct tm *dt)
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

void ble_update_firmware_version()
{
    std::string version = _get_fw_ver_ble_read();
    _mcfv_char.setValue(version);
    _mcfv_char.notify();
}

void ble_update_matrix_show(const bool show)
{
    uint8_t value[1] = { (uint8_t)(show ? 0x01 : 0x00) };
    _mctnc_char.setValue(value, sizeof(value));
    _mctnc_char.notify();
}

void start_advertising(BLEServer* pServer)
{
    ESP_LOGI(TAG, "Start advertising...");
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(MC_SERVICE_UUID);
    pServer->getAdvertising()->start();
    ESP_LOGI(TAG, "OK (advertising started)");
}
