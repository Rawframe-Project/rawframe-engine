// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A map from nonzero 64-bit ids to pointers.

#include "id_map.h"

#include <string.h>

void muiIdMapInit(muiIdMap* map, uint64_t* keys, void** values, uint32_t size)
{
    memset(keys, 0, size * sizeof(uint64_t));
    *map = (muiIdMap){keys, values, size - 1, 0};
}

static uint32_t Home(const muiIdMap* map, uint64_t id)
{
    return (uint32_t)((id * 0x9E3779B97F4A7C15ULL) >> 32) & map->mask;
}

// Where an id sits, or where it would go.
static uint32_t PlaceOf(const muiIdMap* map, uint64_t id)
{
    uint32_t at = Home(map, id);
    while (map->keys[at] != 0 && map->keys[at] != id)
    {
        at = (at + 1) & map->mask;
    }
    return at;
}

void* muiIdMapFind(const muiIdMap* map, uint64_t id)
{
    uint32_t at = PlaceOf(map, id);
    return id != 0 && map->keys[at] == id ? map->values[at] : nullptr;
}

bool muiIdMapInsert(muiIdMap* map, uint64_t id, void* value)
{
    if (id == 0 || (map->count + 1) * 2 > map->mask + 1)
    {
        return false;
    }
    uint32_t at = PlaceOf(map, id);
    map->count++;
    map->keys[at] = id;
    map->values[at] = value;
    return true;
}

void* muiIdMapRemove(muiIdMap* map, uint64_t id)
{
    uint32_t gap = PlaceOf(map, id);
    if (id == 0 || map->keys[gap] != id)
    {
        return nullptr;
    }
    void* value = map->values[gap];
    // Linear probing's deletion: later entries whose home lies outside
    // the gap move back into it.
    for (uint32_t at = (gap + 1) & map->mask; map->keys[at] != 0; at = (at + 1) & map->mask)
    {
        uint32_t home = Home(map, map->keys[at]);
        bool inside = gap <= at ? home > gap && home <= at : home > gap || home <= at;
        if (!inside)
        {
            map->keys[gap] = map->keys[at];
            map->values[gap] = map->values[at];
            gap = at;
        }
    }
    map->keys[gap] = 0;
    map->count--;
    return value;
}
