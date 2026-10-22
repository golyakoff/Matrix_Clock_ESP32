/*******************************************************************
    Tetris clock that fetches its time Using the EzTimeLibrary

    For use with the ESP32
 *                                                                 *
    Written by Andrei Golyakoff using the code of Brian Lough
 *******************************************************************/

// Standard Libraries
#include <Arduino.h>
#include <Wire.h>
#include <esp_ota_ops.h>
#include <esp_log.h>

// Project includes
#include "time_helper.h"
#include "rtc.h"
#include "brightness_schedule.h"
#include "matrix.h"
#include "ble.h"
#include "alarm.h"
#include "touch_sensor.h"
#include "version.h"
#include "ota.h"
#include "net_settings.h"
#include "wifi.h"
#include "updater.h"

#define RTC_SPEED   (400000U)
#define RTC_SCL     (26U)
#define RTC_SDA     (27U)
#define RTC_SQW     (35U)

#define TOUCH_PIN   (12U)
// A touch makes the pad reading drop well below its untouched level; the old absolute threshold
// (40 with Arduino core 2.x units, the untouched level was ~70) was a ~40% drop
#define TOUCH_THRESHOLD_PERCENT (20.0f)

#define OTA_FAILED_REBOOT_DELAY_MS (10000UL)

// A freshly installed firmware is confirmed after running this long; if it crashes or hangs before
// that, the bootloader boots the previous one on the next reset
#define FIRMWARE_CONFIRM_DELAY_MS (30000UL)

static const char* TAG = "MAIN";

static unsigned long _10ms_loop_due = 0;
static bool _ota_failed = false;
static unsigned long _ota_failed_at = 0;
static bool _firmware_confirmed = false;

// The current OTA update comes from the internet (updater), not from the app over BLE
static volatile bool _ota_from_internet = false;

// Set by the WiFi task after an SNTP sync: loop() copies the system time into the RTC at the next
// second boundary
static volatile bool _net_time_pending = false;
static time_t _net_time_last_sec = 0;

static struct tm _rtc_dt;
static struct tm _matrix_dt;
static struct tm _matrix_dt_prev;

void alarm_off_callback(const uint8_t hours, const uint8_t minutes);
static Alarm alarmOff = Alarm(&alarm_off_callback);

void alarm_on_callback(const uint8_t hours, const uint8_t minutes);
static Alarm alarmOn = Alarm(&alarm_on_callback);

RealTimeClock rtc;

void touch_callback_isr();
static volatile uint32_t _touch_last_interrupt_time = 0;
// Set by the touch ISR: the BLE notification about the new "show" state is sent from loop(), since
// the BLE stack can't be called from an interrupt.
static volatile bool _touch_show_changed = false;
static const uint32_t _touch_debounce_delay = 100;
static TouchSensor touchSensor = TouchSensor(TOUCH_PIN, touch_callback_isr, TOUCH_THRESHOLD_PERCENT);

void main_rtc_init();
void main_ble_init();
void main_brightness_schedule_init();
void main_matrix_init();
void main_brightness_schedule_flush();
void main_firmware_confirm();
void main_network_init();
void main_net_settings_flush();
void main_net_time_apply();

void ota_progress_callback(size_t received, size_t total);
void ota_status_callback(const char* status, bool is_error);
void ota_completion_callback(bool success, const char* message);

void main_ota_init() {
    detachInterrupt(RTC_SQW);
    matrix_enter_ota_mode();
    touchSensor.Unload();

    // Clear any pending auto-reboot left over from an earlier failed attempt in this power cycle,
    // so it can't fire in the middle of this new attempt.
    _ota_failed = false;
}

void main_clock_init() {
    main_rtc_init();
    if (!net_settings_init())
        ESP_LOGE(TAG, "Error: net_settings_init(). The network settings are defaults until reboot.");
    main_ble_init();
    delay(500);
    main_brightness_schedule_init();
    main_matrix_init();
}

void setup()
{
    Serial.begin(115200);
    main_clock_init();
    // Publish how many Animation Splash animations this build ships so the app can show exactly the
    // set the connected clock has (the 16MB build may ship more than the 4MB one). BLE and matrix are
    // both up by now (main_clock_init()).
    ble_set_animation_count(matrix_get_animation_count());
    touchSensor.Init();
    main_network_init();
    ESP_LOGI(TAG, "Firmware version: %s", ota_get_current_version());
}

