// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Merging hole contours into their outlines, so that every region
// is one simple polygon.

#ifndef MAUL_NAV_SRC_HOLES_H
#define MAUL_NAV_SRC_HOLES_H

#include "allocator.h"
#include "contour.h"

#include <stdint.h>

// Bridges every hole of a set built from regions 1 to regionCount into
// its region's outline, left to right, and removes the holes from the
// set. A hole that cannot be bridged is dropped and counted.
mnavResult mnavMergeHoles(mnavMemory* memory, mnavContourSet* set, uint32_t regionCount);

#endif // MAUL_NAV_SRC_HOLES_H
