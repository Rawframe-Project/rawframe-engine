// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The tile cache (mnav-0016): entries in a table of the def's tiles, each
// with a packed field and its own copies of volumes, all in the cache's
// memory and within its limits.

#include "tile_cache.h"

#include "allocator.h"
#include "compact.h"
#include "field_pack.h"
#include "outline.h"

#include "maul-nav/bake.h"
#include "maul-nav/base.h"

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

struct mnavTileCache
{
    mnavMemory memory;
    mnavTileCacheDef def;
    mnavCachedTile* tiles;
    int32_t count;
};

mnavResult mnavCreateTileCache(const mnavTileCacheDef* def, mnavTileCache** cacheOut)
{
    if (cacheOut == nullptr)
    {
        return mnav_errorInvalid;
    }
    *cacheOut = nullptr;
    if (def == nullptr || def->cookie != MNAV_TILE_CACHE_DEF_COOKIE)
    {
        return mnav_errorInvalid;
    }
    if (def->version != MNAV_ABI_VERSION)
    {
        return mnav_errorVersion;
    }
    if ((def->allocator.alloc == nullptr) != (def->allocator.free == nullptr))
    {
        return mnav_errorInvalid;
    }
    if (def->limits.tiles < 1 || def->limits.tiles > MNAV_MAX_CACHED_TILES ||
        def->limits.memoryBytes < 1)
    {
        return mnav_errorRange;
    }
    mnavMemory memory = mnavMakeMemory(def->allocator, def->limits.memoryBytes);
    mnavTileCache* cache = nullptr;
    mnavResult result =
        mnavAllocate(&memory, 1, sizeof(mnavTileCache), alignof(mnavTileCache), (void**)&cache);
    if (result != mnav_success)
    {
        return result;
    }
    *cache = (mnavTileCache){.memory = memory, .def = *def};
    result = mnavAllocate(&cache->memory, (size_t)def->limits.tiles, sizeof(mnavCachedTile),
                          alignof(mnavCachedTile), (void**)&cache->tiles);
    if (result != mnav_success)
    {
        mnavDestroyTileCache(cache);
        return result;
    }
    *cacheOut = cache;
    return mnav_success;
}

// Releases what an entry holds.
static void Release(mnavMemory* memory, mnavCachedTile* tile)
{
    mnavReleasePackedField(memory, &tile->field);
    mnavRelease(memory, tile->volumes, (size_t)tile->volumeCount, sizeof(mnavBakeVolume),
                alignof(mnavBakeVolume));
    mnavRelease(memory, tile->points, (size_t)tile->pointCount, sizeof(mnavVec2),
                alignof(mnavVec2));
    *tile = (mnavCachedTile){0};
}

void mnavDestroyTileCache(mnavTileCache* cache)
{
    if (cache == nullptr)
    {
        return;
    }
    for (int32_t i = 0; i < cache->count; ++i)
    {
        Release(&cache->memory, &cache->tiles[i]);
    }
    mnavMemory memory = cache->memory;
    mnavRelease(&memory, cache->tiles, (size_t)cache->def.limits.tiles, sizeof(mnavCachedTile),
                alignof(mnavCachedTile));
    mnavRelease(&memory, cache, 1, sizeof(mnavTileCache), alignof(mnavTileCache));
}

static int32_t IndexOf(const mnavTileCache* cache, int32_t tileX, int32_t tileZ)
{
    for (int32_t i = 0; i < cache->count; ++i)
    {
        if (cache->tiles[i].tileX == tileX && cache->tiles[i].tileZ == tileZ)
        {
            return i;
        }
    }
    return -1;
}

const mnavCachedTile* mnavFindCachedTile(const mnavTileCache* cache, int32_t tileX, int32_t tileZ)
{
    int32_t i = IndexOf(cache, tileX, tileZ);
    return i >= 0 ? &cache->tiles[i] : nullptr;
}

void mnavDropCachedTile(mnavTileCache* cache, int32_t tileX, int32_t tileZ)
{
    int32_t i = cache != nullptr ? IndexOf(cache, tileX, tileZ) : -1;
    if (i < 0)
    {
        return;
    }
    Release(&cache->memory, &cache->tiles[i]);
    cache->tiles[i] = cache->tiles[cache->count - 1];
    cache->count -= 1;
}

