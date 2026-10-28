// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The tile cache: per tile, the open-space field before
// volumes apply, the volumes that shape it, and the hashes a rebuild
// continues from, in the cache's own memory.

#ifndef MAUL_NAV_SRC_TILE_CACHE_H
#define MAUL_NAV_SRC_TILE_CACHE_H

#include "allocator.h"
#include "compact.h"
#include "field_pack.h"
#include "host_code.h"
#include "raster.h"

#include "maul-nav/bake.h"

#include <stdbool.h>
#include <stdint.h>

enum
{
    MNAV_SETTINGS_WORDS = 21
};

// A baker's settings for one tile as words: the bake generator, the
// origin, the cells, the agent, the region, edge and detail settings,
// and the tile's place. The fingerprint starts from their hash.
typedef struct mnavBakeSettings
{
    uint32_t words[MNAV_SETTINGS_WORDS];
} mnavBakeSettings;

// What the cache holds for one tile.
typedef struct mnavCachedTile
{
    int32_t tileX;
    int32_t tileZ;
    // The baker's settings for the tile, compared in full so that a
    // rebuild by a baker of another def is refused.
    mnavBakeSettings settings;
    // The fingerprint after the triangles, before the volumes.
    uint64_t geometry;
    int32_t triangles;
    // The field before volumes apply, packed.
    mnavPackedField field;
    // The input's volumes that reach the tile, in input order, then, when
    // the input has an include volume and none of them is one, the
    // input's first include: so that including still applies and still
    // counts in the fingerprint.
    mnavBakeVolume* volumes;
    int32_t volumeCount;
    mnavVec2* points;
    int32_t pointCount;
} mnavCachedTile;

// Keeps a tile's field, packed, and its volumes, replacing what the
// cache held for it; tile gives the tile's place and hashes, scratch the
// memory packing works in. mnav_errorCapacity when the cache holds its most
// tiles or bytes, the tile's old entry then dropped.
mnavResult mnavCacheTile(mnavTileCache* cache, mnavMemory* scratch, const mnavCachedTile* tile,
                         const mnavCompactField* field, const mnavBakeVolume* volumes,
                         int32_t volumeCount);

// What the cache holds for a tile, or NULL.
const mnavCachedTile* mnavFindCachedTile(const mnavTileCache* cache, int32_t tileX, int32_t tileZ);

// Drops what the cache holds for a tile, as mnavDropCachedTile does, for
// a call that has marked the cache itself.
void mnavUncacheTile(mnavTileCache* cache, int32_t tileX, int32_t tileZ);

// The cache's host code mark, for the calls that use it; NULL for NULL.
mnavHostCode* mnavTileCacheHost(mnavTileCache* cache);

#endif // MAUL_NAV_SRC_TILE_CACHE_H
