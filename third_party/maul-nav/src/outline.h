// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// 2D outlines filled into the fragments of one tile (mnav-0002): every cell
// whose center a walkable outline holds and no obstruction does becomes
// the fragment of a flat triangle at height 0.

#ifndef MAUL_NAV_SRC_OUTLINE_H
#define MAUL_NAV_SRC_OUTLINE_H

#include "allocator.h"
#include "raster.h"
#include "tile_index.h"

#include "maul-nav/bake.h"

#include <stdint.h>

// Whether a ring of points' bounds meet the tile's cells, border
// included; (x, y) is the place (x, 0, y).
bool mnavRingTouchesTile(const mnavTileFrame* frame, const mnavVec2* points, int32_t count);

// Whether an outline's bounds meet the tile's cells, border included.
bool mnavOutlineTouchesTile(const mnavTileFrame* frame, const mnavOutline* outline);

// Called for a cell (x, z) of the tile; anything but mnav_success stops
// the visit with that result.
typedef mnavResult (*mnavCellVisit)(void* context, int32_t x, int32_t z);

// Visits every cell of the tile whose center a ring holds by the even-odd
// rule, row by row and along each row in order; crossings has room for
// pointCount values.
mnavResult mnavVisitRing(const mnavTileFrame* frame, const mnavVec2* points, int32_t pointCount,
                         double* crossings, mnavCellVisit visit, void* context);

// The outlines a 2D bake reads for a tile, in input order: every one, or
// those a tile index lists for the tile, which hold all that reach it.
typedef struct mnavOutlineSet
{
    const mnavOutline* outlines;
    const mnavIndexEntry* listed;
    int32_t count;
    bool indexed;
} mnavOutlineSet;

// The set of outlines for tile (tileX, tileZ): those index lists, or with
// a NULL index every one.
mnavOutlineSet mnavOutlinesFor(const mnavOutline* outlines, int32_t outlineCount,
                               const mnavTileIndex* index, int32_t tileX, int32_t tileZ);

// The k-th outline of a set.
static inline const mnavOutline* mnavOutlineAt(const mnavOutlineSet* set, int32_t k)
{
    return &set->outlines[set->indexed ? set->listed[k].mesh : k];
}

// Adds the fragments of a set's valid outlines for the tile. Returns
// mnav_errorLimit past the tileTriangles limit on outlines touching the
// tile, the tileSpans limit on fragments or the memory limit, and
// mnav_errorCapacity when the allocator fails.
mnavResult mnavCollectOutlines(mnavMemory* memory, const mnavBakeDef* def,
                               const mnavTileFrame* frame, const mnavOutlineSet* set,
                               mnavFragmentList* list);

#endif // MAUL_NAV_SRC_OUTLINE_H
