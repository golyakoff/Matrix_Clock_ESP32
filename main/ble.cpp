#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>
#include <esp_log.h>

#include "time_helper.h"
#include "ble.h"

#define BLE_ALARM_TOTAL_MINUTES_MASK   0b0000011111111111
#define BLE_ALARM_IS_ACTIVE_MASK       0b0000100000000000

#define MC_HOURLY_BRIGHTNESS_LEN       (24U)

// Animation Splash two-byte layout (see MC_ANIMATION_SPLASH_CHAR_UUID in ble.h)
#define MC_ANIM_SPLASH_MODE_MASK       0b00000011
#define MC_ANIM_SPLASH_DURATION_MASK   0b00001100
#define MC_ANIM_SPLASH_DURATION_SHIFT  (2U)
#define MC_ANIM_SPLASH_INDEX_MASK      0b01110000  // byte 0: the low 3 bits of the index
#define MC_ANIM_SPLASH_INDEX_SHIFT     (4U)
#define MC_ANIM_SPLASH_PREVIEW_MASK    0b10000000
#define MC_ANIM_SPLASH_INDEX_HI_MASK   0b00000001  // byte 1: the high bit of the index
#define MC_ANIM_SPLASH_INDEX_HI_SHIFT  (3U)        // ...which is bit 3 of the index itself
#define MC_ANIM_SPLASH_LEN             (2U)

static const char* TAG = "BLE";

// BLE Transmission Type: LSO: Least Significant Octet First

// BLE characteristics, created in ble_init(). NimBLE adds the CCCD (0x2902) descriptor to every
// characteristic with the notify property by itself.
static NimBLECharacteristic* _mct_char = nullptr;
static NimBLECharacteristic* _mcts_char = nullptr;
static NimBLECharacteristic* _mctofa_char = nullptr;
static NimBLECharacteristic* _mctona_char = nullptr;
static NimBLECharacteristic* _mctnc_char = nullptr;
static NimBLECharacteristic* _mcabe_char = nullptr;
static NimBLECharacteristic* _mcmbv_char = nullptr;
static NimBLECharacteristic* _mcco_char = nullptr;
static NimBLECharacteristic* _mcas_char = nullptr;
static NimBLECharacteristic* _mchbt_char = nullptr;
static NimBLECharacteristic* _mctt_char = nullptr;
static NimBLECharacteristic* _mctao_char = nullptr;
static NimBLECharacteristic* _mcfs_char = nullptr;
static NimBLECharacteristic* _mcac_char = nullptr;
static NimBLECharacteristic* _mcfv_char = nullptr;
static NimBLECharacteristic* _mcotac_char = nullptr;
static NimBLECharacteristic* _mcotad_char = nullptr;

// Variables
static bool _device_connected = false;

// Kept so an OTA update can ask the central for connection parameters friendlier to the fast
// firmware stream (a long supervision timeout) - see ble_request_ota_conn_params().
static NimBLEServer* _pServer = nullptr;
static uint16_t _conn_handle = 0;
static bool _have_conn_handle = false;

static struct tm _last_update_dt;

static char _mcts_char_buffer[20];       // string time buffer for conversion
static std::string _mcts_char_value;     // string time
static uint32_t _mct_char_value;         // epoch time

void start_advertising();

static ble_set_time_on_ble_write_t _set_time_on_ble_write = nullptr;

static ble_set_show_on_ble_write_t _set_show_on_ble_write = nullptr;
static ble_get_show_on_ble_read_t _get_show_on_ble_read = nullptr;

static ble_get_auto_bright_en_on_ble_read_t _get_auto_bright_en_on_ble_read = nullptr;
static ble_set_auto_bright_en_on_ble_write_t _set_auto_bright_en_on_ble_write = nullptr;

static ble_get_manual_bright_val_on_ble_read_t _get_manual_bright_val_on_ble_read = nullptr;
static ble_set_manual_bright_val_on_ble_write_t _set_manual_bright_val_on_ble_write = nullptr;