void loop()
{
    unsigned long now = millis();

    if (_ota_failed && (now - _ota_failed_at >= OTA_FAILED_REBOOT_DELAY_MS)) {
        ESP_LOGI(TAG, "Rebooting after failed OTA update...");
        ESP.restart();
    }

    if (now > _10ms_loop_due) {
        matrix_100hz_loop();

        _10ms_loop_due = now + 10;

        matrix_get_time(&_matrix_dt);
        if (times_are_different(&_matrix_dt, &_matrix_dt_prev))
        {
            ble_update_rtc_time(&_matrix_dt);
            memcpy(&_matrix_dt_prev, &_matrix_dt, sizeof(struct tm));
            alarmOn.tick(&_matrix_dt);
            alarmOff.tick(&_matrix_dt);
        }

        main_brightness_schedule_flush();
        main_net_settings_flush();
        main_net_time_apply();

        if (!_firmware_confirmed && now >= FIRMWARE_CONFIRM_DELAY_MS)
            main_firmware_confirm();

        if (_touch_show_changed) {
            _touch_show_changed = false;
            ble_update_matrix_show(matrix_get_show());
        }
    }
}

// Tells the Arduino core not to confirm a new firmware at start (initArduino()): main_firmware_confirm()
// does it once the clock has been running for a while
bool verifyRollbackLater()
{
    return true;
}

// Cancels the rollback of a freshly installed firmware (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE). Writes
// the otadata partition, so the display timers are stopped meanwhile, as for any flash write.
void main_firmware_confirm()
{
    _firmware_confirmed = true;

    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) != ESP_OK ||
        state != ESP_OTA_IMG_PENDING_VERIFY)
        return;

    matrix_pause_timers();
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    matrix_resume_timers();

    if (err != ESP_OK)
        ESP_LOGE(TAG, "Error: esp_ota_mark_app_valid_cancel_rollback(): %s", esp_err_to_name(err));
    else
        ESP_LOGI(TAG, "Firmware %s confirmed", ota_get_current_version());
}

// Writes the hourly brightness table received over BLE into NVS, if there is one pending.
// The display timers are stopped for the duration of the write: their ISRs execute code from flash,
// and the flash cache is disabled while NVS erases/writes its sector.
void main_brightness_schedule_flush()
{
    if (!brightness_schedule_is_dirty())
        return;

    ESP_LOGI(TAG, "Writing the pending hourly brightness table into NVS...");

    matrix_pause_timers();
    bool saved = brightness_schedule_flush();
    matrix_resume_timers();

    if (!saved)
        ESP_LOGE(TAG, "Error: main_brightness_schedule_flush() > brightness_schedule_flush()");
    else
        ESP_LOGI(TAG, "brightness_schedule_flush(): OK");
}

