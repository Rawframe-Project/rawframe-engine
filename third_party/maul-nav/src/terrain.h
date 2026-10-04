// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Terrains (mnav-0003): checking them, and the triangles of the cells that
// may reach a tile, made as the bake reads them.

#ifndef MAUL_NAV_SRC_TERRAIN_H
#define MAUL_NAV_SRC_TERRAIN_H

#include "raster.h"

#include "maul-nav/bake.h"

#include <stdbool.h>
#include <stdint.h>

// Checks a terrain as hostile input to a bake with a def.
mnavInputResult mnavCheckTerrain(const mnavBakeDef* def, const mnavTerrain* terrain);

// The triangles a checked terrain holds.
int64_t mnavTerrainTriangles(const mnavTerrain* terrain);

// The cells that may reach a tile's cells, border included: columns c0 to
// c1 and rows r0 to r1. False when none may.
bool mnavTerrainCells(const mnavTerrain* terrain, const mnavTileFrame* frame, int32_t* c0,
                      int32_t* c1, int32_t* r0, int32_t* r1);

// Triangle k (0 or 1) of cell (column, row): its corners and given area.
// False for a hole.
bool mnavTerrainTriangle(const mnavTerrain* terrain, int32_t column, int32_t row, int32_t k,
                         mnavVec3 corners[3], mnavAreaType* area);

#endif // MAUL_NAV_SRC_TERRAIN_H
