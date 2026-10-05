// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Open addressing with linear probing; stale entries are kept until a
// rebuild, as lookups of the same key reuse them in place.

#include "glyph_table.h"

#include "allocator.h"

#include <stdalign.h>
#include <string.h>

enum
{
    MIN_CAPACITY = 64
};

static uint32_t Hash(const muiGlyphKey* key)
{
    uint64_t h = key->font * 0x9E3779B97F4A7C15u;
    h ^= ((uint64_t)key->glyph << 32 | key->sizeBin) * 0xC2B2AE3D27D4EB4Fu;
    h ^= h >> 29;
    return (uint32_t)(h * 0x165667B19E3779F9u >> 32);
}

static bool SameKey(const muiGlyphKey* a, const muiGlyphKey* b)
{
    return a->font == b->font && a->glyph == b->glyph && a->sizeBin == b->sizeBin;
}

bool muiIsEntryStale(const muiAtlasEntry* entry, const uint32_t* generations)
{
    return entry->plot != MUI_NO_PLOT && generations[entry->plot - 1] != entry->generation;
}

muiAtlasEntry* muiFindEntry(const muiGlyphTable* table, const muiGlyphKey* key)
{
    uint32_t mask = table->capacity - 1;
    for (uint32_t i = Hash(key) & mask;; i = (i + 1) & mask)
    {
        muiAtlasEntry* entry = &table->entries[i];
        if (entry->plot == 0 || SameKey(&entry->key, key))
        {
            return entry;
        }
    }
}

bool muiReserveEntry(const muiAllocator* allocator, muiGlyphTable* table,
                     const uint32_t* generations)
{
    if ((table->count + 1) * 2 <= table->capacity)
    {
        return true;
    }
    uint32_t live = 0;
    for (uint32_t i = 0; i < table->capacity; i++)
    {
        const muiAtlasEntry* entry = &table->entries[i];
        live += entry->plot != 0 && !muiIsEntryStale(entry, generations) ? 1u : 0u;
    }
    // Live entries fill at most a quarter of the new table, so it takes
    // as many again before the next rebuild.
    uint32_t capacity = MIN_CAPACITY;
    while (capacity < (live + 1) * 4)
    {
        if (capacity > UINT32_MAX / 2)
        {
            return false;
        }
        capacity *= 2;
    }
    muiAtlasEntry* entries =
        muiAllocate(allocator, (size_t)capacity * sizeof(muiAtlasEntry), alignof(muiAtlasEntry));
    if (entries == nullptr)
    {
        return false;
    }
    memset(entries, 0, (size_t)capacity * sizeof(muiAtlasEntry));
    muiGlyphTable rebuilt = {entries, capacity, 0};
    for (uint32_t i = 0; i < table->capacity; i++)
    {
        const muiAtlasEntry* entry = &table->entries[i];
        if (entry->plot != 0 && !muiIsEntryStale(entry, generations))
        {
            *muiFindEntry(&rebuilt, &entry->key) = *entry;
            rebuilt.count++;
        }
    }
    muiFreeGlyphTable(allocator, table);
    *table = rebuilt;
    return true;
}

void muiFreeGlyphTable(const muiAllocator* allocator, muiGlyphTable* table)
{
    if (table->entries != nullptr)
    {
        muiRelease(allocator, table->entries, (size_t)table->capacity * sizeof(muiAtlasEntry),
                   alignof(muiAtlasEntry));
    }
    *table = (muiGlyphTable){nullptr, 0, 0};
}
