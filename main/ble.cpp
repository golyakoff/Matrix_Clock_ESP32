#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>
#include <esp_log.h>

#include "time_helper.h"
#include "ble.h"
#include "net_settings.h"

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

// Network characteristics, created only when ble_set_network_callbacks() was called
static NimBLECharacteristic* _mctz_char = nullptr;
static NimBLECharacteristic* _mcws_char = nullptr;
static NimBLECharacteristic* _mcwst_char = nullptr;
static NimBLECharacteristic* _mcwn_char = nullptr;
static NimBLECharacteristic* _mccmd_char = nullptr;
static NimBLECharacteristic* _mcui_char = nullptr;
static NimBLECharacteristic* _mcos_char = nullptr;
static NimBLECharacteristic* _mcdn_char = nullptr;
static NimBLECharacteristic* _mcuc_char = nullptr;

// Variables
static bool _device_connected = false;

// The advertised (and GAP) name: BLE_SERVER_NAME, or the one from the network callbacks
static char _device_name[NET_NAME_MAX_LEN + 1] = BLE_SERVER_NAME;

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
static ble_on_disconnect_t _on_disconnect = nullptr;

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
        if (_on_disconnect)
            _on_disconnect();

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

// ---------------------------------------------------------------- network characteristics

#define MC_STR8_KEEP                   (0xFFU) // password length byte: keep the stored one / one is stored
#define MC_WIFI_STATUS_LEN             (10U)
#define MC_OTA_STATUS_LEN              (10U)
#define MC_WIFI_NETWORKS_HEADER_LEN    (4U)
#define MC_WIFI_NETWORKS_PAGE_MAX      (BLE_OTA_DATA_BUFFER) // MTU - 3: what a read returns in one go

static const ble_network_callbacks_t* _network_cb = nullptr;
static uint8_t _networks_cursor = 0; // the first network of the page the next read returns
static uint8_t _ota_status_value[MC_OTA_STATUS_LEN] = { 0 };

static void put_u32(uint8_t* p, uint32_t v)
{
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = (v >> 24) & 0xFF;
}

static void set_wifi_settings_value()
{
    uint8_t mode = 0;
    uint8_t period = 0;
    char ssid[NET_SSID_MAX_LEN + 1] = { 0 };
    bool password_stored = false;
    _network_cb->get_wifi(&mode, &period, ssid, &password_stored);

    uint8_t ssid_len = (uint8_t)strnlen(ssid, NET_SSID_MAX_LEN);
    uint8_t value[2 + 1 + NET_SSID_MAX_LEN + 1];
    size_t len = 0;
    value[len++] = mode;
    value[len++] = period;
    value[len++] = ssid_len;
    memcpy(value + len, ssid, ssid_len);
    len += ssid_len;
    value[len++] = password_stored ? MC_STR8_KEEP : 0;
    _mcws_char->setValue(value, len);
}

// Parses a WiFi settings write, see MC_WIFI_SETTINGS_CHAR_UUID
static bool write_wifi_settings(const uint8_t* data, size_t len)
{
    if (len < 4)
        return false;
    uint8_t mode = data[0];
    uint8_t period = data[1];
    uint8_t ssid_len = data[2];
    if (ssid_len > NET_SSID_MAX_LEN || len < 3U + ssid_len + 1U)
        return false;
    char ssid[NET_SSID_MAX_LEN + 1] = { 0 };
    memcpy(ssid, data + 3, ssid_len);

    size_t pos = 3U + ssid_len;
    uint8_t password_len = data[pos++];
    char password[NET_PASSWORD_MAX_LEN + 1] = { 0 };
    if (password_len == MC_STR8_KEEP)
    {
        if (len != pos)
            return false;
        return _network_cb->set_wifi(mode, period, ssid, nullptr);
    }
    if (password_len > NET_PASSWORD_MAX_LEN || len != pos + password_len)
        return false;
    memcpy(password, data + pos, password_len);
    return _network_cb->set_wifi(mode, period, ssid, password);
}

