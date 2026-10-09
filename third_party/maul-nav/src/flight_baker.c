// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The flight baker (mnav-0015): one call checks the input, builds a
// flight tile and encodes it, fingerprinted as navmesh tiles are.

#include "allocator.h"
#include "bake_def.h"
#include "bake_input.h"
#include "bytes.h"
#include "fingerprint.h"
#include "flight.h"
#include "flight_def.h"
#include "flight_tile.h"
#include "raster.h"

#include "maul-nav/bake.h"
#include "maul-nav/base.h"
#include "maul-nav/flight.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct mnavFlightBaker
{
    mnavFlightDef def;
    mnavFlightShape shape;
    // The heightfield's def and cells at the voxel size.
    mnavBakeDef voxels;
    mnavBakeCells cells;
    mnavMemory memory;
    uint8_t* tile;
    size_t tileSize;
};

mnavFlightDefResult mnavCreateFlightBaker(const mnavFlightDef* def, mnavFlightBaker** bakerOut)
{
    if (bakerOut == nullptr)
    {
        return (mnavFlightDefResult){mnav_errorInvalid, mnav_flightSettingNone};
    }
    *bakerOut = nullptr;
    mnavFlightShape shape = {0};
    mnavFlightDefResult checked = mnavCheckFlightDef(def, &shape);
    if (checked.result != mnav_success)
    {
        return checked;
    }
    mnavBakeDef voxels = mnavFlightBakeDef(def);
    mnavBakeCells cells = {0};
    if (mnavCheckBakeDef(&voxels, &cells).result != mnav_success)
    {
        return (mnavFlightDefResult){mnav_errorInvalid, mnav_flightSettingNone};
    }
    mnavMemory memory = mnavMakeMemory(def->allocator, def->limits.memoryBytes);
    mnavFlightBaker* baker = nullptr;
    mnavResult result =
        mnavAllocate(&memory, 1, sizeof(mnavFlightBaker), alignof(mnavFlightBaker), (void**)&baker);
    if (result != mnav_success)
    {
        return (mnavFlightDefResult){result, mnav_flightSettingNone};
    }
    *baker = (mnavFlightBaker){*def, shape, voxels, cells, memory, nullptr, 0};
    *bakerOut = baker;
    return (mnavFlightDefResult){mnav_success, mnav_flightSettingNone};
}

static void DropTile(mnavFlightBaker* baker)
{
    mnavReleaseFlightTileBytes(&baker->memory, baker->tile, baker->tileSize);
    baker->tile = nullptr;
    baker->tileSize = 0;
}

void mnavDestroyFlightBaker(mnavFlightBaker* baker)
{
    if (baker == nullptr)
    {
        return;
    }
    DropTile(baker);
    mnavMemory memory = baker->memory;
    mnavRelease(&memory, baker, 1, sizeof(mnavFlightBaker), alignof(mnavFlightBaker));
}

// The hash of the generator, the settings that shape the tile and its
// place, after a word that keeps flight tiles apart from navmesh tiles.
static uint64_t HashSettings(const mnavFlightDef* def, int32_t tileX, int32_t tileZ)
{
    mnavVersion version = mnavGetVersion();
    uint64_t origin[3] = {mnavDoubleBits(def->origin.x), mnavDoubleBits(def->origin.y),
                          mnavDoubleBits(def->origin.z)};
    const uint32_t words[] = {
        0x464E564Du,
        version.major,
        version.minor,
        version.patch,
        (uint32_t)origin[0],
        (uint32_t)(origin[0] >> 32),
        (uint32_t)origin[1],
        (uint32_t)(origin[1] >> 32),
        (uint32_t)origin[2],
        (uint32_t)(origin[2] >> 32),
        mnavFloatBits(def->voxelSize),
        (uint32_t)def->tileVoxels,
        mnavFloatBits(def->floor),
        mnavFloatBits(def->ceiling),
        mnavFloatBits(def->radius),
        def->groundBelow ? 1u : 0u,
        (uint32_t)tileX,
        (uint32_t)tileZ,
    };
    return mnavHashWords(MNAV_HASH_INIT, words, (int32_t)(sizeof(words) / sizeof(words[0])));
}

static mnavResult Bake(mnavFlightBaker* baker, const mnavBakeInput* input, int32_t tileX,
                       int32_t tileZ, mnavFlightBakeReport* report)
{
    if (!mnavCheckInputArrays(input))
    {
        return mnav_errorInvalid;
    }
    mnavResult result =
        mnavCheckSolidInput(&baker->voxels, input, false, &report->mesh, &report->input);
    mnavTileFrame frame = {0};
    if (result == mnav_success &&
        !mnavMakeTileFrame(&baker->voxels, &baker->cells, tileX, tileZ, &frame))
    {
        result = mnav_errorRange;
    }
    if (result != mnav_success)
    {
        return result;
    }
    mnavBakeInput all = *input;
    all.index = nullptr;
    report->fingerprint =
        mnavFingerprintInput(&frame, baker->cells.cosMaxSlope, &all, tileX, tileZ,
                             HashSettings(&baker->def, tileX, tileZ), &report->triangles);
    mnavFlightTile tile = {0};
    result = mnavBuildFlightTile(&baker->memory, &baker->def, &baker->shape, input, tileX, tileZ,
                                 &tile, &report->spans);
    if (result == mnav_success)
    {
        report->cubes = tile.cubeCount;
        report->nodes = tile.nodeCount;
        report->leaves = tile.leafCount;
        result = mnavEncodeFlightTile(&baker->memory, &baker->def, mnavGetVersion(),
                                      report->fingerprint, &tile, &baker->tile, &baker->tileSize);
    }
    mnavReleaseFlightTile(&baker->memory, &tile);
    return result;
}

mnavResult mnavBakeFlightTile(mnavFlightBaker* baker, const mnavBakeInput* input, int32_t tileX,
                              int32_t tileZ, mnavFlightBakeReport* reportOut)
{
    mnavFlightBakeReport report = {0};
    report.mesh = -1;
    report.input = (mnavInputResult){mnav_success, mnav_elementNone, -1};
    mnavResult result = mnav_errorInvalid;
    if (baker != nullptr && input != nullptr)
    {
        DropTile(baker);
        baker->memory.peak = baker->memory.used;
        result = Bake(baker, input, tileX, tileZ, &report);
        report.tileBytes = result == mnav_success ? baker->tileSize : 0;
        report.memoryPeak = baker->memory.peak;
    }
    report.result = result;
    if (reportOut != nullptr)
    {
        *reportOut = report;
    }
    return result;
}

mnavResult mnavCopyFlightTile(const mnavFlightBaker* baker, uint8_t* buffer, size_t capacity,
                              size_t* sizeOut)
{
    if (baker == nullptr || (buffer == nullptr && capacity > 0) || baker->tile == nullptr)
    {
        return mnav_errorInvalid;
    }
    if (sizeOut != nullptr)
    {
        *sizeOut = baker->tileSize;
    }
    if (capacity < baker->tileSize)
    {
        return mnav_errorCapacity;
    }
    if (buffer != nullptr)
    {
        memcpy(buffer, baker->tile, baker->tileSize);
    }
    return mnav_success;
}