// RTC init with debug messages
void main_rtc_init()
{
    if (!rtc.init(RTC_SDA, RTC_SCL, RTC_SPEED))
    {
        for(;;) {
            ESP_LOGE(TAG, "Error: rtc.init(). RTC was not initialized!");
            delay(5000);
        }
    }    
    ESP_LOGI(TAG, "rtc.init(): OK");

    // Write out RTC chip aging offset
    int8_t rtc_ao = rtc.getAgingOffset();
    ESP_LOGI(TAG, "rtc.getAgingOffset(): OK : %d", rtc_ao);
    
    // Write out RTC chip temperature
    int8_t rtc_t;
    uint8_t rtc_f;
    if (!rtc.getTemperature(&rtc_t, &rtc_f))
    {
        for(;;) {
            ESP_LOGE(TAG, "Error: rtc.getTemperature(). Cannot read temperature from RTC!");
            delay(5000);
        }
    }
    ESP_LOGI(TAG, "rtc.getTemperature(): OK : %d.%d", rtc_t, rtc_f);

    // Write out RTC date and time
    if (!rtc.getTime(&_rtc_dt))
    {
        for(;;) {
            ESP_LOGE(TAG, "Error: rtc.getTime(). Cannot read time from RTC!");
            delay(5000);
        }
    }
    log_tm(TAG, "initialized _rtc_dt: ", &_rtc_dt);
    ESP_LOGI(TAG,
        "rtc.getTime(): OK : %d-%02d-%02d %02d:%02d:%02d",
        _rtc_dt.tm_year + 1900,
        _rtc_dt.tm_mon,
        _rtc_dt.tm_mday,
        _rtc_dt.tm_hour,
        _rtc_dt.tm_min,
        _rtc_dt.tm_sec);

    attachInterrupt(RTC_SQW, matrix_1hz_isr_loop, FALLING);

    // Initialize runtime alarms from RTC
    uint8_t hours;
    uint8_t minutes;
    bool active;
    rtc.loadAlarm(rtc_alarm_index_on, &hours, &minutes, &active);
    alarmOn.set(hours, minutes, active);
    rtc.loadAlarm(rtc_alarm_index_off, &hours, &minutes, &active);
    alarmOff.set(hours, minutes, active);

    // Initialize runtime matrix brightness settings from RTC
    bool use_auto_brightness;
    uint8_t manual_brightness_value;
    rtc.loadBrightness(&use_auto_brightness, &manual_brightness_value);
    matrix_set_auto_brightness(use_auto_brightness);
    matrix_set_manual_brightness(manual_brightness_value);

    // Initialize runtime matrix color order setting from RTC
    bool use_rrbbgg;
    rtc.loadColorOrder(&use_rrbbgg);
    matrix_set_color_order(use_rrbbgg);

    // Initialize runtime Animation Splash setting from RTC
    uint8_t animation_splash_mode;
    uint8_t animation_splash_duration;
    uint8_t animation_splash_index;
    rtc.loadAnimationSplash(&animation_splash_mode, &animation_splash_duration, &animation_splash_index);
    matrix_set_animation_splash(animation_splash_mode, animation_splash_duration, animation_splash_index);
}

// Hourly brightness schedule init with debug messages
void main_brightness_schedule_init()
{
    if (!brightness_schedule_init())
    {
        for(;;) {
            ESP_LOGE(TAG, "Error: brightness_schedule_init(). Hourly brightness schedule was not initialized!");
            delay(5000);
        }
    }
    ESP_LOGI(TAG, "brightness_schedule_init(): OK");
}

// Matrix init with debug message
void main_matrix_init()
{
    matrix_init(&_rtc_dt);
    ESP_LOGI(TAG, "matrix_init(): OK");
}

// BLE init
void set_rtc_time_on_ble_write(const struct tm *dt)
{
    // update rtc
    if (rtc.setTime(dt))
    {
        // update matrix
        matrix_set_time(dt, true);

        #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
        log_tm(TAG, "-> Runtime date and time updated with the value", dt);
        #endif

        return;
    }

    ESP_LOGE(TAG, "-> Error: Runtime date and time update failed");
}

void set_matrix_show_on_ble_write(bool show)
{
    matrix_set_show(show);

    #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "Called set_matrix_show_on_ble_write(%d)", show);
    #endif
}

bool get_matrix_show_on_ble_read()
{
    #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "Called get_matrix_show_on_ble_read()");
    #endif

    return matrix_get_show();
}

void set_matrix_auto_brightness_on_ble_write(bool auto_brightness)
{
    matrix_set_auto_brightness(auto_brightness);

    #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "Called set_matrix_auto_brightness_on_ble_write(%d)", auto_brightness);
    #endif

    if (!rtc.saveBrightness(auto_brightness, matrix_get_manual_brightness()))
        ESP_LOGE(TAG, "Error: set_matrix_auto_brightness_on_ble_write(...) > rtc.saveBrightness(...)");
}

bool get_matrix_auto_brightness_on_ble_read()
{
    #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "Called get_matrix_auto_brightness_on_ble_read()");
    #endif

    return matrix_get_auto_brightness();
}

void set_matrix_manual_brightness_on_ble_write(uint8_t manual_brightness)
{
    matrix_set_manual_brightness(manual_brightness);

    #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "Called set_matrix_manual_brightness_on_ble_write(%d)", manual_brightness);
    #endif

    if (!rtc.saveBrightness(matrix_get_auto_brightness(), manual_brightness))
        ESP_LOGE(TAG, "Error: set_matrix_manual_brightness_on_ble_write(...) > rtc.saveBrightness(...)");
}

