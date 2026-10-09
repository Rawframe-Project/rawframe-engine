// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The flight volume's insides (mnav-0015), for the queries that read it.

#ifndef MAUL_NAV_SRC_FLIGHT_VOLUME_H
#define MAUL_NAV_SRC_FLIGHT_VOLUME_H

#include "allocator.h"
#include "flight.h"
#include "flight_def.h"

#include "maul-nav/flight.h"

#include <stdint.h>

// A committed tile, or a staged change at a place: a tile to install, or
// a removal.
typedef struct mnavFlightEntry
{
    uint64_t fingerprint;
    mnavFlightTile tile;
    int32_t x;
    int32_t z;
    bool removal;
} mnavFlightEntry;

struct mnavFlightVolume
{
    mnavFlightDef def;
    mnavFlightShape shape;
    mnavMemory memory;
    // The committed tiles, sorted by x, then z.
    mnavFlightEntry* tiles;
    int32_t tileCount;
    // The staged changes, one per place, in staging order.
    mnavFlightEntry* staged;
    int32_t stagedCount;
    int32_t stagedCapacity;
};

// The tile committed at a place, or NULL.
const mnavFlightTile* mnavFlightTileAt(const mnavFlightVolume* volume, int32_t tileX,
                                       int32_t tileZ);

#endif // MAUL_NAV_SRC_FLIGHT_VOLUME_H
