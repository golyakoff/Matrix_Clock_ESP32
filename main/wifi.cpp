#include <string.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_netif_sntp.h>
#include <esp_timer.h>
#include <esp_wifi.h>

#include "wifi.h"
#include "net_settings.h"
#include "updater.h"

static const char* TAG = "WIFI";

#define WIFI_HOSTNAME                   "tetris-clock"
#define WIFI_SNTP_SERVER                "pool.ntp.org"
#define MC_WIFI_TASK_STACK_SIZE            (8192U)  // TLS handshake and JSON parsing of the update check
#define MC_WIFI_TASK_PRIORITY              (3U)
#define WIFI_FIRST_SESSION_DELAY_MS     (5000U)  // the first session after boot or a settings change
#define WIFI_SESSION_CONNECT_MS         (30000U) // connection timeout of a session
#define WIFI_SESSION_RETRY_MS           (10U * 60U * 1000U) // retry after a failed session
#define WIFI_SNTP_WAIT_MS               (15000U)
#define WIFI_SCAN_TIMEOUT_MS            (10000U)
#define WIFI_SCAN_RECORDS_MAX           (80U)    // driver records read (with duplicates and hidden ones)
#define WIFI_MIN_VALID_TIME             (1767225600) // 2026-01-01 UTC: anything earlier is not a real time

#define BIT_GOT_IP       BIT0
#define BIT_DISCONNECTED BIT1
#define BIT_REQUEST      BIT2 // settings changed, pause/resume
#define BIT_JOB          BIT3 // something needs the internet
#define BIT_SCAN         BIT4 // scan requested
#define BIT_SCAN_DONE    BIT5

static esp_netif_t *_iface = nullptr;
static EventGroupHandle_t _events = nullptr;
static wifi_status_cb_t _status_cb = nullptr;
static wifi_scan_cb_t _scan_cb = nullptr;
static wifi_time_cb_t _time_cb = nullptr;

static volatile wifi_state_t _state = wifi_state_off;
static volatile uint32_t _ip_addr = 0;
static volatile uint32_t _synced_at = 0;
static volatile bool _paused = false;
static volatile bool _want_connect = false; // connect on STA_START
static volatile uint8_t _last_reason = 0;
static bool _started = false;
static bool _sntp_running = false;

static SemaphoreHandle_t _scan_mutex = nullptr;
static volatile wifi_scan_state_t _scan_state = wifi_scan_none;
static wifi_network_t _networks[WIFI_SCAN_MAX];
static size_t _networks_count = 0;
static volatile bool _scan_ok = false; // result of the last WIFI_EVENT_SCAN_DONE

static void set_state(wifi_state_t s)
{
    if (_state == s)
        return;
    _state = s;
    if (_status_cb)
        _status_cb();
}

static bool is_error_reason(uint8_t reason)
{
    switch (reason)
    {
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_NO_AP_FOUND:
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
            return true;
        default:
            return false;
    }
}

static void wifi_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id)
    {
        case WIFI_EVENT_STA_START:
            if (_want_connect)
                esp_wifi_connect();
            break;
        case WIFI_EVENT_SCAN_DONE:
            _scan_ok = ((wifi_event_sta_scan_done_t *)data)->status == 0;
            xEventGroupSetBits(_events, BIT_SCAN_DONE);
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
        {
            wifi_event_sta_disconnected_t *e = (wifi_event_sta_disconnected_t *)data;
            _last_reason = e->reason;
            _ip_addr = 0;
            ESP_LOGI(TAG, "WiFi disconnected, reason %d", e->reason);
            set_state(_started && is_error_reason(e->reason) ? wifi_state_error
                    : _started ? wifi_state_connecting : wifi_state_off);
            xEventGroupSetBits(_events, BIT_DISCONNECTED);
            break;
        }
        default:
            break;
    }
}

static void ip_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id != IP_EVENT_STA_GOT_IP)
        return;
    ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
    _ip_addr = e->ip_info.ip.addr;
    ESP_LOGI(TAG, "WiFi connected, IP " IPSTR, IP2STR(&e->ip_info.ip));
    set_state(wifi_state_connected);
    xEventGroupSetBits(_events, BIT_GOT_IP);
}