static void set_wifi_status_value()
{
    wifi_status_t status;
    _network_cb->get_wifi_status(&status);
    uint8_t value[MC_WIFI_STATUS_LEN];
    value[0] = status.state;
    memcpy(value + 1, status.ip, 4);
    value[5] = (uint8_t)status.rssi;
    put_u32(value + 6, status.synced_at);
    _mcwst_char->setValue(value, sizeof(value));
}

// One page of the scan result starting at _networks_cursor, see MC_WIFI_NETWORKS_CHAR_UUID
static void set_wifi_networks_value(bool header_only)
{
    static uint8_t value[MC_WIFI_NETWORKS_PAGE_MAX];
    const wifi_network_t* networks = nullptr;
    size_t count = 0;
    wifi_scan_state_t state = wifi_scan_lock(&networks, &count);
    if (state != wifi_scan_done)
        count = 0;

    size_t len = MC_WIFI_NETWORKS_HEADER_LEN;
    uint8_t first = _networks_cursor < count ? _networks_cursor : (uint8_t)count;
    uint8_t on_page = 0;
    for (size_t i = first; !header_only && i < count; i++)
    {
        uint8_t ssid_len = (uint8_t)strnlen(networks[i].ssid, sizeof(networks[i].ssid) - 1);
        if (len + 1 + ssid_len + 3 > sizeof(value))
            break;
        value[len++] = ssid_len;
        memcpy(value + len, networks[i].ssid, ssid_len);
        len += ssid_len;
        value[len++] = (uint8_t)networks[i].rssi;
        value[len++] = networks[i].security;
        value[len++] = networks[i].channel;
        on_page++;
    }

    value[0] = state;
    value[1] = (uint8_t)count;
    value[2] = first;
    value[3] = on_page;
    _mcwn_char->setValue(value, len);
    // Unlocked only now: the BLE task (reads) and the WiFi task (scan notifications) share value[]
    wifi_scan_unlock();
}

static void set_update_info_value()
{
    update_info_t info;
    _network_cb->get_update_info(&info);
    uint8_t version_len = (uint8_t)strnlen(info.version, sizeof(info.version) - 1);
    uint8_t value[1 + 1 + sizeof(info.version) + 4];
    size_t len = 0;
    value[len++] = info.state;
    value[len++] = version_len;
    memcpy(value + len, info.version, version_len);
    len += version_len;
    put_u32(value + len, info.checked_at);
    len += 4;
    _mcui_char->setValue(value, len);
}

class NetworkCharacteristicCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        NimBLEAttValue value = pCharacteristic->getValue();
        const uint8_t* data = value.data();
        const size_t len = value.size();

        if (pCharacteristic == _mctz_char)
        {
            char tz[NET_TZ_MAX_LEN + 1] = { 0 };
            if (len == 0 || len > NET_TZ_MAX_LEN || !_network_cb->set_tz((const char*)memcpy(tz, data, len)))
                ESP_LOGE(TAG, "Error: invalid time zone (%d bytes)", (int)len);
            return;
        }

        if (pCharacteristic == _mcws_char)
        {
            if (!write_wifi_settings(data, len))
                ESP_LOGE(TAG, "Error: invalid WiFi settings (%d bytes)", (int)len);
            return;
        }

        if (pCharacteristic == _mcwn_char)
        {
            if (len == 1)
                _networks_cursor = data[0];
            return;
        }

        if (pCharacteristic == _mccmd_char)
        {
            if (len >= 1)
                _network_cb->command(data[0]);
            return;
        }

        if (pCharacteristic == _mcuc_char)
        {
            if (len != 1 || !_network_cb->set_update_channel(data[0]))
                ESP_LOGE(TAG, "Error: invalid update channel (%d bytes)", (int)len);
            else
                ESP_LOGI(TAG, "Update channel %d", data[0]);
            return;
        }

        if (pCharacteristic == _mcdn_char)
        {
            char name[NET_NAME_MAX_LEN + 1] = { 0 };
            if (len > NET_NAME_MAX_LEN || !_network_cb->set_name((const char*)memcpy(name, data, len)))
            {
                ESP_LOGE(TAG, "Error: invalid device name (%d bytes)", (int)len);
                return;
            }
            // Advertised from the next disconnect on: the clock does not advertise while connected
            _network_cb->get_name(_device_name);
            NimBLEDevice::setDeviceName(_device_name);
            ESP_LOGI(TAG, "Device name '%s'", _device_name);
            return;
        }
    }

    void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
    {
        if (pCharacteristic == _mctz_char)
        {
            char tz[NET_TZ_MAX_LEN + 1] = { 0 };
            _network_cb->get_tz(tz);
            pCharacteristic->setValue((const uint8_t*)tz, strnlen(tz, NET_TZ_MAX_LEN));
            return;
        }

        if (pCharacteristic == _mcdn_char)
        {
            pCharacteristic->setValue((const uint8_t*)_device_name, strnlen(_device_name, NET_NAME_MAX_LEN));
            return;
        }

        if (pCharacteristic == _mcuc_char)
        {
            uint8_t channel = _network_cb->get_update_channel();
            pCharacteristic->setValue(&channel, 1);
            return;
        }

        if (pCharacteristic == _mcws_char)
            set_wifi_settings_value();
        else if (pCharacteristic == _mcwst_char)
            set_wifi_status_value();
        else if (pCharacteristic == _mcwn_char)
            set_wifi_networks_value(false);
        else if (pCharacteristic == _mcui_char)
            set_update_info_value();
    }
};
NetworkCharacteristicCallbacks _networkCharacteristicCallbacks;

