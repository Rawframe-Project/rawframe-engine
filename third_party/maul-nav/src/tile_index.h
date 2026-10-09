// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The triangles of a bake's meshes that may reach each tile of a grid
// (mnav-0014).

#ifndef MAUL_NAV_SRC_TILE_INDEX_H
#define MAUL_NAV_SRC_TILE_INDEX_H

#include "allocator.h"
#include "bake_def.h"

#include "maul-nav/bake.h"

#include <stdint.h>

// An item of the input: a triangle, by its mesh and its place in the
// mesh, or an outline, by its place and 0.
typedef struct mnavIndexEntry
{
    int32_t mesh;
    int32_t triangle;
} mnavIndexEntry;

struct mnavTileIndex
{
    mnavMemory memory;
    // The grid it was built for.
    int32_t tileCells;
    float cellSize;
    int32_t border;
    // What it was made from: outlines or meshes, how many, and each mesh's
    // triangles or each outline's points.
    bool outlines;
    int32_t sourceCount;
    int32_t* counts;
    // The tiles listed, columns by rows from (minX, minZ), row by row; tile
    // k's triangles are entries first[k] to first[k + 1], in input order.
    int32_t minX;
    int32_t minZ;
    int32_t columns;
    int32_t rows;
    int32_t* first;
    mnavIndexEntry* entries;
    int32_t entryCount;
};

// Whether an index was built for this grid and these meshes' counts.
bool mnavTileIndexFits(const mnavTileIndex* index, const mnavBakeDef* def,
                       const mnavBakeCells* cells, const mnavTriangleMesh* meshes,
                       int32_t meshCount);

// Whether an index was built for this grid and these outlines' counts.
bool mnavTileIndexFits2D(const mnavTileIndex* index, const mnavBakeDef* def,
                         const mnavBakeCells* cells, const mnavOutline* outlines,
                         int32_t outlineCount);

// The items listed for tile (tileX, tileZ), triangles (mesh, triangle) or
// outlines (outline, 0): none past the tiles the index covers.
void mnavTileIndexList(const mnavTileIndex* index, int32_t tileX, int32_t tileZ,
                       const mnavIndexEntry** entriesOut, int32_t* countOut);

#endif // MAUL_NAV_SRC_TILE_INDEX_H