static ble_get_color_order_on_ble_read_t _get_color_order_on_ble_read = nullptr;
static ble_set_color_order_on_ble_write_t _set_color_order_on_ble_write = nullptr;

static ble_get_animation_splash_on_ble_read_t _get_animation_splash_on_ble_read = nullptr;
static ble_set_animation_splash_on_ble_write_t _set_animation_splash_on_ble_write = nullptr;

static ble_get_hourly_brightness_on_ble_read_t _get_hourly_brightness_on_ble_read = nullptr;
static ble_set_hourly_brightness_on_ble_write_t _set_hourly_brightness_on_ble_write = nullptr;

static ble_set_alarm_on_ble_write_t _set_alarm_on_ble_write = nullptr;
static ble_get_alarm_on_ble_read_t _get_alarm_on_ble_read = nullptr;

static ble_get_rtc_temperature_ble_read_t _get_rtc_temperature_ble_read = nullptr;

static ble_get_rtc_aging_offset_ble_read_t _get_rtc_aging_offset_ble_read = nullptr;
static ble_set_rtc_aging_offset_ble_write_t _set_rtc_aging_offset_ble_write = nullptr;

static ble_get_fw_ver_ble_read_t _get_fw_ver_ble_read = nullptr;
static ble_set_ota_control_ble_write_t _set_ota_control_ble_write = nullptr;
static ble_set_ota_data_ble_write_t _set_ota_data_ble_write = nullptr;

// BLE Server Callbacks class
class MyServerCallbacks: public NimBLEServerCallbacks
{
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override
    {
        _device_connected = true;
        // Kept so ble_request_ota_conn_params() can target this connection when an OTA update starts.
        _conn_handle = connInfo.getConnHandle();
        _have_conn_handle = true;
        ESP_LOGI(TAG, "BLE device connected");

        // Translate the firmware version
        ble_update_firmware_version();
    }

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override
    {
        _device_connected = false;
        _have_conn_handle = false;
        ESP_LOGI(TAG, "BLE device disconnected, reason %d", reason);

        start_advertising();
    }
};
MyServerCallbacks _myServerCallbacks;

