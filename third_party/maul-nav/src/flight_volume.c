// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The flight volume (mnav-0015): flight tiles loaded from bytes, staged
// and committed together, kept sorted by place.

#include "flight_volume.h"

#include "allocator.h"
#include "flight.h"
#include "flight_def.h"
#include "flight_tile.h"

#include "maul-nav/base.h"
#include "maul-nav/flight.h"
#include "maul-nav/navmesh.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

mnavFlightDefResult mnavCreateFlightVolume(const mnavFlightDef* def, mnavFlightVolume** volumeOut)
{
    if (volumeOut == nullptr)
    {
        return (mnavFlightDefResult){mnav_errorInvalid, mnav_flightSettingNone};
    }
    *volumeOut = nullptr;
    mnavFlightShape shape = {0};
    mnavFlightDefResult checked = mnavCheckFlightDef(def, &shape);
    if (checked.result != mnav_success)
    {
        return checked;
    }
    mnavMemory memory = mnavMakeMemory(def->allocator, def->limits.memoryBytes);
    mnavFlightVolume* volume = nullptr;
    mnavResult result = mnavAllocate(&memory, 1, sizeof(mnavFlightVolume),
                                     alignof(mnavFlightVolume), (void**)&volume);
    if (result != mnav_success)
    {
        return (mnavFlightDefResult){result, mnav_flightSettingNone};
    }
    *volume = (mnavFlightVolume){.def = *def, .shape = shape, .memory = memory};
    *volumeOut = volume;
    return (mnavFlightDefResult){mnav_success, mnav_flightSettingNone};
}

static void DropEntries(mnavMemory* memory, mnavFlightEntry* entries, int32_t count)
{
    for (int32_t i = 0; i < count; ++i)
    {
        mnavReleaseFlightTile(memory, &entries[i].tile);
    }
}

void mnavDestroyFlightVolume(mnavFlightVolume* volume)
{
    if (volume == nullptr)
    {
        return;
    }
    DropEntries(&volume->memory, volume->tiles, volume->tileCount);
    DropEntries(&volume->memory, volume->staged, volume->stagedCount);
    mnavRelease(&volume->memory, volume->tiles, (size_t)volume->tileCount, sizeof(mnavFlightEntry),
                alignof(mnavFlightEntry));
    mnavRelease(&volume->memory, volume->staged, (size_t)volume->stagedCapacity,
                sizeof(mnavFlightEntry), alignof(mnavFlightEntry));
    mnavMemory memory = volume->memory;
    mnavRelease(&memory, volume, 1, sizeof(mnavFlightVolume), alignof(mnavFlightVolume));
}

// Orders places by x, then z.
static int32_t Compare(int32_t ax, int32_t az, int32_t bx, int32_t bz)
{
    if (ax != bx)
    {
        return ax < bx ? -1 : 1;
    }
    return az < bz ? -1 : (az > bz ? 1 : 0);
}

// The committed entry at a place, or NULL.
static const mnavFlightEntry* Find(const mnavFlightVolume* volume, int32_t x, int32_t z)
{
    int32_t low = 0;
    int32_t high = volume->tileCount;
    while (low < high)
    {
        int32_t mid = low + (high - low) / 2;
        int32_t order = Compare(volume->tiles[mid].x, volume->tiles[mid].z, x, z);
        if (order == 0)
        {
            return &volume->tiles[mid];
        }
        low = order < 0 ? mid + 1 : low;
        high = order < 0 ? high : mid;
    }
    return nullptr;
}

// Stages a change in place of anything staged at its place; on failure
// the change's tile is released.
static mnavResult Stage(mnavFlightVolume* volume, mnavFlightEntry change)
{
    for (int32_t i = 0; i < volume->stagedCount; ++i)
    {
        mnavFlightEntry* e = &volume->staged[i];
        if (e->x == change.x && e->z == change.z)
        {
            mnavReleaseFlightTile(&volume->memory, &e->tile);
            *e = change;
            return mnav_success;
        }
    }
    mnavResult result = mnavReserve(
        &volume->memory, (void**)&volume->staged, &volume->stagedCapacity, volume->stagedCount,
        volume->stagedCount + 1, sizeof(mnavFlightEntry), alignof(mnavFlightEntry));
    if (result != mnav_success)
    {
        mnavReleaseFlightTile(&volume->memory, &change.tile);
        return result;
    }
    volume->staged[volume->stagedCount++] = change;
    return mnav_success;
}

mnavTileResult mnavStageFlightTile(mnavFlightVolume* volume, const uint8_t* bytes, size_t size)
{
    if (volume == nullptr || bytes == nullptr)
    {
        return (mnavTileResult){mnav_errorInvalid, mnav_tileHeader, -1};
    }
    mnavFlightEntry change = {0};
    mnavTileResult loaded = mnavDecodeFlightTile(&volume->memory, &volume->def, &volume->shape,
                                                 bytes, size, &change.tile, &change.fingerprint);
    if (loaded.result != mnav_success)
    {
        return loaded;
    }
    change.x = change.tile.tileX;
    change.z = change.tile.tileZ;
    mnavResult result = Stage(volume, change);
    return (mnavTileResult){result, mnav_tileHeader, -1};
}

mnavResult mnavStageFlightTileRemoval(mnavFlightVolume* volume, int32_t tileX, int32_t tileZ)
{
    if (volume == nullptr)
    {
        return mnav_errorInvalid;
    }
    return Stage(volume, (mnavFlightEntry){.x = tileX, .z = tileZ, .removal = true});
}

static int OrderEntries(const void* a, const void* b)
{
    const mnavFlightEntry* ea = a;
    const mnavFlightEntry* eb = b;
    return Compare(ea->x, ea->z, eb->x, eb->z);
}

