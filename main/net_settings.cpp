#include <Arduino.h>
#include <Preferences.h>
#include <esp_log.h>

#include "net_settings.h"

static const char* TAG = "NET_SETTINGS";

#define NET_SETTINGS_NAMESPACE  "network"
#define NET_KEY_MODE            "wifi_mode"
#define NET_KEY_PERIOD          "wifi_period"
#define NET_KEY_SSID            "wifi_ssid"
#define NET_KEY_PASSWORD        "wifi_pass"
#define NET_KEY_TZ              "tz"

#define NET_DEFAULT_PERIOD      (1U)

static Preferences _preferences;
static net_settings_t _settings;

// Changed from the BLE task, read by the WiFi task, flushed to NVS from the main loop.
static portMUX_TYPE _settings_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool _settings_dirty = false;

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    size_t len = strnlen(src, dst_size - 1);
    memcpy(dst, src, len);
    dst[len] = 0;
}

bool net_settings_init()
{
    memset(&_settings, 0, sizeof(_settings));
    _settings.mode = net_wifi_off;
    _settings.period_hours = NET_DEFAULT_PERIOD;
    copy_string(_settings.tz, sizeof(_settings.tz), NET_DEFAULT_TZ);

    if (!_preferences.begin(NET_SETTINGS_NAMESPACE, false))
    {
        ESP_LOGE(TAG, "Error: preferences.begin() failed.");
        return false;
    }

    uint8_t mode = _preferences.getUChar(NET_KEY_MODE, net_wifi_off);
    uint8_t period = _preferences.getUChar(NET_KEY_PERIOD, NET_DEFAULT_PERIOD);
    if (mode <= net_wifi_periodic)
        _settings.mode = mode;
    if (period >= NET_WIFI_PERIOD_MIN && period <= NET_WIFI_PERIOD_MAX)
        _settings.period_hours = period;

    // getString() logs an error for a missing key: check first
    if (_preferences.isKey(NET_KEY_SSID))
        _preferences.getString(NET_KEY_SSID, _settings.ssid, sizeof(_settings.ssid));
    if (_preferences.isKey(NET_KEY_PASSWORD))
        _preferences.getString(NET_KEY_PASSWORD, _settings.password, sizeof(_settings.password));
    if (_preferences.isKey(NET_KEY_TZ))
    {
        char tz[sizeof(_settings.tz)] = { 0 };
        if (_preferences.getString(NET_KEY_TZ, tz, sizeof(tz)) > 0 && tz[0])
            copy_string(_settings.tz, sizeof(_settings.tz), tz);
    }

    ESP_LOGI(TAG, "WiFi mode %d, period %d h, SSID '%s', TZ '%s'",
        _settings.mode, _settings.period_hours, _settings.ssid, _settings.tz);
    return true;
}

void net_settings_get(net_settings_t *out)
{
    portENTER_CRITICAL(&_settings_mux);
    memcpy(out, &_settings, sizeof(_settings));
    portEXIT_CRITICAL(&_settings_mux);
}

bool net_settings_set_wifi(uint8_t mode, uint8_t period_hours, const char *ssid, const char *password)
{
    if (mode > net_wifi_periodic ||
        period_hours < NET_WIFI_PERIOD_MIN || period_hours > NET_WIFI_PERIOD_MAX ||
        ssid == nullptr || strlen(ssid) > NET_SSID_MAX_LEN ||
        (password != nullptr && strlen(password) > NET_PASSWORD_MAX_LEN) ||
        (mode != net_wifi_off && ssid[0] == 0))
        return false;

    portENTER_CRITICAL(&_settings_mux);
    _settings.mode = mode;
    _settings.period_hours = period_hours;
    copy_string(_settings.ssid, sizeof(_settings.ssid), ssid);
    if (password != nullptr)
        copy_string(_settings.password, sizeof(_settings.password), password);
    _settings_dirty = true;
    portEXIT_CRITICAL(&_settings_mux);
    return true;
}

bool net_settings_set_tz(const char *tz)
{
    if (tz == nullptr || tz[0] == 0 || strlen(tz) > NET_TZ_MAX_LEN)
        return false;

    portENTER_CRITICAL(&_settings_mux);
    copy_string(_settings.tz, sizeof(_settings.tz), tz);
    _settings_dirty = true;
    portEXIT_CRITICAL(&_settings_mux);
    return true;
}

bool net_settings_is_dirty()
{
    return _settings_dirty;
}

bool net_settings_flush()
{
    net_settings_t settings;

    portENTER_CRITICAL(&_settings_mux);
    if (!_settings_dirty)
    {
        portEXIT_CRITICAL(&_settings_mux);
        return true;
    }
    memcpy(&settings, &_settings, sizeof(settings));
    // Cleared before the write: a change arriving meanwhile gets flushed on the next loop iteration
    _settings_dirty = false;
    portEXIT_CRITICAL(&_settings_mux);

    bool ok = _preferences.putUChar(NET_KEY_MODE, settings.mode) == 1 &&
              _preferences.putUChar(NET_KEY_PERIOD, settings.period_hours) == 1 &&
              _preferences.putString(NET_KEY_SSID, settings.ssid) == strlen(settings.ssid) &&
              _preferences.putString(NET_KEY_PASSWORD, settings.password) == strlen(settings.password) &&
              _preferences.putString(NET_KEY_TZ, settings.tz) == strlen(settings.tz);
    if (!ok)
        ESP_LOGE(TAG, "Error: writing the network settings into NVS failed.");
    return ok;
}
