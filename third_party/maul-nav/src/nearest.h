// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The nearest point's surface, shared with the queries that need heights.

#ifndef MAUL_NAV_SRC_NEAREST_H
#define MAUL_NAV_SRC_NEAREST_H

#include "navmesh.h"

#include <stdint.h>

// The tile places a box covers: columns x0 to x1, rows z0 to z1, kept
// within the extent tiles can have.
typedef struct mnavCover
{
    int64_t x0;
    int64_t x1;
    int64_t z0;
    int64_t z1;
} mnavCover;

mnavCover mnavCoverOf(const mnavNavmesh* navmesh, mnavPos3 point, mnavPos3 half);

// The world height of a polygon's detail surface at a ground point, at the
// detail triangle nearest it.
double mnavSurfaceHeight(const mnavNavmesh* navmesh, int32_t slot, int32_t polygon, double x,
                         double z);

// A detail vertex of the tile in slot, in world meters.
mnavPos3 mnavDetailWorld(const mnavNavmesh* navmesh, int32_t slot, const mnavDetailVertex* v);

#endif // MAUL_NAV_SRC_NEAREST_H