// BLE Characteristic Callbacks class
class MyCharacteristicCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        // The value stays valid until the callback returns: the stack doesn't touch it meanwhile.
        NimBLEAttValue value = pCharacteristic->getValue();
        const uint8_t* write_value = value.data();
        const size_t write_len = value.size();

        if(pCharacteristic == _mct_char && write_len == 4)
        {
            if (_set_time_on_ble_write == nullptr)
            {
                ESP_LOGE(TAG, "Error: callback _set_time_on_ble_write is nullptr.");
                return;
            }

            _mct_char_value = write_value[0];
            _mct_char_value |= write_value[1] << 8;
            _mct_char_value |= write_value[2] << 16;
            _mct_char_value |= write_value[3] << 24;
            
            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG,
                "Write callback for char (%s), raw array (LSO): 0x%02x 0x%02x 0x%02x 0x%02x, uint32_t: 0x%08x (%d)",
                pCharacteristic->getUUID().toString().c_str(),
                write_value[3],
                write_value[2],
                write_value[1],
                write_value[0],
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
        
        if(pCharacteristic == _mctnc_char && write_len == 1)
        {
            if (write_value[0] > 1)
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

            _set_show_on_ble_write(write_value[0] == 1);

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "OK");
            #endif

            return;
        }

        if(pCharacteristic == _mcabe_char && write_len == 1)
        {
            if (write_value[0] > 1)
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

            _set_auto_bright_en_on_ble_write(write_value[0] == 1);

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "OK");
            #endif

            return;
        }

        if(pCharacteristic == _mcmbv_char && write_len == 1)
        {
            if (_set_manual_bright_val_on_ble_write == nullptr)
            {
                ESP_LOGE(TAG, "Error: callback _set_manual_bright_val_on_ble_write is nullptr.");
                return;
            }

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "Calling callback _set_manual_bright_val_on_ble_write(uint8_t)...");
            #endif

            _set_manual_bright_val_on_ble_write(write_value[0]);

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "OK");
            #endif

            return;
        }

        if(pCharacteristic == _mcco_char && write_len == 1)
        {
            if (write_value[0] > 1)
            {
                ESP_LOGE(TAG, "Error, value of _mcco_char provided is out of range [0, 1].");
                return;
            }

            if (_set_color_order_on_ble_write == nullptr)
            {
                ESP_LOGE(TAG, "Error: callback _set_color_order_on_ble_write is nullptr.");
                return;
            }

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "Calling callback _set_color_order_on_ble_write(bool)...");
            #endif

            _set_color_order_on_ble_write(write_value[0] == 1);

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "OK");
            #endif

            return;
        }

        // The second byte (the high bit of the animation index) is optional: an app older than
        // firmware v1.8.0 writes a single byte and means index 0..7.
        if(pCharacteristic == _mcas_char &&
           (write_len == 1 || write_len == MC_ANIM_SPLASH_LEN))
        {
            if (_set_animation_splash_on_ble_write == nullptr)
            {
                ESP_LOGE(TAG, "Error: callback _set_animation_splash_on_ble_write is nullptr.");
                return;
            }

            uint8_t value = write_value[0];
            uint8_t value_hi = (write_len == MC_ANIM_SPLASH_LEN) ? write_value[1] : 0U;
            uint8_t mode = value & MC_ANIM_SPLASH_MODE_MASK;
            uint8_t duration = (value & MC_ANIM_SPLASH_DURATION_MASK) >> MC_ANIM_SPLASH_DURATION_SHIFT;
            uint8_t index = ((value & MC_ANIM_SPLASH_INDEX_MASK) >> MC_ANIM_SPLASH_INDEX_SHIFT) |
                            ((value_hi & MC_ANIM_SPLASH_INDEX_HI_MASK) << MC_ANIM_SPLASH_INDEX_HI_SHIFT);
            bool preview_now = (value & MC_ANIM_SPLASH_PREVIEW_MASK) > 0;

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "Calling callback _set_animation_splash_on_ble_write(mode=%d, dur=%d, idx=%d, preview=%d)...", mode, duration, index, preview_now);
            #endif

            _set_animation_splash_on_ble_write(mode, duration, index, preview_now);

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "OK");
            #endif

            return;
        }

        if(pCharacteristic == _mchbt_char && write_len == MC_HOURLY_BRIGHTNESS_LEN)
        {
            if (_set_hourly_brightness_on_ble_write == nullptr)
            {
                ESP_LOGE(TAG, "Error: callback _set_hourly_brightness_on_ble_write is nullptr.");
                return;
            }

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "Calling callback _set_hourly_brightness_on_ble_write(...)...");
            #endif

            _set_hourly_brightness_on_ble_write(write_value, write_len);

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "OK");
            #endif

            return;
        }

        if ((pCharacteristic == _mctofa_char ||
             pCharacteristic == _mctona_char) &&
            write_len == 2)
        {
            if (_set_alarm_on_ble_write == nullptr)
            {
                ESP_LOGE(TAG, "Error: callback _set_alarm_on_ble_write is nullptr.");
                return;
            }

            uint16_t result = ((uint16_t)(write_value[1]) << 8) + write_value[0];
            
            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, 
                "BLE::onWrite for alarm: Bytes: h = %02x l = %02x, int16_t result = %d", 
                write_value[1],
                write_value[0],
                result);
            #endif
           
            bool active = (result & BLE_ALARM_IS_ACTIVE_MASK) > 0;
            uint8_t hours = (result & BLE_ALARM_TOTAL_MINUTES_MASK) / 60;
            uint8_t minutes = (result & BLE_ALARM_TOTAL_MINUTES_MASK) % 60;
            ble_alarm_index_t index = pCharacteristic == _mctofa_char
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

        if (pCharacteristic == _mctao_char)
        {
            if (_set_rtc_aging_offset_ble_write == nullptr)
            {
                ESP_LOGE(TAG, "Error: callback _set_rtc_aging_offset_ble_write is nullptr.");
                return;
            }

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "BLE::onWrite for RTC aging offset: Byte: %02x", write_value[0]);
            #endif

            int8_t result = (int8_t)write_value[0];
            
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

        if(pCharacteristic == _mcotac_char) {
            if (_set_ota_control_ble_write == nullptr)
            {
                ESP_LOGE(TAG, "Error: callback _set_ota_control_ble_write is nullptr.");
                return;
            }

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "Calling callback _set_ota_control_ble_write()...");
            #endif

            _set_ota_control_ble_write(write_value, write_len);

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "OK");
            #endif

            return;
        }

        if(pCharacteristic == _mcotad_char) {
            if (_set_ota_data_ble_write == nullptr)
            {
                ESP_LOGE(TAG, "Error: callback _set_ota_data_ble_write is nullptr.");
                return;
            }

            delay(2);

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "Calling callback _set_ota_data_ble_write()...");
            #endif
            
            _set_ota_data_ble_write(write_value, write_len);

            #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
            ESP_LOGD(TAG, "OK");
            #endif
            
            return;
        }
    }

    void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
    {
        if(pCharacteristic == _mctnc_char)
        {
            bool show = _get_show_on_ble_read();
            uint8_t data_val[1] = { static_cast<uint8_t>(show ? 1U : 0U) };
            pCharacteristic->setValue(data_val, sizeof(data_val));
            return;
        }

        if(pCharacteristic == _mcabe_char)
        {
            bool enable_auto_brightness = _get_auto_bright_en_on_ble_read();
            uint8_t data_val[1] = { static_cast<uint8_t>(enable_auto_brightness ? 1U : 0U) };
            pCharacteristic->setValue(data_val, sizeof(data_val));
            return;
        }

        if(pCharacteristic == _mcmbv_char)
        {
            uint8_t manual_brightness_value = _get_manual_bright_val_on_ble_read();
            uint8_t data_val[1] = { manual_brightness_value };
            pCharacteristic->setValue(data_val, sizeof(data_val));
            return;
        }

        if(pCharacteristic == _mcco_char)
        {
            bool use_rrbbgg = _get_color_order_on_ble_read();
            uint8_t data_val[1] = { static_cast<uint8_t>(use_rrbbgg ? 1U : 0U) };
            pCharacteristic->setValue(data_val, sizeof(data_val));
            return;
        }

        if(pCharacteristic == _mcas_char)
        {
            uint8_t mode = 0;
            uint8_t duration = 0;
            uint8_t index = 0;
            _get_animation_splash_on_ble_read(&mode, &duration, &index);
            // A client that only reads byte 0 still gets a usable value for animations 0..7.
            uint8_t data_val[MC_ANIM_SPLASH_LEN] = {
                static_cast<uint8_t>(
                    (mode & MC_ANIM_SPLASH_MODE_MASK) |
                    ((duration << MC_ANIM_SPLASH_DURATION_SHIFT) & MC_ANIM_SPLASH_DURATION_MASK) |
                    ((index << MC_ANIM_SPLASH_INDEX_SHIFT) & MC_ANIM_SPLASH_INDEX_MASK)),
                static_cast<uint8_t>(
                    (index >> MC_ANIM_SPLASH_INDEX_HI_SHIFT) & MC_ANIM_SPLASH_INDEX_HI_MASK) };
            pCharacteristic->setValue(data_val, sizeof(data_val));
            return;
        }

        if(pCharacteristic == _mchbt_char)
        {
            uint8_t table[MC_HOURLY_BRIGHTNESS_LEN];
            _get_hourly_brightness_on_ble_read(table);
            pCharacteristic->setValue(table, sizeof(table));
            return;
        }

        if(pCharacteristic == _mctofa_char ||
           pCharacteristic == _mctona_char)
        {
            ble_alarm_index_t index = pCharacteristic == _mctofa_char
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

        if(pCharacteristic == _mctt_char)
        {
            int8_t temperature = _get_rtc_temperature_ble_read();
            uint8_t data_val[1] = { static_cast<uint8_t>(temperature) };
            pCharacteristic->setValue(data_val, sizeof(data_val));
            return;
        }

        if(pCharacteristic == _mctao_char)
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
    ble_set_color_order_on_ble_write_t ble_set_color_order_on_ble_write_cb,
    ble_get_color_order_on_ble_read_t ble_get_color_order_on_ble_read_cb,
    ble_set_animation_splash_on_ble_write_t ble_set_animation_splash_on_ble_write_cb,
    ble_get_animation_splash_on_ble_read_t ble_get_animation_splash_on_ble_read_cb,
    ble_set_hourly_brightness_on_ble_write_t ble_set_hourly_brightness_on_ble_write_cb,
    ble_get_hourly_brightness_on_ble_read_t ble_get_hourly_brightness_on_ble_read_cb,
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

    _set_color_order_on_ble_write = ble_set_color_order_on_ble_write_cb;
    _get_color_order_on_ble_read = ble_get_color_order_on_ble_read_cb;

    _set_animation_splash_on_ble_write = ble_set_animation_splash_on_ble_write_cb;
    _get_animation_splash_on_ble_read = ble_get_animation_splash_on_ble_read_cb;

    _set_hourly_brightness_on_ble_write = ble_set_hourly_brightness_on_ble_write_cb;
    _get_hourly_brightness_on_ble_read = ble_get_hourly_brightness_on_ble_read_cb;

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
    NimBLEDevice::init(BLE_SERVER_NAME);
    ESP_LOGI(TAG, "OK (BLE device initialized)");

    ESP_LOGI(TAG, "Set MTU to '%d'...", BLE_DEVICE_MTU);
    NimBLEDevice::setMTU(BLE_DEVICE_MTU);
    ESP_LOGI(TAG, "OK (actual MTU value now: %d)", NimBLEDevice::getMTU());
    
    // Create the BLE Server
    ESP_LOGI(TAG, "Create BLE server...");
    NimBLEServer *pServer = NimBLEDevice::createServer();
    _pServer = pServer;
    pServer->setCallbacks(&_myServerCallbacks);
    // start_advertising() is called from onDisconnect() instead, as with the Bluedroid stack before
    pServer->advertiseOnDisconnect(false);
    ESP_LOGI(TAG, "OK (BLE server created)");

    ESP_LOGI(TAG, "Create primary service...");
    NimBLEService *bleService = pServer->createService(MC_SERVICE_UUID);
    ESP_LOGI(TAG, "OK (primary service '%s' created)", bleService->getUUID().toString().c_str());

    const uint32_t RW = NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE;

    ESP_LOGI(TAG, "Add MatrixClock Time characteristic...");
    _mct_char = bleService->createCharacteristic(MC_TIME_CHAR_UUID, RW | NIMBLE_PROPERTY::NOTIFY);
    _mct_char->setCallbacks(&_myCharacteristicCallbacks);
    
    ESP_LOGI(TAG, "Add MatrixClock Time String characteristic...");
    _mcts_char = bleService->createCharacteristic(MC_TIME_STR_CHAR_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

    ESP_LOGI(TAG, "Add MatrixClock Turn Off Alarm characteristic...");
    _mctofa_char = bleService->createCharacteristic(MC_TURN_OFF_ALARM_CHAR_UUID, RW);
    _mctofa_char->setCallbacks(&_myCharacteristicCallbacks);

    ESP_LOGI(TAG, "Add MatrixClock Turn On Alarm characteristic...");
    _mctona_char = bleService->createCharacteristic(MC_TURN_ON_ALARM_CHAR_UUID, RW);
    _mctona_char->setCallbacks(&_myCharacteristicCallbacks);
    
    ESP_LOGI(TAG, "Add MatrixClock Turn On/Off Control characteristic...");
    _mctnc_char = bleService->createCharacteristic(MC_TURN_ON_CONTROL_CHAR_UUID, RW | NIMBLE_PROPERTY::NOTIFY);
    _mctnc_char->setCallbacks(&_myCharacteristicCallbacks);

    ESP_LOGI(TAG, "Add MatrixClock Turn On Auto Brightness characteristic...");
    _mcabe_char = bleService->createCharacteristic(MC_AUTO_BRIGHT_ENABLE_CHAR_UUID, RW);
    _mcabe_char->setCallbacks(&_myCharacteristicCallbacks);

    ESP_LOGI(TAG, "Add MatrixClock Manual Brightness Value characteristic...");
    _mcmbv_char = bleService->createCharacteristic(MC_MANUAL_BRIGHT_VAL_CHAR_UUID, RW);
    _mcmbv_char->setCallbacks(&_myCharacteristicCallbacks);

    ESP_LOGI(TAG, "Add MatrixClock Color Order characteristic...");
    _mcco_char = bleService->createCharacteristic(MC_COLOR_ORDER_CHAR_UUID, RW);
    _mcco_char->setCallbacks(&_myCharacteristicCallbacks);

    ESP_LOGI(TAG, "Add MatrixClock Animation Splash characteristic...");
    _mcas_char = bleService->createCharacteristic(MC_ANIMATION_SPLASH_CHAR_UUID, RW);
    _mcas_char->setCallbacks(&_myCharacteristicCallbacks);

    ESP_LOGI(TAG, "Add MatrixClock Hourly Brightness Schedule characteristic...");
    _mchbt_char = bleService->createCharacteristic(MC_HOURLY_BRIGHTNESS_CHAR_UUID, RW);
    _mchbt_char->setCallbacks(&_myCharacteristicCallbacks);

    ESP_LOGI(TAG, "Add MatrixClock RTC Temperature Value characteristic...");
    _mctt_char = bleService->createCharacteristic(MC_TEMPERATURE_CHAR_UUID, NIMBLE_PROPERTY::READ);
    _mctt_char->setCallbacks(&_myCharacteristicCallbacks);

    ESP_LOGI(TAG, "Add MatrixClock RTC Aging Offset Value characteristic...");
    _mctao_char = bleService->createCharacteristic(MC_AGING_OFFSET_CHAR_UUID, RW);
    _mctao_char->setCallbacks(&_myCharacteristicCallbacks);

    ESP_LOGI(TAG, "Add MatrixClock Flash Size characteristic...");
    _mcfs_char = bleService->createCharacteristic(MC_FLASH_SIZE_CHAR_UUID, NIMBLE_PROPERTY::READ);
    // Physical flash chip size in whole MB, read from the chip's JEDEC id at boot (constant for the
    // power-on session), so the value is set once here rather than via an onRead callback.
    uint8_t flash_size_mb = (uint8_t)(ESP.getFlashChipSize() >> 20);
    _mcfs_char->setValue(&flash_size_mb, sizeof(flash_size_mb));
    ESP_LOGI(TAG, "OK (flash %u MB)", flash_size_mb);

    ESP_LOGI(TAG, "Add MatrixClock Animation Count characteristic...");
    _mcac_char = bleService->createCharacteristic(MC_ANIMATION_COUNT_CHAR_UUID, NIMBLE_PROPERTY::READ);
    // Value comes from the matrix animation registry, which ble.cpp can't include; main.cpp pushes
    // it in via ble_set_animation_count() at boot. Seed with 0 until then.
    uint8_t animation_count_seed = 0;
    _mcac_char->setValue(&animation_count_seed, sizeof(animation_count_seed));

    ESP_LOGI(TAG, "Add MatrixClock Firmware Version characteristic...");
    _mcfv_char = bleService->createCharacteristic(MC_VERSION_CHAR_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    // Set right away, not only on connect: a read may come before onConnect() has run
    _mcfv_char->setValue(_get_fw_ver_ble_read());

    ESP_LOGI(TAG, "Add MatrixClock OTA Control characteristic...");
    _mcotac_char = bleService->createCharacteristic(MC_OTA_CONTROL_CHAR_UUID, NIMBLE_PROPERTY::WRITE);
    _mcotac_char->setCallbacks(&_myCharacteristicCallbacks);

    ESP_LOGI(TAG, "Add MatrixClock OTA Data characteristic...");
    _mcotad_char = bleService->createCharacteristic(MC_OTA_DATA_CHAR_UUID, NIMBLE_PROPERTY::WRITE, BLE_OTA_DATA_BUFFER);
    _mcotad_char->setCallbacks(&_myCharacteristicCallbacks);

    // Start the server (registers the service with the stack)
    ESP_LOGI(TAG, "pServer->start()...");
    pServer->start();
    ESP_LOGI(TAG, "OK (BLE server successfully started)");

    // Start advertising
    start_advertising();
}

void ble_update_rtc_time(struct tm *dt)
{
    if (_device_connected && times_are_different(dt, &_last_update_dt)) {
        memcpy(&_last_update_dt, dt, sizeof(struct tm));
        // The value is always the 20 bytes of "YYYY-MM-DD HH:mm:ss" + zero; formatted via a bigger
        // buffer, since the compiler can't know the fields fit
        char formatted[48] = { 0 };
        snprintf(
            formatted,
            sizeof(formatted),
            "%d-%02d-%02d %02d:%02d:%02d",
            1900 + dt->tm_year,
            dt->tm_mon,
            dt->tm_mday,
            dt->tm_hour,
            dt->tm_min,
            dt->tm_sec);
        memset(_mcts_char_buffer, 0, sizeof(_mcts_char_buffer));
        memcpy(_mcts_char_buffer, formatted, sizeof(_mcts_char_buffer) - 1);
        
        _mcts_char_value.assign(_mcts_char_buffer, sizeof(_mcts_char_buffer));
        _mcts_char->setValue(_mcts_char_value);
        _mcts_char->notify();

        _mct_char_value = (uint32_t)mktime(dt);
        _mct_char->setValue(_mct_char_value);
        _mct_char->notify();
    }
}

void ble_update_firmware_version()
{
    std::string version = _get_fw_ver_ble_read();
    _mcfv_char->setValue(version);
    _mcfv_char->notify();
}

void ble_set_animation_count(uint8_t count)
{
    _mcac_char->setValue(&count, sizeof(count));
    ESP_LOGI(TAG, "Animation count characteristic set to %u", count);
}

void ble_request_ota_conn_params()
{
    if (!_pServer || !_device_connected || !_have_conn_handle) {
        ESP_LOGW(TAG, "ble_request_ota_conn_params: no active connection, skipping");
        return;
    }

    // The OTA firmware stream keeps the CPU busy writing flash, which stalls the radio for tens of
    // milliseconds at a time (a 4 KB sector erase). With a short supervision timeout the central
    // tears the link down mid-transfer (the clock stops answering connection events -> GATT status
    // 8), which aborts the update and reboots the clock. Ask the central for a connection tuned for
    // the transfer: a short-ish interval (frequent events, so the radio re-syncs quickly in the gaps
    // between flash writes) plus a long 6 s supervision timeout, so those flash-write stalls can't
    // reach the drop threshold. Interval is in 1.25 ms units, timeout in 10 ms units:
    //   min 15 ms (12), max 30 ms (24), latency 0, supervision timeout 6000 ms (600).
    _pServer->updateConnParams(_conn_handle, 12, 24, 0, 600);
    ESP_LOGI(TAG, "Requested OTA connection params (15-30 ms interval, 6 s supervision timeout)");
}

void ble_update_matrix_show(const bool show)
{
    uint8_t value[1] = { (uint8_t)(show ? 0x01 : 0x00) };
    _mctnc_char->setValue(value, sizeof(value));
    _mctnc_char->notify();
}

void start_advertising()
{
    ESP_LOGI(TAG, "Start advertising...");
    // The 128-bit service UUID (the app scans by it) fills most of the 31-byte advertising packet,
    // so the name goes into the scan response, as the Bluedroid stack did.
    NimBLEAdvertisementData adv_data;
    adv_data.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    adv_data.addServiceUUID(NimBLEUUID(MC_SERVICE_UUID));
    NimBLEAdvertisementData scan_data;
    scan_data.setName(BLE_SERVER_NAME);

    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->setAdvertisementData(adv_data);
    pAdvertising->setScanResponseData(scan_data);
    pAdvertising->start();
    ESP_LOGI(TAG, "OK (advertising started)");
}
