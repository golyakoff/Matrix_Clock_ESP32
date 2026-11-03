#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The phones the clock remembers. Keep in sync with CONFIG_BT_NIMBLE_MAX_BONDS (ble.cpp checks it).
#define BONDS_MAX       (5U)

// Bytes of UTF-8 in a phone name, as in the BLE protocol.
#define BOND_NAME_MAX   (48U)

// The identity address of a bond: the address NimBLE keeps in its bond store.
typedef struct {
    uint8_t type;       // BLE address type (0 public, 1 random, 2 public identity, 3 random identity)
    uint8_t addr[6];    // identity address, least significant byte first
} bond_id_t;

/**
 * @brief Loads the names from NVS into RAM. Called once before BLE starts.
 *
 * @return false if NVS could not be opened; the list then works without names.
 */
bool bonds_init();

/**
 * @brief Whether there are name changes not written to NVS yet.
 */
bool bonds_is_dirty();

/**
 * @brief Writes the pending changes to NVS. Called from loop() with the display timers stopped.
 */
bool bonds_flush();

/**
 * @brief Stores (or replaces) the name of a phone; an empty name forgets it.
 *
 * @return false if the table is full or the id/name is invalid.
 */
bool bonds_set_name(const bond_id_t *id, const char *name);

/** Copies the name of a phone into out; returns its length, 0 when there is no name. */
size_t bonds_get_name(const bond_id_t *id, char *out, size_t size);

/** Drops the name of one phone. */
void bonds_forget(const bond_id_t *id);

/** Drops the names of phones that are not in the list (a bond was evicted or forgotten). */
void bonds_prune(const bond_id_t *ids, size_t count);
