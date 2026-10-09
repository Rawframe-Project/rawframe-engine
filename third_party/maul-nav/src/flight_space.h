// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The flight volume's space as its queries see it (mnav-0015): voxels in
// the volume's frame, the open blocks of the octrees that hold them, the
// blocks across a block's faces, and a conservative walk along a segment.

#ifndef MAUL_NAV_SRC_FLIGHT_SPACE_H
#define MAUL_NAV_SRC_FLIGHT_SPACE_H

#include "flight.h"

#include "maul-nav/flight.h"

#include <stdint.h>

// What holds a voxel: open space, something solid (or the space past the
// volume's floor and ceiling), or no loaded tile.
enum
{
    MNAV_SPACE_OPEN = 0,
    MNAV_SPACE_SOLID = 1,
    MNAV_SPACE_UNLOADED = 2
};

// A block of the volume: its lowest voxel, x and z counted from the
// origin's voxel and y from the floor voxel, and its side in voxels.
typedef struct mnavFlightBlock
{
    int32_t x;
    int32_t y;
    int32_t z;
    int32_t size;
} mnavFlightBlock;

// Reads a volume, keeping the last tile it looked up.
typedef struct mnavFlightCursor
{
    const mnavFlightVolume* volume;
    const mnavFlightTile* tile;
    int32_t tileX;
    int32_t tileZ;
    bool cached;
    int32_t side;
    int32_t layers;
} mnavFlightCursor;

mnavFlightCursor mnavMakeFlightCursor(const mnavFlightVolume* volume);

// What holds voxel (x, y, z); blockOut, which may be NULL, receives the
// open block holding an open one.
int32_t mnavFlightHolder(mnavFlightCursor* cursor, int32_t x, int32_t y, int32_t z,
                         mnavFlightBlock* blockOut);

// Called for each open block across a face.
typedef void (*mnavFlightVisit)(void* context, mnavFlightBlock block);

// Calls visit for every open block that shares part of face f of block
// b: f / 2 is the axis (x, y, z) and f % 2 the side (0 below, 1 above).
// Returns what lies across the face when no tile is there to search:
// MNAV_SPACE_SOLID past the volume's floor or ceiling, MNAV_SPACE_UNLOADED
// with no tile loaded; MNAV_SPACE_OPEN when a tile was searched.
int32_t mnavFlightFace(mnavFlightCursor* cursor, const mnavFlightBlock* b, int32_t f,
                       mnavFlightVisit visit, void* context);

// Walks the voxels a segment from a to b crosses, in voxels of the
// volume's frame, and where it passes an edge or a corner every voxel it
// touches. Returns MNAV_SPACE_OPEN when all are open, or what holds the
// first one that is not, with tOut, which may be NULL, receiving the
// fraction of the segment where it is entered.
int32_t mnavFlightWalk(mnavFlightCursor* cursor, const double a[3], const double b[3],
                       double* tOut);

#endif // MAUL_NAV_SRC_FLIGHT_SPACE_H