static void sntp_synced(struct timeval *tv)
{
    if (tv->tv_sec < WIFI_MIN_VALID_TIME)
        return;
    _synced_at = (uint32_t)tv->tv_sec;
    if (_time_cb)
        _time_cb(tv->tv_sec);
    if (_status_cb)
        _status_cb();
}

static void sntp_start()
{
    if (_sntp_running)
        return;
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(WIFI_SNTP_SERVER);
    cfg.sync_cb = sntp_synced;
    if (esp_netif_sntp_init(&cfg) == ESP_OK)
        _sntp_running = true;
}

static void sntp_stop()
{
    if (!_sntp_running)
        return;
    esp_netif_sntp_deinit();
    _sntp_running = false;
}

static void apply_tz()
{
    net_settings_t settings;
    net_settings_get(&settings);
    setenv("TZ", settings.tz, 1);
    tzset();
}

static esp_err_t start()
{
    net_settings_t settings;
    net_settings_get(&settings);

    wifi_config_t cfg = {};
    // A 32 byte SSID / 64 byte password fills the field without a terminating zero, that is valid
    memcpy(cfg.sta.ssid, settings.ssid, strnlen(settings.ssid, sizeof(cfg.sta.ssid)));
    memcpy(cfg.sta.password, settings.password, strnlen(settings.password, sizeof(cfg.sta.password)));
    cfg.sta.threshold.authmode = settings.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    esp_err_t res = esp_wifi_set_mode(WIFI_MODE_STA);
    if (res == ESP_OK)
        res = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (res != ESP_OK)
        return res;

    ESP_LOGI(TAG, "WiFi: connecting to '%s'", settings.ssid);
    xEventGroupClearBits(_events, BIT_GOT_IP | BIT_DISCONNECTED);
    _want_connect = true;
    _started = true;
    set_state(wifi_state_connecting);
    return esp_wifi_start();
}

static void stop()
{
    if (!_started)
        return;
    sntp_stop();
    _want_connect = false;
    _started = false;
    esp_wifi_stop();
    _ip_addr = 0;
    set_state(wifi_state_off);
    ESP_LOGI(TAG, "WiFi: stopped");
}

static bool current_mode_periodic()
{
    if (_paused)
        return false;
    net_settings_t settings;
    net_settings_get(&settings);
    return settings.mode == net_wifi_periodic && settings.ssid[0];
}

static uint32_t current_period_ms()
{
    net_settings_t settings;
    net_settings_get(&settings);
    return settings.period_hours * 3600000UL;
}

// Waits for any of the bits or the timeout, returns the bits that were set
static EventBits_t wait(EventBits_t bits, uint32_t timeout_ms)
{
    return xEventGroupWaitBits(_events, bits, pdTRUE, pdFALSE,
            timeout_ms == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms));
}

////////////////////////////////////////////////////////////////////////////////
/// Scan

static uint8_t security(wifi_auth_mode_t mode)
{
    switch (mode)
    {
        case WIFI_AUTH_OPEN:
        case WIFI_AUTH_OWE:
            return wifi_security_open;
        case WIFI_AUTH_WEP:
            return wifi_security_wep;
        case WIFI_AUTH_ENTERPRISE:
        case WIFI_AUTH_WPA3_ENT_192:
        case WIFI_AUTH_WPA3_ENTERPRISE:
        case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
        case WIFI_AUTH_WPA_ENTERPRISE:
            return wifi_security_enterprise;
        default:
            return wifi_security_personal;
    }
}

static void scan_finish(wifi_scan_state_t s)
{
    _scan_state = s;
    if (_scan_cb)
        _scan_cb();
}