uint64_t mnavGetTileCacheBytes(const mnavTileCache* cache)
{
    return cache != nullptr ? cache->memory.used : 0;
}

// The volumes the entry keeps: those reaching the tile, in order, and the
// first include when there is one and none of those includes. Returns how
// many, and their points in total.
static int32_t Kept(const mnavTileFrame* frame, const mnavBakeVolume* volumes, int32_t count,
                    bool* keep, int32_t* pointsOut)
{
    int32_t kept = 0;
    int32_t points = 0;
    int32_t firstInclude = -1;
    bool keptInclude = false;
    for (int32_t i = 0; i < count; ++i)
    {
        bool include = volumes[i].kind == mnav_volumeInclude;
        firstInclude = include && firstInclude < 0 ? i : firstInclude;
        keep[i] = mnavRingTouchesTile(frame, volumes[i].points, volumes[i].pointCount);
        keptInclude = keptInclude || (keep[i] && include);
    }
    if (firstInclude >= 0 && !keptInclude)
    {
        keep[firstInclude] = true;
    }
    for (int32_t i = 0; i < count; ++i)
    {
        kept += keep[i] ? 1 : 0;
        points += keep[i] ? volumes[i].pointCount : 0;
    }
    *pointsOut = points;
    return kept;
}

// Copies the kept volumes and their points into the entry.
static mnavResult CopyVolumes(mnavMemory* memory, const mnavBakeVolume* volumes, int32_t count,
                              const bool* keep, mnavCachedTile* tile)
{
    mnavResult result = mnavAllocate(memory, (size_t)tile->volumeCount, sizeof(mnavBakeVolume),
                                     alignof(mnavBakeVolume), (void**)&tile->volumes);
    if (result == mnav_success)
    {
        result = mnavAllocate(memory, (size_t)tile->pointCount, sizeof(mnavVec2), alignof(mnavVec2),
                              (void**)&tile->points);
    }
    if (result != mnav_success)
    {
        return result;
    }
    int32_t v = 0;
    int32_t p = 0;
    for (int32_t i = 0; i < count; ++i)
    {
        if (!keep[i])
        {
            continue;
        }
        tile->volumes[v] = volumes[i];
        tile->volumes[v].points = &tile->points[p];
        memcpy(&tile->points[p], volumes[i].points,
               (size_t)volumes[i].pointCount * sizeof(mnavVec2));
        p += volumes[i].pointCount;
        v += 1;
    }
    return mnav_success;
}

mnavResult mnavCacheTile(mnavTileCache* cache, mnavMemory* scratch, const mnavCachedTile* tile,
                         const mnavCompactField* field, const mnavBakeVolume* volumes,
                         int32_t volumeCount)
{
    mnavDropCachedTile(cache, tile->tileX, tile->tileZ);
    if (cache->count == cache->def.limits.tiles)
    {
        return mnav_errorLimit;
    }
    // The volumes kept are marked in the cache's memory for the while.
    bool* keep = nullptr;
    mnavResult result = mnavAllocate(&cache->memory, (size_t)volumeCount, sizeof(bool),
                                     alignof(bool), (void**)&keep);
    if (result != mnav_success)
    {
        return result;
    }
    mnavCachedTile entry = {.tileX = tile->tileX,
                            .tileZ = tile->tileZ,
                            .settings = tile->settings,
                            .geometry = tile->geometry,
                            .triangles = tile->triangles};
    entry.volumeCount = Kept(&field->frame, volumes, volumeCount, keep, &entry.pointCount);
    result = CopyVolumes(&cache->memory, volumes, volumeCount, keep, &entry);
    mnavRelease(&cache->memory, keep, (size_t)volumeCount, sizeof(bool), alignof(bool));
    if (result == mnav_success)
    {
        result = mnavPackField(scratch, &cache->memory, field, &entry.field);
    }
    if (result != mnav_success)
    {
        Release(&cache->memory, &entry);
        return result;
    }
    cache->tiles[cache->count++] = entry;
    return mnav_success;
}
