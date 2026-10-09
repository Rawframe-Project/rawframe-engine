// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The flight def (mnav-0015): its default, its check, and what it comes
// to in voxels.

#ifndef MAUL_NAV_SRC_FLIGHT_DEF_H
#define MAUL_NAV_SRC_FLIGHT_DEF_H

#include "maul-nav/bake.h"
#include "maul-nav/flight.h"

#include <stdint.h>

// What a valid flight def comes to in voxels: the flier's radius rounded
// up, the volume's lowest voxel above the origin's (negative below it),
// and its cubes of the tile's side.
typedef struct mnavFlightShape
{
    int32_t radius;
    int32_t floorVoxel;
    int32_t cubeCount;
} mnavFlightShape;

// Checks a def as mnavValidateFlightDef does; shapeOut, which may be
// NULL, receives the shape of a valid one.
mnavFlightDefResult mnavCheckFlightDef(const mnavFlightDef* def, mnavFlightShape* shapeOut);

// The bake def a flight tile's heightfield is built with, from a valid
// flight def: cells and cell heights of one voxel, tiles of the tile's
// side, the flier's radius as the agent's, and the def's origin,
// allocator and limits.
mnavBakeDef mnavFlightBakeDef(const mnavFlightDef* def);

#endif // MAUL_NAV_SRC_FLIGHT_DEF_H
