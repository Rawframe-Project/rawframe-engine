// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Looking up gamepad mappings.

#include "pad_db.h"

// The device's key: bus, vendor, product, and version when it counts.
static uint64_t KeyOf(uint16_t bus, uint16_t vendor, uint16_t product, uint16_t version)
{
    return (uint64_t)bus << 48 | (uint64_t)vendor << 32 | (uint64_t)product << 16 | version;
}

static uint64_t EntryKey(const mwinPadEntry* entry)
{
    return KeyOf(entry->bus, entry->vendor, entry->product, entry->version);
}

// The first entry at or after a key.
static size_t LowerBound(const mwinPadDatabase* database, uint64_t key)
{
    size_t low = 0;
    size_t high = database->entryCount;
    while (low < high)
    {
        size_t middle = low + (high - low) / 2;
        if (EntryKey(&database->entries[middle]) < key)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low;
}

const mwinPadMapping* mwinFindPadMapping(const mwinPadDatabase* database, uint16_t bus,
                                         uint16_t vendor, uint16_t product, uint16_t version)
{
    uint64_t key = KeyOf(bus, vendor, product, version);
    size_t found = LowerBound(database, key);
    if (found < database->entryCount && EntryKey(&database->entries[found]) == key)
    {
        return &database->mappings[database->entries[found].mapping];
    }
    // Another version of the same device.
    size_t any = LowerBound(database, key & ~(uint64_t)0xFFFF);
    const mwinPadEntry* entry = any < database->entryCount ? &database->entries[any] : nullptr;
    return entry != nullptr && (EntryKey(entry) >> 16) == (key >> 16)
               ? &database->mappings[entry->mapping]
               : nullptr;
}
