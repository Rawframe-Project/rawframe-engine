// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The commit's part in runtime tiers (mnav-0004).

#ifndef MAUL_NAV_SRC_MODIFIERS_H
#define MAUL_NAV_SRC_MODIFIERS_H

#include "maul-nav/navmesh.h"

// Applies the staged area changes to the polygons still current, after
// the tiles of the same commit.
void mnavApplyAreas(mnavNavmesh* navmesh);

#endif // MAUL_NAV_SRC_MODIFIERS_H
