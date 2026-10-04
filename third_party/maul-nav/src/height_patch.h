// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The floor heights under one polygon, read from the open-space field for
// its detail mesh.

#ifndef MAUL_NAV_SRC_HEIGHT_PATCH_H
#define MAUL_NAV_SRC_HEIGHT_PATCH_H

#include "allocator.h"
#include "compact.h"
#include "polymesh.h"
#include "region.h"

#include "maul-nav/bake.h"

#include <stdint.h>

// A cell without a height.
#define MNAV_UNSET_HEIGHT 0xFFFFu

// Heights over the cells minX to minX + width - 1 and minZ to minZ + depth
// - 1, in the mesh's cells (the tile without its border) and offset cell
// heights. Reused from polygon to polygon.
typedef struct mnavHeightPatch
{
    uint16_t* heights;
    int32_t minX;
    int32_t minZ;
    int32_t width;
    int32_t depth;
    int32_t capacity;
    // The flood's queue: a span and its column per entry.
    uint32_t* queue;
    int32_t queueCapacity;
} mnavHeightPatch;

// Fills the patch for a polygon of the mesh over its cells and one more
// on each side, within the field. A polygon with a region takes each
// cell's span of that region and floods from the region's edge into the
// other cells; a polygon of mixed regions, or one whose region has no span
// there, floods from the spans at its vertices, the one nearest each
// vertex's height. border is the field's border in cells.
mnavResult mnavFillHeightPatch(mnavMemory* memory, const mnavCompactField* field,
                               const mnavRegionMap* regions, int32_t border,
                               const mnavPolyMesh* mesh, int32_t polygon, mnavHeightPatch* patch);

// The height in cell (x, z) of the mesh's cells, clamped into the patch;
// when unset, the set height nearest reference in the first ring of cells
// round it, out to radius, that has one, the first in ring order on ties.
// Returns false when no cell within radius is set.
bool mnavPatchHeight(const mnavHeightPatch* patch, int32_t x, int32_t z, int32_t reference,
                     int32_t radius, uint16_t* height);

void mnavReleaseHeightPatch(mnavMemory* memory, mnavHeightPatch* patch);

#endif // MAUL_NAV_SRC_HEIGHT_PATCH_H
