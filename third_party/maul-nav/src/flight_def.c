// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The flight def: its default, its check and its shape in voxels.

#include "flight_def.h"

#include "scalar.h"

#include "maul-nav/bake.h"
#include "maul-nav/flight.h"

#include <math.h>
#include <stdbool.h>

// Marks a def built by mnavDefaultFlightDef.
#define FLIGHT_DEF_COOKIE 0x464E564Du

// The voxels the heightfield adds to the radius for a tile's border
// (bake_def.c's margin), which must fit within the tile's side.
#define FLIGHT_BORDER_MARGIN 3

mnavFlightDef mnavDefaultFlightDef(void)
{
    mnavFlightDef def = {0};
    def.cookie = FLIGHT_DEF_COOKIE;
    def.voxelSize = 1.0f;
    def.tileVoxels = 32;
    def.floor = 0.0f;
    def.ceiling = 64.0f;
    def.radius = 0.5f;
    def.groundBelow = true;
    def.limits.inputTriangles = 4194304;
    def.limits.tileSpans = 4194304;
    def.limits.tileNodes = 1048576;
    def.limits.tileLeaves = 1048576;
    def.limits.tiles = 65536;
    def.limits.memoryBytes = 268435456;
    return def;
}

static mnavFlightDefResult Refuse(mnavFlightSetting setting)
{
    return (mnavFlightDefResult){mnav_errorInvalid, setting};
}

static mnavFlightSetting CheckLimits(const mnavFlightLimits* limits)
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
        if (counts[i].value < 1 || counts[i].value > counts[i].max)
        {
            return counts[i].setting;
        }
    }
    return limits->memoryBytes == 0 ? mnav_flightSettingMemoryBytes : mnav_flightSettingNone;
}

static bool TileSide(int32_t side)
{
    return side >= MNAV_MIN_FLIGHT_TILE_VOXELS && side <= MNAV_MAX_FLIGHT_TILE_VOXELS &&
           (side & (side - 1)) == 0;
}

// The volume's floor voxel and cubes, or the setting that puts them out
// of range; the voxel size and side are valid.
static mnavFlightSetting Shape(const mnavFlightDef* def, mnavFlightShape* shape)
{
    float low = def->floor / def->voxelSize;
    if (!(fabsf(low) <= (float)MNAV_MAX_HEIGHT_CELLS))
    {
        return mnav_flightSettingFloor;
    }
    shape->floorVoxel = (int32_t)floorf(low);
    // Both bounds are within the height range, so the quotient is small.
    float high = def->ceiling / def->voxelSize;
    if (!(high > low) || !(high <= (float)MNAV_MAX_HEIGHT_CELLS))
    {
        return mnav_flightSettingCeiling;
    }
    float cubes = ceilf((high - (float)shape->floorVoxel) / (float)def->tileVoxels);
    shape->cubeCount = cubes < 1.0f ? 1 : (int32_t)cubes;
    int64_t top = (int64_t)shape->floorVoxel + (int64_t)shape->cubeCount * def->tileVoxels;
    return top > MNAV_MAX_HEIGHT_CELLS ? mnav_flightSettingCeiling : mnav_flightSettingNone;
}

mnavFlightDefResult mnavCheckFlightDef(const mnavFlightDef* def, mnavFlightShape* shapeOut)
{
    if (def == nullptr)
    {
        return Refuse(mnav_flightSettingNone);
    }
    if (def->cookie != FLIGHT_DEF_COOKIE)
    {
        return Refuse(mnav_flightSettingCookie);
    }
    if ((def->allocator.alloc == nullptr) != (def->allocator.free == nullptr))
    {
        return Refuse(mnav_flightSettingAllocator);
    }
    if (!isfinite(def->origin.x) || !isfinite(def->origin.y) || !isfinite(def->origin.z))
    {
        return Refuse(mnav_flightSettingOrigin);
    }
    if (!(def->voxelSize >= MNAV_MIN_CELL_SIZE && def->voxelSize <= MNAV_MAX_CELL_SIZE))
    {
        return Refuse(mnav_flightSettingVoxelSize);
    }
    if (!TileSide(def->tileVoxels))
    {
        return Refuse(mnav_flightSettingTileVoxels);
    }
    mnavFlightShape shape = {0};
    mnavFlightSetting setting = Shape(def, &shape);
    if (setting != mnav_flightSettingNone)
    {
        return Refuse(setting);
    }
    if (!(def->radius >= 0.0f) ||
        !mnavToCells(def->radius, def->voxelSize, true, def->tileVoxels - FLIGHT_BORDER_MARGIN,
                     &shape.radius))
    {
        return Refuse(mnav_flightSettingRadius);
    }
    setting = CheckLimits(&def->limits);
    if (setting != mnav_flightSettingNone)
    {
        return Refuse(setting);
    }
    if (shapeOut != nullptr)
    {
        *shapeOut = shape;
    }
    return (mnavFlightDefResult){mnav_success, mnav_flightSettingNone};
}

mnavFlightDefResult mnavValidateFlightDef(const mnavFlightDef* def)
{
    return mnavCheckFlightDef(def, nullptr);
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