// Unique visible SSIDs, the strongest record of each, sorted by RSSI
static void scan_collect()
{
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n > WIFI_SCAN_RECORDS_MAX)
        n = WIFI_SCAN_RECORDS_MAX;
    wifi_ap_record_t *recs = n ? (wifi_ap_record_t *)calloc(n, sizeof(wifi_ap_record_t)) : nullptr;
    if (!recs || esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK)
        n = 0;
    // frees the records that did not fit
    esp_wifi_clear_ap_list();

    xSemaphoreTake(_scan_mutex, portMAX_DELAY);
    _networks_count = 0;
    for (uint16_t i = 0; i < n; i++)
    {
        const char *ssid = (const char *)recs[i].ssid;
        if (!ssid[0])
            continue;
        size_t j = 0;
        while (j < _networks_count && strcmp(_networks[j].ssid, ssid))
            j++;
        if (j < _networks_count)
        {
            if (recs[i].rssi <= _networks[j].rssi)
                continue;
        }
        else if (_networks_count < WIFI_SCAN_MAX)
            j = _networks_count++;
        else
            continue;
        memcpy(_networks[j].ssid, recs[i].ssid, sizeof(_networks[j].ssid) - 1);
        _networks[j].ssid[sizeof(_networks[j].ssid) - 1] = 0;
        _networks[j].rssi = recs[i].rssi;
        _networks[j].security = security(recs[i].authmode);
        _networks[j].channel = recs[i].primary;
    }
    // insertion sort, strongest first
    for (size_t i = 1; i < _networks_count; i++)
        for (size_t j = i; j > 0 && _networks[j].rssi > _networks[j - 1].rssi; j--)
        {
            wifi_network_t t = _networks[j];
            _networks[j] = _networks[j - 1];
            _networks[j - 1] = t;
        }
    xSemaphoreGive(_scan_mutex);
    free(recs);
}

// Runs in the WiFi task. Starts the radio for the scan if it is not running.
static void scan()
{
    if (_paused)
    {
        scan_finish(wifi_scan_error);
        return;
    }
    bool own_radio = !_started;
    esp_err_t res = ESP_OK;
    if (own_radio)
    {
        // _want_connect is false: STA_START does not connect
        res = esp_wifi_set_mode(WIFI_MODE_STA);
        if (res == ESP_OK)
            res = esp_wifi_start();
    }
    xEventGroupClearBits(_events, BIT_SCAN_DONE);
    if (res == ESP_OK)
        res = esp_wifi_scan_start(nullptr, false);
    bool ok = false;
    if (res != ESP_OK)
        ESP_LOGW(TAG, "WiFi: could not start scan: %d (%s)", res, esp_err_to_name(res));
    else if (xEventGroupWaitBits(_events, BIT_SCAN_DONE, pdTRUE, pdTRUE, pdMS_TO_TICKS(WIFI_SCAN_TIMEOUT_MS)) & BIT_SCAN_DONE)
        ok = _scan_ok;
    else
    {
        ESP_LOGW(TAG, "WiFi: scan timeout");
        esp_wifi_scan_stop();
    }

    if (ok)
        scan_collect();
    else
        esp_wifi_clear_ap_list();
    if (own_radio)
        esp_wifi_stop();

    if (ok)
        ESP_LOGI(TAG, "WiFi: scan done, %u networks", (unsigned)_networks_count);
    scan_finish(ok ? wifi_scan_done : wifi_scan_error);
}

////////////////////////////////////////////////////////////////////////////////

// One session of the periodic mode, returns true on success
static bool run_session()
{
    if (start() != ESP_OK)
    {
        stop();
        updater_fail_pending();
        return false;
    }

    bool ok = false;
    int64_t deadline = esp_timer_get_time() + WIFI_SESSION_CONNECT_MS * 1000LL;
    while (esp_timer_get_time() < deadline)
    {
        EventBits_t b = wait(BIT_GOT_IP | BIT_DISCONNECTED | BIT_REQUEST,
                (deadline - esp_timer_get_time()) / 1000 + 1);
        if (b & (BIT_REQUEST | BIT_GOT_IP))
        {
            ok = b & BIT_GOT_IP;
            break;
        }
        if (b & BIT_DISCONNECTED)
        {
            if (is_error_reason(_last_reason))
                break;
            esp_wifi_connect();
        }
    }

    if (ok)
    {
        apply_tz();
        sntp_start();
        if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(WIFI_SNTP_WAIT_MS)) != ESP_OK)
            ESP_LOGW(TAG, "WiFi: time sync timeout");
        xEventGroupClearBits(_events, BIT_JOB);
        updater_run(true);
    }
    else
        updater_fail_pending();

    wifi_state_t result = _state;
    stop();
    // keep the error visible until the next session
    if (!ok)
        set_state(result == wifi_state_error ? wifi_state_error : wifi_state_off);
    return ok;
}

