// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The tile format (mnav-0003): a baked tile's polygon mesh and detail mesh as
// little-endian integers behind a versioned, fingerprinted header, and
// the loader that checks every byte of it as hostile.

#ifndef MAUL_NAV_SRC_TILE_H
#define MAUL_NAV_SRC_TILE_H

#include "allocator.h"
#include "detail.h"
#include "polymesh.h"

#include "maul-nav/bake.h"
#include "maul-nav/base.h"
#include "maul-nav/navmesh.h"

#include <stddef.h>
#include <stdint.h>

// The header's size in bytes.
#define MNAV_TILE_HEADER_BYTES 104

// What a tile records beside its meshes: the generator's version, the
// bake's fingerprint of its input, the tile's place in the grid, and the
// settings its integers are in.
typedef struct mnavTileInfo
{
    mnavVersion generator;
    uint64_t fingerprint;
    int32_t x;
    int32_t z;
    int32_t tileCells;
    float cellSize;
    float cellHeight;
    int32_t agentHeight;
    int32_t agentRadius;
    int32_t agentStep;
    mnavPos3 origin;
} mnavTileInfo;

// The payload's hash is mnavHash64 over its bytes with eight-byte words
// read little-endian, which is what mnavHash64 computes on every platform
// the library supports; a big-endian port must read them so.

// Writes a tile from a linked polygon mesh and its detail mesh into bytes
// allocated from memory, which mnavReleaseTileBytes frees.
mnavResult mnavEncodeTile(mnavMemory* memory, const mnavTileInfo* info, const mnavPolyMesh* mesh,
                          const mnavDetailMesh* detail, uint8_t** bytes, size_t* size);

void mnavReleaseTileBytes(mnavMemory* memory, uint8_t* bytes, size_t size);

// Reads a tile from size bytes. On success the meshes hold what was
// written (regions are not stored and read as 0); on failure nothing is
// held. A malformed tile is mnav_errorInvalid, another format version
// mnav_errorVersion.
mnavTileResult mnavDecodeTile(mnavMemory* memory, const uint8_t* bytes, size_t size,
                              mnavTileInfo* info, mnavPolyMesh* mesh, mnavDetailMesh* detail);

#endif // MAUL_NAV_SRC_TILE_H