uint8_t get_matrix_manual_brightness_on_ble_read()
{
    #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "Called get_matrix_manual_brightness_on_ble_read()");
    #endif

    return matrix_get_manual_brightness();
}

void set_matrix_color_order_on_ble_write(bool use_rrbbgg)
{
    matrix_set_color_order(use_rrbbgg);

    #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "Called set_matrix_color_order_on_ble_write(%d)", use_rrbbgg);
    #endif

    if (!rtc.saveColorOrder(use_rrbbgg))
        ESP_LOGE(TAG, "Error: set_matrix_color_order_on_ble_write(...) > rtc.saveColorOrder(...)");
}

bool get_matrix_color_order_on_ble_read()
{
    #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "Called get_matrix_color_order_on_ble_read()");
    #endif

    return matrix_get_color_order();
}

void set_animation_splash_on_ble_write(uint8_t mode, uint8_t duration, uint8_t index, bool preview_now)
{
    matrix_set_animation_splash(mode, duration, index);

    #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "Called set_animation_splash_on_ble_write(mode=%d, dur=%d, idx=%d, preview=%d)", mode, duration, index, preview_now);
    #endif

    if (!rtc.saveAnimationSplash(mode, duration, index))
        ESP_LOGE(TAG, "Error: set_animation_splash_on_ble_write(...) > rtc.saveAnimationSplash(...)");

    // A "play now" preview is transient and time-independent, so it isn't persisted - just fired.
    if (preview_now)
        matrix_request_animation_splash_preview();
}

void get_animation_splash_on_ble_read(uint8_t *mode, uint8_t *duration, uint8_t *index)
{
    #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "Called get_animation_splash_on_ble_read()");
    #endif

    matrix_get_animation_splash(mode, duration, index);
}

void set_matrix_hourly_brightness_on_ble_write(const uint8_t* data, size_t length)
{
    if (length != BRIGHTNESS_SCHEDULE_HOURS)
    {
        ESP_LOGE(TAG, "Error: hourly brightness table must be %d bytes, got %d", BRIGHTNESS_SCHEDULE_HOURS, length);
        return;
    }

    #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "Calling callback set_matrix_hourly_brightness_on_ble_write(...)");
    #endif

    // RAM only: the NVS write needs the display timers stopped, so it is done in loop().
    brightness_schedule_set(data);
}

void get_matrix_hourly_brightness_on_ble_read(uint8_t* table_out)
{
    #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "Called get_matrix_hourly_brightness_on_ble_read()");
    #endif

    brightness_schedule_load(table_out);
}

void set_matrix_alarm_on_ble_write(ble_alarm_index_t index, uint8_t hours, uint8_t minutes, bool active)
{
    #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "Called set_matrix_alarm_on_ble_write(%d, %02d, %02d, %d)", index, hours, active);
    #endif

    switch (index)
    {
        case ble_alarm_index_off:
            alarmOff.set(hours, minutes, active);
            if (!rtc.saveAlarm(rtc_alarm_index_off, hours, minutes, active))
                ESP_LOGE(TAG, "Error: set_matrix_alarm_on_ble_write(ble_alarm_index_off...) > rtc.saveAlarm(rtc_alarm_index_off...)");
            break;
        case ble_alarm_index_on:
            alarmOn.set(hours, minutes, active);
            if (!rtc.saveAlarm(rtc_alarm_index_on, hours, minutes, active))
                ESP_LOGE(TAG, "Error: set_matrix_alarm_on_ble_write(ble_alarm_index_on...) > rtc.setAlarm(ble_alarm_index_on...)");
            break;
        default:
            break;
            ESP_LOGE(TAG, "Error: Unknown alarm index '%d' in set_matrix_alarm_on_ble_write()", index);
    }
}

