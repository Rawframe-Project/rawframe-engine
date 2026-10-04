// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Erosion by the agent's radius: walkable space closer to a
// boundary than the radius becomes unwalkable.

#ifndef MAUL_NAV_SRC_ERODE_H
#define MAUL_NAV_SRC_ERODE_H

#include "allocator.h"
#include "compact.h"

#include <stdint.h>

// Makes every span closer than radius cells to a boundary unwalkable. A
// boundary is a span without a walkable linked neighbor in some
// direction.
mnavResult mnavErode(mnavMemory* memory, mnavCompactField* field, int32_t radius);

#endif // MAUL_NAV_SRC_ERODE_H