static void wifi_task(void *arg)
{
    int64_t next_session = esp_timer_get_time() + WIFI_FIRST_SESSION_DELAY_MS * 1000LL;

    while (true)
    {
        if (current_mode_periodic())
        {
            int64_t now = esp_timer_get_time();
            if (now >= next_session)
            {
                bool ok = run_session();
                uint64_t delay_ms = ok ? current_period_ms() : WIFI_SESSION_RETRY_MS;
                next_session = esp_timer_get_time() + delay_ms * 1000LL;
                ESP_LOGI(TAG, "WiFi: next session in %llu min", delay_ms / 60000);
                continue;
            }
            // a settings change starts a new session soon, a job right away
            EventBits_t b = wait(BIT_REQUEST | BIT_JOB | BIT_SCAN, (next_session - now) / 1000 + 1);
            if (b & BIT_SCAN)
                scan();
            if (b & BIT_JOB)
                next_session = 0;
            else if (b & BIT_REQUEST)
                next_session = esp_timer_get_time() + WIFI_FIRST_SESSION_DELAY_MS * 1000LL;
        }
        else
        {
            stop();
            if (_state != wifi_state_off && _state != wifi_state_error)
                set_state(wifi_state_off);
            EventBits_t b = wait(BIT_REQUEST | BIT_SCAN | BIT_JOB, UINT32_MAX);
            if (b & BIT_SCAN)
                scan();
            if (b & BIT_JOB)
                updater_fail_pending(); // a request came while WiFi is off or paused
            next_session = esp_timer_get_time() + WIFI_FIRST_SESSION_DELAY_MS * 1000LL;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

bool wifi_init(wifi_status_cb_t status_cb, wifi_scan_cb_t scan_cb, wifi_time_cb_t time_cb)
{
    _status_cb = status_cb;
    _scan_cb = scan_cb;
    _time_cb = time_cb;

    _events = xEventGroupCreate();
    _scan_mutex = xSemaphoreCreateMutex();
    if (!_events || !_scan_mutex)
        return false;

    apply_tz();

    if (esp_netif_init() != ESP_OK)
        return false;
    // The Arduino core may have created it already
    esp_err_t res = esp_event_loop_create_default();
    if (res != ESP_OK && res != ESP_ERR_INVALID_STATE)
        return false;

    _iface = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(_iface, WIFI_HOSTNAME);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK)
        return false;
    // the settings are kept by net_settings, do not wear the NVS
    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    if (esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_handler, nullptr, nullptr) != ESP_OK ||
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_handler, nullptr, nullptr) != ESP_OK)
        return false;

    if (xTaskCreate(wifi_task, "wifi", MC_WIFI_TASK_STACK_SIZE, nullptr, MC_WIFI_TASK_PRIORITY, nullptr) != pdPASS)
        return false;

    return true;
}

void wifi_apply_settings()
{
    apply_tz();
    if (_events)
        xEventGroupSetBits(_events, BIT_REQUEST);
}

bool wifi_is_usable()
{
    return current_mode_periodic();
}

bool wifi_request_session()
{
    if (!_events || !wifi_is_usable())
        return false;
    xEventGroupSetBits(_events, BIT_JOB);
    return true;
}

void wifi_pause(bool pause)
{
    if (_paused == pause)
        return;
    _paused = pause;
    ESP_LOGI(TAG, "WiFi %s", pause ? "paused" : "resumed");
    if (_events)
        xEventGroupSetBits(_events, BIT_REQUEST);
}

void wifi_get_status(wifi_status_t *s)
{
    memset(s, 0, sizeof(wifi_status_t));
    s->state = _state;
    s->synced_at = _synced_at;
    uint32_t addr = _ip_addr;
    if (_state == wifi_state_connected && addr)
    {
        memcpy(s->ip, &addr, 4); // network byte order
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
            s->rssi = ap.rssi;
    }
}

bool wifi_scan_start()
{
    if (!_events || !_scan_mutex || _paused)
        return false;
    if (_scan_state == wifi_scan_running)
        return true;
    xSemaphoreTake(_scan_mutex, portMAX_DELAY);
    _networks_count = 0;
    xSemaphoreGive(_scan_mutex);
    scan_finish(wifi_scan_running);
    xEventGroupSetBits(_events, BIT_SCAN);
    return true;
}

wifi_scan_state_t wifi_scan_lock(const wifi_network_t **networks, size_t *count)
{
    xSemaphoreTake(_scan_mutex, portMAX_DELAY);
    *networks = _networks;
    *count = _networks_count;
    return _scan_state;
}

void wifi_scan_unlock()
{
    xSemaphoreGive(_scan_mutex);
}