void get_matrix_alarm_on_ble_read(ble_alarm_index_t index, uint8_t *hours, uint8_t *minutes, bool *active)
{
    #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "Call get_matrix_alarm_on_ble_read(%d, %02d, %02d, %d)", index, hours, active);
    #endif

    switch (index)
    {
        case ble_alarm_index_off:
            alarmOff.get(hours, minutes, active);
            break;
        case ble_alarm_index_on:
            alarmOn.get(hours, minutes, active);
            break;
        default:
            break;
            ESP_LOGE(TAG, "Error: Unknown alarm index '%d' in get_matrix_alarm_on_ble_read()", index);
    }
}

int8_t get_rtc_temperature_ble_read()
{
    #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "Call get_rtc_temperature_ble_read()");
    #endif

    int8_t c;
    uint8_t f;
    if (!rtc.getTemperature(&c, &f))
    {
        ESP_LOGE(TAG, "Error: get_rtc_temperature_ble_read(...) > rtc.getTemperature(...)");
        return 0;
    }

    return c;
}

int8_t get_rtc_aging_offset_on_ble_read()
{
    #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "Call get_rtc_aging_offset_on_ble_read()");
    #endif

    return rtc.getAgingOffset();
}

void set_rtc_aging_offset_on_ble_write(const int8_t aging_offset)
{
    if (!rtc.setAgingOffset(aging_offset))
        ESP_LOGE(TAG, "Error: set_rtc_aging_offset_on_ble_write(...) > rtc.setAgingOffset(...)");

    #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "Called set_rtc_aging_offset_on_ble_write(%d)", aging_offset);
    #endif

}

std::string get_fw_ver_on_ble_read()
{
    #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG, "Called get_fw_ver_on_ble_read()");
    #endif

    return std::string(ota_get_current_version());
}

void set_ota_control_ble_write(const uint8_t* data, size_t length)
{
    if (length == 0) {
        ota_emit_status("Empty OTA control command", true);
        return;
    }

    switch (data[0]) {
        case 0x01: // Start OTA
            {
                uint32_t firmware_size = 
                    (data[1]) | 
                    (data[2] << 8) | 
                    (data[3] << 16) | 
                    (data[4] << 24);

                // Disable all ISRs but BLE, and WiFi with them: the radio is BLE's for the transfer
                _ota_from_internet = false;
                wifi_pause(true);
                main_ota_init();

                // Ask the phone for a long supervision timeout so the fast firmware stream's
                // flash-write stalls don't drop the link mid-transfer (GATT status 8).
                ble_request_ota_conn_params();

                // Start OTA update
                if (ota_begin(
                    firmware_size,
                    ota_progress_callback,
                    ota_status_callback,
                    ota_completion_callback,
                    matrix_pause_timers,
                    matrix_resume_timers)
                ) {
                    // Blank the panel for the transfer so the display refresh ISR stops competing
                    // for CPU/RF while the stream runs (the panel comes back only on failure). A
                    // long supervision timeout alone (ble_request_ota_conn_params()) was not enough
                    // to keep the link up under the fast release stream - the refresh ISR still
                    // starved BLE enough to drop it partway; blanking the panel is what carries it
                    // to 100%. Verified on-device: timeout-only dropped ~18%, both together = 100%.
                    matrix_ota_freeze_display();
                    ota_emit_status("OTA started successfully", false);
                } else {
                    ota_emit_status("OTA start failed", true);
                }
            }
            break;

        case 0x02: // End OTA
            if (ota_end()) {
                ota_emit_status("OTA completed successfully", false);
            } else {
                ota_emit_status("OTA completion failed", true);
            }
            break;
            
        case 0x03: // Abort OTA
            ota_abort();
            ota_emit_status("OTA aborted by command", false);
            break;
            
        default:
            {
                char error_msg[64];
                snprintf(error_msg, sizeof(error_msg),
                         "Unknown OTA control command: 0x%02x", data[0]);
                ota_emit_status(error_msg, true);
            }
            break;
    }
}

void set_ota_data_ble_write(const uint8_t* data, size_t length)
{
    if (!ota_write((uint8_t*)data, length)) {
        ota_emit_status("OTA data write failed", true);
    }
}

