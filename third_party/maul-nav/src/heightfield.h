// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A tile's solid heightfield: the merged spans of every column, built
// from the tile's fragments so that it depends only on the set of input
// triangles.

#ifndef MAUL_NAV_SRC_HEIGHTFIELD_H
#define MAUL_NAV_SRC_HEIGHTFIELD_H

#include "allocator.h"
#include "raster.h"

#include "maul-nav/bake.h"

#include <stdint.h>

// A solid span: cell heights from bottom, inclusive, to top, exclusive,
// offset by MNAV_HEIGHT_OFFSET, and the area of its top surface.
typedef struct mnavSpan
{
    uint16_t bottom;
    uint16_t top;
    mnavAreaType area;
} mnavSpan;

// The spans of column (x, z) are spans[columns[i]] up to, not including,
// spans[columns[i + 1]] with i = x + z * width, in ascending order.
typedef struct mnavHeightfield
{
    mnavTileFrame frame;
    uint32_t* columns;
    mnavSpan* spans;
    int32_t spanCount;
    // The spans array's allocated length.
    int32_t spanCapacity;
} mnavHeightfield;

// Rasterizes every triangle of the meshes, which must be valid for def,
// into tile (tileX, tileZ). Returns mnav_errorRange for a tile past the
// input's extent, mnav_errorLimit for a tile with more triangles than the
// tileTriangles limit or more fragments than tileSpans, or for the memory
// limit, and mnav_errorCapacity when the allocator fails.
mnavResult mnavBuildHeightfield(mnavMemory* memory, const mnavBakeDef* def,
                                const mnavBakeCells* cells, const mnavTriangleMesh* meshes,
                                int32_t meshCount, int32_t tileX, int32_t tileZ,
                                mnavHeightfield* heightfield);

// The same from meshes and terrains: the terrains' triangles of the cells
// that may reach the tile, after the meshes'.
mnavResult mnavBuildHeightfieldInput(mnavMemory* memory, const mnavBakeDef* def,
                                     const mnavBakeCells* cells, const mnavBakeInput* input,
                                     int32_t tileX, int32_t tileZ, mnavHeightfield* heightfield);

// The same from 2D outlines, which must be valid for def: every cell one
// holds becomes a span at height 0 (mnav-0002). With a tile index of the
// outlines, only those it lists for the tile are read. Returns
// mnav_errorLimit past the tileTriangles limit on outlines touching the
// tile, otherwise as above.
mnavResult mnavBuildHeightfield2D(mnavMemory* memory, const mnavBakeDef* def,
                                  const mnavBakeCells* cells, const mnavOutline* outlines,
                                  int32_t outlineCount, const mnavTileIndex* index, int32_t tileX,
                                  int32_t tileZ, mnavHeightfield* heightfield);

void mnavReleaseHeightfield(mnavMemory* memory, mnavHeightfield* heightfield);

#endif // MAUL_NAV_SRC_HEIGHTFIELD_H