// The tiles a commit leaves: the committed ones, less those removed, and
// those installed at new places.
static int64_t CountAfter(const mnavFlightVolume* volume)
{
    int64_t count = volume->tileCount;
    for (int32_t i = 0; i < volume->stagedCount; ++i)
    {
        const mnavFlightEntry* e = &volume->staged[i];
        bool held = Find(volume, e->x, e->z) != nullptr;
        count += e->removal ? (held ? -1 : 0) : (held ? 0 : 1);
    }
    return count;
}

// Merges the sorted staged changes into the committed tiles, into next,
// releasing the tiles replaced or removed.
static void Merge(mnavFlightVolume* volume, mnavFlightEntry* next)
{
    int32_t n = 0;
    int32_t t = 0;
    for (int32_t s = 0; s < volume->stagedCount; ++s)
    {
        const mnavFlightEntry* change = &volume->staged[s];
        while (t < volume->tileCount &&
               Compare(volume->tiles[t].x, volume->tiles[t].z, change->x, change->z) < 0)
        {
            next[n++] = volume->tiles[t++];
        }
        if (t < volume->tileCount && volume->tiles[t].x == change->x &&
            volume->tiles[t].z == change->z)
        {
            mnavReleaseFlightTile(&volume->memory, &volume->tiles[t].tile);
            ++t;
        }
        if (!change->removal)
        {
            next[n++] = *change;
        }
    }
    while (t < volume->tileCount)
    {
        next[n++] = volume->tiles[t++];
    }
}

mnavResult mnavCommitFlight(mnavFlightVolume* volume)
{
    if (volume == nullptr)
    {
        return mnav_errorInvalid;
    }
    int64_t count = CountAfter(volume);
    if (count > volume->def.limits.tiles)
    {
        return mnav_errorLimit;
    }
    mnavFlightEntry* next = nullptr;
    if (count > 0)
    {
        mnavResult result = mnavAllocate(&volume->memory, (size_t)count, sizeof(mnavFlightEntry),
                                         alignof(mnavFlightEntry), (void**)&next);
        if (result != mnav_success)
        {
            return result;
        }
    }
    if (volume->stagedCount > 1)
    {
        qsort(volume->staged, (size_t)volume->stagedCount, sizeof(mnavFlightEntry), OrderEntries);
    }
    if (next != nullptr)
    {
        Merge(volume, next);
    }
    else
    {
        // Nothing is left: every tile is removed, and the staged changes
        // are removals, which hold no tile.
        DropEntries(&volume->memory, volume->tiles, volume->tileCount);
    }
    mnavRelease(&volume->memory, volume->tiles, (size_t)volume->tileCount, sizeof(mnavFlightEntry),
                alignof(mnavFlightEntry));
    volume->tiles = next;
    volume->tileCount = (int32_t)count;
    volume->stagedCount = 0;
    return mnav_success;
}

mnavResult mnavGetFlightTile(const mnavFlightVolume* volume, int32_t tileX, int32_t tileZ,
                             uint64_t* fingerprintOut)
{
    if (volume == nullptr || fingerprintOut == nullptr)
    {
        return mnav_errorInvalid;
    }
    const mnavFlightEntry* e = Find(volume, tileX, tileZ);
    *fingerprintOut = e != nullptr ? e->fingerprint : 0;
    return e != nullptr ? mnav_success : mnav_errorNotLoaded;
}

// The voxel holding a coordinate, from the origin's, or false past what
// a voxel index can be.
static bool VoxelOf(double meters, double voxel, int64_t* voxelOut)
{
    double q = floor(meters / voxel);
    if (!(fabs(q) < 1073741824.0))
    {
        return false;
    }
    *voxelOut = (int64_t)q;
    return true;
}

// Rounds a quotient toward minus infinity.
static int64_t FloorDiv(int64_t a, int64_t b)
{
    int64_t q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}

mnavResult mnavIsFlightOpen(const mnavFlightVolume* volume, mnavPos3 point, bool* openOut)
{
    if (volume == nullptr || openOut == nullptr)
    {
        return mnav_errorInvalid;
    }
    *openOut = false;
    const mnavFlightDef* def = &volume->def;
    double voxel = (double)def->voxelSize;
    int64_t v[3] = {0, 0, 0};
    if (!VoxelOf(point.x - def->origin.x, voxel, &v[0]) ||
        !VoxelOf(point.y - def->origin.y, voxel, &v[1]) ||
        !VoxelOf(point.z - def->origin.z, voxel, &v[2]))
    {
        return mnav_errorRange;
    }
    int64_t side = def->tileVoxels;
    int64_t y = v[1] - volume->shape.floorVoxel;
    if (y < 0 || y >= (int64_t)volume->shape.cubeCount * side)
    {
        return mnav_errorRange;
    }
    int64_t tileX = FloorDiv(v[0], side);
    int64_t tileZ = FloorDiv(v[2], side);
    const mnavFlightEntry* e = Find(volume, (int32_t)tileX, (int32_t)tileZ);
    if (e == nullptr)
    {
        return mnav_errorNotLoaded;
    }
    *openOut = !mnavFlightSolid(&e->tile, (int32_t)(v[0] - tileX * side), (int32_t)y,
                                (int32_t)(v[2] - tileZ * side));
    return mnav_success;
}

const mnavFlightTile* mnavFlightTileAt(const mnavFlightVolume* volume, int32_t tileX, int32_t tileZ)
{
    const mnavFlightEntry* e = Find(volume, tileX, tileZ);
    return e != nullptr ? &e->tile : nullptr;
}
