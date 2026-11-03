#include <Arduino.h>
#include <Preferences.h>
#include <esp_log.h>
#include <stdio.h>
#include <string.h>

#include "bonds.h"

// Names of the bonded phones, kept next to the Bond store. NimBLE stores the keys and the identity
// address of every bond, but not the phone's name: pairing (SMP) exchanges keys only. The app reads
// the Bluetooth name shown in the phone's settings and sends it with Command 0x05; the clock keeps it
// here in NVS, keyed by the identity address. Names of phones that are no longer bonded are pruned
// when the list is read.
//
// The BLE task only changes this table in RAM; loop() writes it to NVS with the display timers
// stopped (see main_bonds_flush()), as for every other flash write in this firmware.

static const char* TAG = "BONDS";

#define BONDS_NAMESPACE "bonds"
#define BONDS_KEY       "names"

typedef struct {
    uint8_t type;
    uint8_t addr[6];
    char name[BOND_NAME_MAX + 1]; // zero terminated UTF-8
} bond_name_t;

typedef struct {
    uint8_t count;
    bond_name_t items[BONDS_MAX];
} bond_names_t;

static Preferences _preferences;
static bond_names_t _db;
static portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool _dirty = false;
static bool _ready = false;

static bool id_equal(const bond_id_t* a, const bond_id_t* b)
{
    return a->type == b->type && !memcmp(a->addr, b->addr, sizeof(a->addr));
}

static int find(const bond_names_t* db, const bond_id_t* id)
{
    for (int i = 0; i < db->count; i++)
        if (id_equal((const bond_id_t*)&db->items[i], id))
            return i;
    return -1;
}

static bool save(const bond_names_t* db)
{
    if (!_ready)
        return false;

    if (db->count == 0) {
        if (_preferences.isKey(BONDS_KEY) && !_preferences.remove(BONDS_KEY))
            return false;
        return true;
    }

    return _preferences.putBytes(BONDS_KEY, db, sizeof(*db)) == sizeof(*db);
}

bool bonds_init()
{
    memset(&_db, 0, sizeof(_db));
    _dirty = false;
    _ready = false;

    if (!_preferences.begin(BONDS_NAMESPACE, false)) {
        ESP_LOGE(TAG, "Error: preferences.begin() failed. Phone names are not kept.");
        return false;
    }
    _ready = true;

    size_t len = _preferences.getBytesLength(BONDS_KEY);
    if (len == sizeof(_db)) {
        if (_preferences.getBytes(BONDS_KEY, &_db, sizeof(_db)) != sizeof(_db)) {
            ESP_LOGW(TAG, "Could not read the phone names; starting without them.");
            memset(&_db, 0, sizeof(_db));
        }
    } else if (len != 0) {
        ESP_LOGW(TAG, "Phone names have an unexpected size (%u); starting without them.", (unsigned)len);
        memset(&_db, 0, sizeof(_db));
    }

    if (_db.count > BONDS_MAX) {
        ESP_LOGW(TAG, "Phone names hold %u entries, more than %u; starting without them.",
            _db.count, (unsigned)BONDS_MAX);
        memset(&_db, 0, sizeof(_db));
    }

    for (int i = 0; i < _db.count; i++) {
        _db.items[i].name[BOND_NAME_MAX] = 0;
        if (strnlen(_db.items[i].name, BOND_NAME_MAX + 1) > BOND_NAME_MAX) {
            ESP_LOGW(TAG, "Phone name %d is not terminated; starting without the names.", i);
            memset(&_db, 0, sizeof(_db));
            break;
        }
    }

    ESP_LOGI(TAG, "Loaded %u phone name(s)", _db.count);
    return true;
}

bool bonds_is_dirty()
{
    return _dirty;
}

bool bonds_flush()
{
    if (!_ready) {
        // Nothing can be written: drop the pending flag so the main loop does not log every tick
        _dirty = false;
        return false;
    }

    bond_names_t copy;
    portENTER_CRITICAL(&_mux);
    if (!_dirty) {
        portEXIT_CRITICAL(&_mux);
        return true;
    }
    memcpy(&copy, &_db, sizeof(copy));
    // Cleared before the write: a change arriving meanwhile gets flushed on the next loop iteration
    _dirty = false;
    portEXIT_CRITICAL(&_mux);

    bool ok = save(&copy);
    if (!ok) {
        portENTER_CRITICAL(&_mux);
        _dirty = true;
        portEXIT_CRITICAL(&_mux);
        ESP_LOGE(TAG, "Error: writing the phone names into NVS failed.");
    }
    return ok;
}

bool bonds_set_name(const bond_id_t* id, const char* name)
{
    if (!id || !name || id->type > 3)
        return false;

    size_t len = strnlen(name, BOND_NAME_MAX + 1);
    if (len > BOND_NAME_MAX)
        return false;

    portENTER_CRITICAL(&_mux);
    int idx = find(&_db, id);

    if (len == 0) {
        // An empty name forgets the stored one
        if (idx >= 0) {
            memmove(&_db.items[idx], &_db.items[idx + 1], (_db.count - idx - 1) * sizeof(_db.items[0]));
            _db.count--;
            _dirty = true;
        }
        portEXIT_CRITICAL(&_mux);
        return true;
    }

    if (idx < 0) {
        if (_db.count >= BONDS_MAX) {
            portEXIT_CRITICAL(&_mux);
            return false;
        }
        idx = _db.count++;
    }

    bond_name_t* item = &_db.items[idx];
    item->type = id->type;
    memcpy(item->addr, id->addr, sizeof(item->addr));
    memcpy(item->name, name, len);
    item->name[len] = 0;
    _dirty = true;
    portEXIT_CRITICAL(&_mux);
    return true;
}

size_t bonds_get_name(const bond_id_t* id, char* out, size_t size)
{
    if (!out || !size)
        return 0;
    out[0] = 0;
    if (!id)
        return 0;

    portENTER_CRITICAL(&_mux);
    int idx = find(&_db, id);
    if (idx >= 0)
        snprintf(out, size, "%s", _db.items[idx].name);
    portEXIT_CRITICAL(&_mux);

    return strlen(out);
}

void bonds_forget(const bond_id_t* id)
{
    if (!id)
        return;

    portENTER_CRITICAL(&_mux);
    int idx = find(&_db, id);
    if (idx >= 0) {
        memmove(&_db.items[idx], &_db.items[idx + 1], (_db.count - idx - 1) * sizeof(_db.items[0]));
        _db.count--;
        _dirty = true;
    }
    portEXIT_CRITICAL(&_mux);
}

void bonds_prune(const bond_id_t* ids, size_t count)
{
    portENTER_CRITICAL(&_mux);

    size_t kept = 0;
    bool changed = false;
    for (size_t i = 0; i < _db.count; i++) {
        bool found = false;
        for (size_t j = 0; j < count; j++)
            if (id_equal((const bond_id_t*)&_db.items[i], &ids[j])) {
                found = true;
                break;
            }
        if (found)
            _db.items[kept++] = _db.items[i];
        else
            changed = true;
    }

    if (changed) {
        _db.count = kept;
        _dirty = true;
    }
    portEXIT_CRITICAL(&_mux);
}
