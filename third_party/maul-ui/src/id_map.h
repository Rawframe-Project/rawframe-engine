// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A map from nonzero 64-bit ids to pointers: open addressing with linear
// probing, kept at most half full, in storage its owner gives.

#ifndef MAUL_UI_SRC_ID_MAP_H
#define MAUL_UI_SRC_ID_MAP_H

#include <stdbool.h>
#include <stdint.h>

typedef struct muiIdMap
{
    // A key of 0 is an empty place.
    uint64_t* keys;
    void** values;
    uint32_t mask;
    uint32_t count;
} muiIdMap;

// Starts an empty map over size places, a power of two.
void muiIdMapInit(muiIdMap* map, uint64_t* keys, void** values, uint32_t size);

// The value under an id, or NULL.
void* muiIdMapFind(const muiIdMap* map, uint64_t id);

// Puts a value under an id not in the map: false for the id 0 or a map
// half full.
bool muiIdMapInsert(muiIdMap* map, uint64_t id, void* value);

// Takes an id out: the value it had, or NULL.
void* muiIdMapRemove(muiIdMap* map, uint64_t id);

#endif // MAUL_UI_SRC_ID_MAP_H
