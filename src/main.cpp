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

#define RTC_SPEED   (400000U)
#define RTC_SCL     (26U)
#define RTC_SDA     (27U)
#define RTC_SQW     (35U)

#define TOUCH_PIN   (12U)
#define TOUCH_THRESHOLD (40U)

#define OTA_FAILED_REBOOT_DELAY_MS (10000UL)

static const char* TAG = "MAIN";

static unsigned long _10ms_loop_due = 0;
static bool _ota_failed = false;
static unsigned long _ota_failed_at = 0;

static struct tm _rtc_dt;
static struct tm _matrix_dt;
static struct tm _matrix_dt_prev;

void alarm_off_callback(const uint8_t hours, const uint8_t minutes);
static Alarm alarmOff = Alarm(&alarm_off_callback);

void alarm_on_callback(const uint8_t hours, const uint8_t minutes);
static Alarm alarmOn = Alarm(&alarm_on_callback);

RealTimeClock rtc;

IRAM_ATTR void touch_callback_isr();
static volatile uint32_t _touch_last_interrupt_time = 0;
static const uint32_t _touch_debounce_delay = 100;
static TouchSensor touchSensor = TouchSensor(TOUCH_PIN, touch_callback_isr, TOUCH_THRESHOLD);

void main_rtc_init();
void main_ble_init();
void main_brightness_schedule_init();
void main_matrix_init();
void main_brightness_schedule_flush();

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
    main_ble_init();
    delay(500);
    main_brightness_schedule_init();
    main_matrix_init();
}

void setup()
{
    Serial.begin(115200);
    main_clock_init();
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
    }
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

                // Disable all ISRs but BLE
                main_ota_init();
                
                // Start OTA update
                if (ota_begin(
                    firmware_size,
                    ota_progress_callback,
                    ota_status_callback,
                    ota_completion_callback,
                    matrix_pause_timers,
                    matrix_resume_timers)
                ) {
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
    float raw_percent = (total > 0) ? (received * 100.0f) / total : 0.0f;
    float overall_percent = 5.0f + raw_percent * 0.94f;
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

void main_ble_init()
{
    ble_init(
        &set_rtc_time_on_ble_write,
        &set_matrix_show_on_ble_write,
        &get_matrix_show_on_ble_read,
        &set_matrix_auto_brightness_on_ble_write,
        &get_matrix_auto_brightness_on_ble_read,
        &set_matrix_manual_brightness_on_ble_write,
        &get_matrix_manual_brightness_on_ble_read,
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
    uint32_t current_millis = millis();
    if (current_millis - _touch_last_interrupt_time < _touch_debounce_delay)
        return;

    bool new_matrix_show = !matrix_get_show();
    matrix_set_show(new_matrix_show);
    ble_update_matrix_show(new_matrix_show);

    _touch_last_interrupt_time = current_millis;
}
