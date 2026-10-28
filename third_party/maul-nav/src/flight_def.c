// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The flight def: its default, its check and its shape in voxels.

#include "flight_def.h"

#include "fpenv.h"
#include "scalar.h"

#include "maul-nav/bake.h"
#include "maul-nav/flight.h"

#include <math.h>
#include <stdbool.h>

// The voxels the heightfield adds to the radius for a tile's border
// (bake_def.c's margin), which must fit within the tile's side.
#define FLIGHT_BORDER_MARGIN 3

static mnavFlightDefResult Refuse(mnavResult result, mnavFlightSetting setting)
{
    return (mnavFlightDefResult){result, setting};
}

static const mnavFlightDefResult s_accept = {mnav_success, mnav_flightSettingNone};

static mnavFlightDefResult CheckLimits(const mnavFlightLimits* limits)
{
    const struct
    {
        int32_t value;
        int32_t max;
        mnavFlightSetting setting;
    } counts[] = {
        {limits->inputTriangles, MNAV_MAX_INPUT_TRIANGLES, mnav_flightSettingInputTriangles},
        {limits->tileSpans, MNAV_MAX_TILE_SPANS, mnav_flightSettingTileSpans},
        {limits->tileNodes, MNAV_MAX_FLIGHT_TILE_NODES, mnav_flightSettingTileNodes},
        {limits->tileLeaves, MNAV_MAX_FLIGHT_TILE_LEAVES, mnav_flightSettingTileLeaves},
        {limits->tiles, MNAV_MAX_TILES, mnav_flightSettingTiles},
    };
    for (size_t i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i)
    {
        if (counts[i].value < 1)
        {
            return Refuse(mnav_errorInvalid, counts[i].setting);
        }
        if (counts[i].value > counts[i].max)
        {
            return Refuse(mnav_errorLimit, counts[i].setting);
        }
    }
    return limits->memoryBytes == 0 ? Refuse(mnav_errorInvalid, mnav_flightSettingMemoryBytes)
                                    : s_accept;
}

// The tile's side: from MNAV_MIN_FLIGHT_TILE_VOXELS to
// MNAV_MAX_FLIGHT_TILE_VOXELS, and a power of 2.
static mnavFlightDefResult TileSide(int32_t side)
{
    if (side < MNAV_MIN_FLIGHT_TILE_VOXELS || side > MNAV_MAX_FLIGHT_TILE_VOXELS)
    {
        return Refuse(mnav_errorLimit, mnav_flightSettingTileVoxels);
    }
    return (side & (side - 1)) == 0 ? s_accept
                                    : Refuse(mnav_errorInvalid, mnav_flightSettingTileVoxels);
}

// The volume's floor voxel and cubes: mnav_errorInvalid for a bound not
// finite or a ceiling not above the floor, mnav_errorLimit for one past
// MNAV_MAX_HEIGHT_CELLS. The voxel size and side are valid.
static mnavFlightDefResult Shape(const mnavFlightDef* def, mnavFlightShape* shape)
{
    float low = def->floor / def->voxelSize;
    if (isnan(low))
    {
        return Refuse(mnav_errorInvalid, mnav_flightSettingFloor);
    }
    if (!(fabsf(low) <= (float)MNAV_MAX_HEIGHT_CELLS))
    {
        return Refuse(mnav_errorLimit, mnav_flightSettingFloor);
    }
    shape->floorVoxel = (int32_t)floorf(low);
    // Both bounds are within the height range, so the quotient is small.
    float high = def->ceiling / def->voxelSize;
    if (!(high > low))
    {
        return Refuse(mnav_errorInvalid, mnav_flightSettingCeiling);
    }
    if (!(high <= (float)MNAV_MAX_HEIGHT_CELLS))
    {
        return Refuse(mnav_errorLimit, mnav_flightSettingCeiling);
    }
    float cubes = ceilf((high - (float)shape->floorVoxel) / (float)def->tileVoxels);
    shape->cubeCount = cubes < 1.0f ? 1 : (int32_t)cubes;
    int64_t top = (int64_t)shape->floorVoxel + (int64_t)shape->cubeCount * def->tileVoxels;
    return top > MNAV_MAX_HEIGHT_CELLS ? Refuse(mnav_errorLimit, mnav_flightSettingCeiling)
                                       : s_accept;
}

