// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Rasterizing triangles into the fragments of one tile: every cell
// a triangle covers gets the lowest and highest cell height of the part
// of the triangle inside it.

#ifndef MAUL_NAV_SRC_RASTER_H
#define MAUL_NAV_SRC_RASTER_H

#include "allocator.h"

#include "maul-nav/bake.h"

#include <stdint.h>

// Cell heights are stored with this offset, so the input's vertical range
// of MNAV_MAX_HEIGHT_CELLS up or down fits in 16 bits.
#define MNAV_HEIGHT_OFFSET 32768

// Where a tile's cells lie, in the bake's frame. The tile's cells, with its
// border on every side, are width by width; cell (0, 0) starts at minX,
// minZ.
typedef struct mnavTileFrame
{
    int32_t width;
    float minX;
    float minZ;
    float cellSize;
    float cellHeight;
} mnavTileFrame;

// One clipped piece of a triangle in one cell: a column and a solid range
// of cell heights, bottom inclusive, top exclusive.
typedef struct mnavFragment
{
    uint16_t x;
    uint16_t z;
    uint16_t bottom;
    uint16_t top;
    mnavAreaType area;
} mnavFragment;

// A growing list of fragments, refused past its limit.
typedef struct mnavFragmentList
{
    mnavFragment* items;
    int32_t count;
    int32_t capacity;
    int32_t limit;
} mnavFragmentList;

// The frame of tile (tileX, tileZ). Returns false when the tile lies past
// the extent input may have.
bool mnavMakeTileFrame(const mnavBakeDef* def, const mnavBakeCells* cells, int32_t tileX,
                       int32_t tileZ, mnavTileFrame* frame);

// The area a triangle rasterizes with: its own, or mnav_areaNone when it
// is steeper than the slope or faces down. Returns -1 for a triangle with
// no area, which rasterizes nothing.
int32_t mnavTriangleArea(const mnavVec3 corners[3], mnavAreaType area, float cosMaxSlope);

// Whether a triangle touches the tile's cells, border included.
bool mnavTriangleTouchesTile(const mnavTileFrame* frame, const mnavVec3 corners[3]);

// Adds the fragments of a triangle that touches the tile.
mnavResult mnavRasterizeTriangle(mnavMemory* memory, const mnavTileFrame* frame,
                                 const mnavVec3 corners[3], mnavAreaType area,
                                 mnavFragmentList* list);

// Adds one fragment, growing the list; mnav_errorLimit past its limit.
mnavResult mnavAddFragment(mnavMemory* memory, mnavFragmentList* list, mnavFragment fragment);

void mnavReleaseFragments(mnavMemory* memory, mnavFragmentList* list);

#endif // MAUL_NAV_SRC_RASTER_H
