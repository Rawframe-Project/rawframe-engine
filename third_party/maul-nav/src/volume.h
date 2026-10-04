// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Bake volumes (mnav-0003): include and exclude volumes carve the
// open-space field before erosion, area volumes mark it after.

#ifndef MAUL_NAV_SRC_VOLUME_H
#define MAUL_NAV_SRC_VOLUME_H

#include "allocator.h"
#include "compact.h"

#include "maul-nav/bake.h"

#include <stdint.h>

// Checks a volume as hostile input to a bake with a def.
mnavInputResult mnavCheckVolume(const mnavBakeDef* def, const mnavBakeVolume* volume);

// Drops the walkable spans that include volumes, when there are any,
// leave out, then those exclude volumes hold. Returns mnav_errorLimit for
// the memory limit and mnav_errorCapacity when the allocator fails.
mnavResult mnavCarveVolumes(mnavMemory* memory, mnavCompactField* field,
                            const mnavBakeVolume* volumes, int32_t count);

// Gives the walkable spans area volumes hold their areas, in order.
// Returns as mnavCarveVolumes.
mnavResult mnavMarkVolumes(mnavMemory* memory, mnavCompactField* field,
                           const mnavBakeVolume* volumes, int32_t count);

#endif // MAUL_NAV_SRC_VOLUME_H