void ble_set_disconnect_callback(ble_on_disconnect_t callback)
{
    _on_disconnect = callback;
}

void ble_set_network_callbacks(const ble_network_callbacks_t *callbacks)
{
    _network_cb = callbacks;
}

// Creates the network characteristics (ble_init())
static void ble_add_network_characteristics(NimBLEService *bleService)
{
    const uint32_t RW = NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE;

    ESP_LOGI(TAG, "Add MatrixClock network characteristics...");
    _mctz_char = bleService->createCharacteristic(MC_TIME_ZONE_CHAR_UUID, RW);
    _mctz_char->setCallbacks(&_networkCharacteristicCallbacks);

    // The password goes over the air: writing needs an encrypted link (the phone pairs on the first
    // write, "Just Works")
    _mcws_char = bleService->createCharacteristic(MC_WIFI_SETTINGS_CHAR_UUID, RW | NIMBLE_PROPERTY::WRITE_ENC);
    _mcws_char->setCallbacks(&_networkCharacteristicCallbacks);

    _mcwst_char = bleService->createCharacteristic(MC_WIFI_STATUS_CHAR_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    _mcwst_char->setCallbacks(&_networkCharacteristicCallbacks);

    _mcwn_char = bleService->createCharacteristic(MC_WIFI_NETWORKS_CHAR_UUID, RW | NIMBLE_PROPERTY::NOTIFY, MC_WIFI_NETWORKS_PAGE_MAX);
    _mcwn_char->setCallbacks(&_networkCharacteristicCallbacks);

    _mccmd_char = bleService->createCharacteristic(MC_COMMAND_CHAR_UUID, NIMBLE_PROPERTY::WRITE);
    _mccmd_char->setCallbacks(&_networkCharacteristicCallbacks);

    _mcui_char = bleService->createCharacteristic(MC_UPDATE_INFO_CHAR_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    _mcui_char->setCallbacks(&_networkCharacteristicCallbacks);

    _mcos_char = bleService->createCharacteristic(MC_OTA_STATUS_CHAR_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    _mcos_char->setValue(_ota_status_value, sizeof(_ota_status_value));

    _mcdn_char = bleService->createCharacteristic(MC_DEVICE_NAME_CHAR_UUID, RW, NET_NAME_MAX_LEN);
    _mcdn_char->setCallbacks(&_networkCharacteristicCallbacks);

    _mcuc_char = bleService->createCharacteristic(MC_UPDATE_CHANNEL_CHAR_UUID, RW, 1);
    _mcuc_char->setCallbacks(&_networkCharacteristicCallbacks);
}

void ble_update_wifi_status()
{
    if (!_mcwst_char)
        return;
    set_wifi_status_value();
    _mcwst_char->notify();
}

void ble_update_wifi_scan()
{
    if (!_mcwn_char)
        return;
    _networks_cursor = 0;
    set_wifi_networks_value(true);
    _mcwn_char->notify();
}

void ble_update_update_info()
{
    if (!_mcui_char)
        return;
    set_update_info_value();
    _mcui_char->notify();
}

void ble_update_ota_status(uint8_t state, uint8_t error, uint32_t received, uint32_t total)
{
    if (!_mcos_char)
        return;
    _ota_status_value[0] = state;
    _ota_status_value[1] = error;
    put_u32(_ota_status_value + 2, received);
    put_u32(_ota_status_value + 6, total);
    _mcos_char->setValue(_ota_status_value, sizeof(_ota_status_value));
    _mcos_char->notify();
}

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
    if (_network_cb)
        _network_cb->get_name(_device_name);
    ESP_LOGI(TAG, "Initialize BLE device '%s'...", _device_name);
    NimBLEDevice::init(_device_name);
    ESP_LOGI(TAG, "OK (BLE device initialized)");

    ESP_LOGI(TAG, "Set MTU to '%d'...", BLE_DEVICE_MTU);
    NimBLEDevice::setMTU(BLE_DEVICE_MTU);
    ESP_LOGI(TAG, "OK (actual MTU value now: %d)", NimBLEDevice::getMTU());

    // Bonding with "Just Works" pairing and LE Secure Connections, for the encrypted WiFi settings
    // write; the other characteristics stay open
    if (_network_cb)
        NimBLEDevice::setSecurityAuth(true, false, true);
    
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

    if (_network_cb)
        ble_add_network_characteristics(bleService);

    // Start the server (registers the service with the stack)
    ESP_LOGI(TAG, "pServer->start()...");
    pServer->start();
    ESP_LOGI(TAG, "OK (BLE server successfully started)");
    // Starting the server resets the GAP service, and with it the Device Name characteristic to
    // NimBLE's default "nimble" (the name Android shows in the pairing dialog)
    NimBLEDevice::setDeviceName(_device_name);

    // Start advertising
    start_advertising();
}

void ble_update_rtc_time(struct tm *dt)
{
    // Every 30 s: the link and advertising state, and advertising again if it stopped while no
    // phone is connected (the clock would no longer show up in the app's list)
    static int last_check = -1;
    const int now = dt->tm_min * 60 + dt->tm_sec;
    if (dt->tm_sec % 30 == 0 && now != last_check)
    {
        last_check = now;
        const int connected = NimBLEDevice::getServer()->getConnectedCount();
        const bool advertising = NimBLEDevice::getAdvertising()->isAdvertising();
        ESP_LOGD(TAG, "State: %d connected, advertising %d", connected, advertising);
        if (connected == 0 && !advertising)
        {
            ESP_LOGW(TAG, "Not connected and not advertising: advertising again");
            start_advertising();
        }
    }

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
            dt->tm_mon + 1,
            dt->tm_mday,
            dt->tm_hour,
            dt->tm_min,
            dt->tm_sec);
        memset(_mcts_char_buffer, 0, sizeof(_mcts_char_buffer));
        memcpy(_mcts_char_buffer, formatted, sizeof(_mcts_char_buffer) - 1);
        
        _mcts_char_value.assign(_mcts_char_buffer, sizeof(_mcts_char_buffer));
        _mcts_char->setValue(_mcts_char_value);
        _mcts_char->notify();

        _mct_char_value = (uint32_t)tm_to_seconds(dt); // the fields as UTC, as the app writes them
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
    scan_data.setName(_device_name);

    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->setAdvertisementData(adv_data);
    pAdvertising->setScanResponseData(scan_data);
    if (pAdvertising->start())
        ESP_LOGI(TAG, "OK (advertising started)");
    else
        ESP_LOGE(TAG, "Error: advertising did not start");
}
