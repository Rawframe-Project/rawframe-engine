// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Partitioning a tile's open space into regions by layers:
// monotone sweeps per row, merged into non-overlapping layers of one
// area.

#ifndef MAUL_NAV_SRC_REGION_H
#define MAUL_NAV_SRC_REGION_H

#include "allocator.h"
#include "compact.h"

#include <stdint.h>

// The flag of a border region; the low bits name the side, 1 to 4.
#define MNAV_BORDER_REGION 0x80000000u

// One region id per open span: 0 for none (unwalkable or dropped), a
// border region on the tile's border, or a region from 1 to count.
typedef struct mnavRegionMap
{
    uint32_t* ids;
    int32_t spanCount;
    uint32_t count;
    // Layers dropped for being small and off the border.
    uint32_t dropped;
} mnavRegionMap;

// Partitions the open space of a field whose border is border cells wide,
// dropping layers of fewer than minRegion spans that do not touch the
// border.
mnavResult mnavBuildRegions(mnavMemory* memory, const mnavCompactField* field, int32_t border,
                            int32_t minRegion, mnavRegionMap* map);

void mnavReleaseRegions(mnavMemory* memory, mnavRegionMap* map);

#endif // MAUL_NAV_SRC_REGION_H
