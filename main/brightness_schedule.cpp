#include <Arduino.h>
#include <Preferences.h>
#include <esp_log.h>

#include "brightness_schedule.h"

static const char* TAG = "BRIGHT_SCHED";

#define BRIGHTNESS_SCHEDULE_NAMESPACE "brightness"
#define BRIGHTNESS_SCHEDULE_KEY       "hourly"

// Default table: dim through the night, ramping up in the morning and back down in the evening.
static const uint8_t _default_table[BRIGHTNESS_SCHEDULE_HOURS] =
{
 // 00  01  02  03  04  05  06  07  08  09  10  11
    1,  1,  1,  1,  1,  1,  1,  2,  3,  4,  6,  8,
 // 12  13  14  15  16  17  18  19  20  21  22  23
    10, 12, 12, 12, 12, 12, 10,  8,  6,  4,  2,  1
};

static Preferences _preferences;
static uint8_t _table[BRIGHTNESS_SCHEDULE_HOURS];

// The table is updated from the BLE task and flushed to NVS from the main loop.
static portMUX_TYPE _table_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool _table_dirty = false;

bool brightness_schedule_init()
{
    if (!_preferences.begin(BRIGHTNESS_SCHEDULE_NAMESPACE, false))
    {
        ESP_LOGE(TAG, "Error: preferences.begin() failed.");
        return false;
    }

    size_t read_len = _preferences.getBytes(BRIGHTNESS_SCHEDULE_KEY, _table, sizeof(_table));
    if (read_len != sizeof(_table))
    {
        ESP_LOGI(TAG, "No valid hourly brightness table found in NVS, saving the default one.");
        memcpy(_table, _default_table, sizeof(_table));
        return brightness_schedule_save(_table);
    }

    return true;
}

bool brightness_schedule_save(const uint8_t table_24[BRIGHTNESS_SCHEDULE_HOURS])
{
    size_t written = _preferences.putBytes(BRIGHTNESS_SCHEDULE_KEY, table_24, BRIGHTNESS_SCHEDULE_HOURS);
    if (written != BRIGHTNESS_SCHEDULE_HOURS)
    {
        ESP_LOGE(TAG, "Error: preferences.putBytes() wrote %d of %d bytes.", written, BRIGHTNESS_SCHEDULE_HOURS);
        return false;
    }

    if (table_24 != _table)
        memcpy(_table, table_24, sizeof(_table));

    return true;
}

void brightness_schedule_set(const uint8_t table_24[BRIGHTNESS_SCHEDULE_HOURS])
{
    portENTER_CRITICAL(&_table_mux);
    memcpy(_table, table_24, sizeof(_table));
    _table_dirty = true;
    portEXIT_CRITICAL(&_table_mux);
}

bool brightness_schedule_is_dirty()
{
    return _table_dirty;
}

bool brightness_schedule_flush()
{
    uint8_t table[BRIGHTNESS_SCHEDULE_HOURS];

    portENTER_CRITICAL(&_table_mux);
    if (!_table_dirty)
    {
        portEXIT_CRITICAL(&_table_mux);
        return true;
    }
    memcpy(table, _table, sizeof(table));
    // Cleared before the write: a table arriving while we are writing marks the schedule
    // dirty again and gets flushed on the next loop iteration.
    _table_dirty = false;
    portEXIT_CRITICAL(&_table_mux);

    size_t written = _preferences.putBytes(BRIGHTNESS_SCHEDULE_KEY, table, sizeof(table));
    if (written != sizeof(table))
    {
        ESP_LOGE(TAG, "Error: preferences.putBytes() wrote %d of %d bytes.", (int)written, (int)sizeof(table));
        return false;
    }

    return true;
}

void brightness_schedule_load(uint8_t table_out_24[BRIGHTNESS_SCHEDULE_HOURS])
{
    memcpy(table_out_24, _table, sizeof(_table));
}

uint8_t brightness_schedule_get_for_hour(uint8_t hour)
{
    return _table[hour % BRIGHTNESS_SCHEDULE_HOURS];
}