void ota_progress_callback(size_t received, size_t total) {
    // Match what the phone app shows: it downloads the .bin to the phone first, then sends it
    // over BLE (this "install" phase, which is all the device sees) and finally reboots/verifies,
    // and maps that onto one 0-100% bar as 2..5% (download) + 5..99% (install) + 99..100% (reboot).
    // See performCompleteUpdate()/installFirmwareInternal() in the Android app's FirmwareRepository.kt.
    // An update from the internet is all done by the clock: its progress is the whole bar.
    float raw_percent = (total > 0) ? (received * 100.0f) / total : 0.0f;
    float overall_percent = _ota_from_internet ? raw_percent : 5.0f + raw_percent * 0.94f;
    matrix_set_ota_progress(overall_percent);

    #if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG
    ESP_LOGD(TAG,
        "OTA Progress: %d/%d bytes (%.1f%%)",
        received,
        total,
        overall_percent);
    #endif
}

void ota_status_callback(const char* status, bool is_error) {
    if (is_error)
        ESP_LOGE(TAG, "OTA: %s", status);
    else
        ESP_LOGI(TAG, "OTA: %s", status);
}

void ota_completion_callback(bool success, const char* message) {
    if (!success) {
        ESP_LOGE(TAG, "OTA FAILED: %s", message);
        matrix_show_ota_failed();

        // Reboot on our own since the device has no power button: deferred to loop() (instead of
        // a blocking delay() here) so the BLE stack's write response for the command that got us
        // here isn't held up - this callback runs on the BLE stack's own task.
        _ota_failed = true;
        _ota_failed_at = millis();
    } else {
        ESP_LOGI(TAG, "OTA SUCCESS: %s", message);
    }
}


// ---------------------------------------------------------------- network: WiFi, time, updates

// Writes the network settings received over BLE into NVS, if there are pending ones (see
// main_brightness_schedule_flush() about the display timers).
void main_net_settings_flush()
{
    if (!net_settings_is_dirty())
        return;

    matrix_pause_timers();
    bool saved = net_settings_flush();
    matrix_resume_timers();

    if (!saved)
        ESP_LOGE(TAG, "Error: main_net_settings_flush() > net_settings_flush()");
}

// SNTP has set the system time (UTC, local via TZ): copy it into the RTC and the matrix exactly when
// a new second starts, so the RTC's 1 Hz output ticks in step with it (within one 10 ms loop tick).
void main_net_time_apply()
{
    if (!_net_time_pending)
        return;

    struct timeval tv;
    gettimeofday(&tv, nullptr);
    if (_net_time_last_sec == 0)
    {
        _net_time_last_sec = tv.tv_sec;
        return;
    }
    if (tv.tv_sec == _net_time_last_sec)
        return;

    _net_time_pending = false;
    _net_time_last_sec = 0;

    struct tm local;
    localtime_r(&tv.tv_sec, &local);
    if (rtc.setTime(&local))
    {
        matrix_set_time(&local, true);
        log_tm(TAG, "Time synced from the internet: ", &local);
    }
    else
        ESP_LOGE(TAG, "Error: main_net_time_apply() > rtc.setTime()");
}

// WiFi task callbacks
void wifi_status_changed()
{
    ble_update_wifi_status();
}

void wifi_scan_changed()
{
    ble_update_wifi_scan();
}

void wifi_time_synced(time_t utc)
{
    _net_time_last_sec = 0;
    _net_time_pending = true;
}

// Updater callbacks (WiFi task)
void update_info_changed()
{
    ble_update_update_info();
}

void update_ota_status_changed(uint8_t state, uint8_t error, uint32_t received, uint32_t total)
{
    ble_update_ota_status(state, error, received, total);
}

bool update_ota_begin(size_t firmware_size)
{
    _ota_from_internet = true;
    main_ota_init();
    if (ota_begin(
        firmware_size,
        ota_progress_callback,
        ota_status_callback,
        ota_completion_callback,
        matrix_pause_timers,
        matrix_resume_timers))
        return true;

    // Nothing was written: leave the OTA screen through the usual failure path (reboot in 10 s)
    ota_completion_callback(false, "OTA start failed");
    return false;
}

// BLE network characteristics callbacks (BLE stack's task)
void net_get_tz(char *tz_out)
{
    net_settings_t settings;
    net_settings_get(&settings);
    strcpy(tz_out, settings.tz);
}

