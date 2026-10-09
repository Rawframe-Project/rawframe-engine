// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A tile's flight volume (mnav-0015): the bake's input rasterized at the
// voxel size, solid below the ground when asked, dilated by the flier's
// radius, as a compact octree per cube of the tile's side.

#ifndef MAUL_NAV_SRC_FLIGHT_H
#define MAUL_NAV_SRC_FLIGHT_H

#include "allocator.h"
#include "flight_def.h"

#include "maul-nav/bake.h"
#include "maul-nav/flight.h"

#include <stdint.h>

// A block's class: no solid voxel, every voxel solid, or both.
enum
{
    MNAV_FLIGHT_EMPTY = 0,
    MNAV_FLIGHT_SOLID = 1,
    MNAV_FLIGHT_MIXED = 2
};

// A node of a mixed block: its mixed children in the mask's low byte and
// its solid ones in the high byte, child o at bit o = x + 2y + 4z; its
// stored children, the mixed ones in octant order, start at firstChild:
// nodes one level down, or leaves below a node of 8 voxels.
typedef struct mnavFlightNode
{
    uint32_t firstChild;
    uint16_t mask;
} mnavFlightNode;

// A baked tile: cubes of side voxels stacked from the floor voxel up,
// each a root class and, when mixed, its root node's index, -1 else. A
// leaf's bit x + 4y + 16z is solid.
typedef struct mnavFlightTile
{
    int32_t tileX;
    int32_t tileZ;
    int32_t side;
    int32_t floorVoxel;
    int32_t cubeCount;
    uint8_t* roots;
    int32_t* rootNodes;
    mnavFlightNode* nodes;
    int32_t nodeCount;
    int32_t nodeCapacity;
    uint64_t* leaves;
    int32_t leafCount;
    int32_t leafCapacity;
} mnavFlightTile;

// Builds tile (tileX, tileZ) from input, which must be valid, with a
// valid def and its shape; spansOut receives the heightfield's spans.
// Returns the heightfield's errors, mnav_errorLimit past the def's node
// or leaf limit or for memory, and mnav_errorCapacity.
mnavResult mnavBuildFlightTile(mnavMemory* memory, const mnavFlightDef* def,
                               const mnavFlightShape* shape, const mnavBakeInput* input,
                               int32_t tileX, int32_t tileZ, mnavFlightTile* tileOut,
                               int32_t* spansOut);

// Whether voxel (x, y, z) of a tile is solid: x and z from 0 to its side,
// y from 0 to its cubes times its side, up from its floor voxel.
bool mnavFlightSolid(const mnavFlightTile* tile, int32_t x, int32_t y, int32_t z);

void mnavReleaseFlightTile(mnavMemory* memory, mnavFlightTile* tile);

#endif // MAUL_NAV_SRC_FLIGHT_H