mnavFlightDefResult mnavCheckFlightDef(const mnavFlightDef* def, mnavFlightShape* shapeOut)
{
    if (def == nullptr)
    {
        return Refuse(mnav_errorInvalid, mnav_flightSettingNone);
    }
    if (def->cookie != MNAV_FLIGHT_DEF_COOKIE)
    {
        return Refuse(mnav_errorInvalid, mnav_flightSettingCookie);
    }
    if (def->version != MNAV_ABI_VERSION)
    {
        return Refuse(mnav_errorVersion, mnav_flightSettingNone);
    }
    if ((def->allocator.alloc == nullptr) != (def->allocator.free == nullptr))
    {
        return Refuse(mnav_errorInvalid, mnav_flightSettingAllocator);
    }
    if (!isfinite(def->origin.x) || !isfinite(def->origin.y) || !isfinite(def->origin.z))
    {
        return Refuse(mnav_errorInvalid, mnav_flightSettingOrigin);
    }
    if (isnan(def->voxelSize))
    {
        return Refuse(mnav_errorInvalid, mnav_flightSettingVoxelSize);
    }
    if (!(def->voxelSize >= MNAV_MIN_CELL_SIZE && def->voxelSize <= MNAV_MAX_CELL_SIZE))
    {
        return Refuse(mnav_errorLimit, mnav_flightSettingVoxelSize);
    }
    mnavFlightDefResult r = TileSide(def->tileVoxels);
    mnavFlightShape shape = {0};
    r = r.result == mnav_success ? Shape(def, &shape) : r;
    if (r.result != mnav_success)
    {
        return r;
    }
    if (!(def->radius >= 0.0f) ||
        !mnavToCells(def->radius, def->voxelSize, true, def->tileVoxels - FLIGHT_BORDER_MARGIN,
                     &shape.radius))
    {
        return Refuse(mnav_errorInvalid, mnav_flightSettingRadius);
    }
    r = CheckLimits(&def->limits);
    if (r.result != mnav_success)
    {
        return r;
    }
    if (shapeOut != nullptr)
    {
        *shapeOut = shape;
    }
    return s_accept;
}

static MNAV_NOINLINE mnavFlightDefResult ValidateFlightDefWork(const mnavFlightDef* def)
{
    return mnavCheckFlightDef(def, nullptr);
}

mnavFlightDefResult mnavValidateFlightDef(const mnavFlightDef* def)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavFlightDefResult result = ValidateFlightDefWork(def);
    mnavFloatEnvLeave(env);
    return result;
}

mnavBakeDef mnavFlightBakeDef(const mnavFlightDef* def)
{
    mnavBakeDef bake = mnavDefaultBakeDef();
    bake.allocator = def->allocator;
    bake.origin = def->origin;
    bake.cellSize = def->voxelSize;
    bake.cellHeight = def->voxelSize;
    bake.tileCells = def->tileVoxels;
    bake.agent.radius = def->radius;
    bake.agent.height = def->voxelSize;
    bake.agent.stepHeight = 0.0f;
    bake.minRegionArea = 0.0f;
    bake.maxEdgeError = 0.0f;
    bake.maxEdgeLength = 0.0f;
    bake.detailSampleDistance = 0.0f;
    bake.detailMaxError = 0.0f;
    bake.limits.inputTriangles = def->limits.inputTriangles;
    bake.limits.tileSpans = def->limits.tileSpans;
    bake.limits.memoryBytes = def->limits.memoryBytes;
    return bake;
}