bool net_set_tz(const char *tz)
{
    if (!net_settings_set_tz(tz))
        return false;
    // Applied right away, but without wifi_apply_settings(): the app writes the time zone on every
    // connection, which must not start a WiFi session each time
    setenv("TZ", tz, 1);
    tzset();
    return true;
}

void net_get_wifi(uint8_t *mode, uint8_t *period_hours, char *ssid_out, bool *password_stored)
{
    net_settings_t settings;
    net_settings_get(&settings);
    *mode = settings.mode;
    *period_hours = settings.period_hours;
    strcpy(ssid_out, settings.ssid);
    *password_stored = settings.password[0] != 0;
}

bool net_set_wifi(uint8_t mode, uint8_t period_hours, const char *ssid, const char *password)
{
    if (!net_settings_set_wifi(mode, period_hours, ssid, password))
        return false;
    wifi_apply_settings();
    return true;
}

void net_command(uint8_t command)
{
    switch (command)
    {
        case 0x01: // sync time now
            if (!wifi_request_session())
                ESP_LOGW(TAG, "Time sync requested, but WiFi is off");
            break;
        case 0x02: // check for an update
            updater_request_check();
            break;
        case 0x03: // install the update
            updater_request_install();
            break;
        case 0x04: // scan for networks
            if (!wifi_scan_start())
                ESP_LOGW(TAG, "WiFi scan not possible now");
            break;
        default:
            ESP_LOGE(TAG, "Error: unknown command 0x%02x", command);
            break;
    }
}

static const ble_network_callbacks_t _network_callbacks = {
    .get_tz = net_get_tz,
    .set_tz = net_set_tz,
    .get_wifi = net_get_wifi,
    .set_wifi = net_set_wifi,
    .get_wifi_status = wifi_get_status,
    .get_update_info = updater_get_info,
    .command = net_command,
};

// Starts WiFi and the updater; BLE has the network characteristics (main_ble_init())
void main_network_init()
{
    updater_init(update_info_changed, update_ota_status_changed, update_ota_begin);
    if (!wifi_init(wifi_status_changed, wifi_scan_changed, wifi_time_synced))
        ESP_LOGE(TAG, "Error: wifi_init(). No WiFi until reboot.");
    else
        ESP_LOGI(TAG, "wifi_init(): OK");
}

void main_ble_init()
{
    ble_set_network_callbacks(&_network_callbacks);
    ble_init(
        &set_rtc_time_on_ble_write,
        &set_matrix_show_on_ble_write,
        &get_matrix_show_on_ble_read,
        &set_matrix_auto_brightness_on_ble_write,
        &get_matrix_auto_brightness_on_ble_read,
        &set_matrix_manual_brightness_on_ble_write,
        &get_matrix_manual_brightness_on_ble_read,
        &set_matrix_color_order_on_ble_write,
        &get_matrix_color_order_on_ble_read,
        &set_animation_splash_on_ble_write,
        &get_animation_splash_on_ble_read,
        &set_matrix_hourly_brightness_on_ble_write,
        &get_matrix_hourly_brightness_on_ble_read,
        &set_matrix_alarm_on_ble_write,
        &get_matrix_alarm_on_ble_read,
        &get_rtc_temperature_ble_read,
        &get_rtc_aging_offset_on_ble_read,
        &set_rtc_aging_offset_on_ble_write,
        &get_fw_ver_on_ble_read,
        &set_ota_control_ble_write,
        &set_ota_data_ble_write);
}

 void alarm_off_callback(const uint8_t hours, const uint8_t minutes)
 {
    matrix_set_show(false);
    ble_update_matrix_show(false);
 }

 void alarm_on_callback(const uint8_t hours, const uint8_t minutes)
 {
    matrix_set_show(true);
    ble_update_matrix_show(true);
 }

IRAM_ATTR void touch_callback_isr()
{
    // The ISR fires on release too: act on the touch only
    if (!touchSensor.IsPressed())
        return;

    uint32_t current_millis = millis();
    if (current_millis - _touch_last_interrupt_time < _touch_debounce_delay)
        return;

    bool new_matrix_show = !matrix_get_show();
    matrix_set_show(new_matrix_show);
    _touch_show_changed = true;

    _touch_last_interrupt_time = current_millis;
}
