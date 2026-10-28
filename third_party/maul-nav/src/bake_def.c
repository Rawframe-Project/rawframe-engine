// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The bake def: its default, its check and its conversion to cells.

#include "bake_def.h"

#include "fpenv.h"
#include "scalar.h"

#include "maul-nav/bake.h"

#include <math.h>
#include <stdbool.h>

// The cells added to the agent's radius for a tile's border, so that the
// erosion and the region filters near a tile's edge see the same cells as
// the neighbor tile does.
#define BORDER_MARGIN_CELLS 3

static MNAV_NOINLINE mnavBakeDefResult ValidateBakeDefWork(const mnavBakeDef* def,
                                                           mnavBakeCells* cellsOut)
{
    return mnavCheckBakeDef(def, cellsOut);
}

mnavBakeDefResult mnavValidateBakeDef(const mnavBakeDef* def, mnavBakeCells* cellsOut)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavBakeDefResult result = ValidateBakeDefWork(def, cellsOut);
    mnavFloatEnvLeave(env);
    return result;
}

static mnavBakeDefResult Refuse(mnavResult result, mnavBakeSetting setting)
{
    return (mnavBakeDefResult){result, setting};
}

static const mnavBakeDefResult s_accept = {mnav_success, mnav_settingNone};

static bool InRange(float value, float low, float high)
{
    // A NaN fails both comparisons.
    return value >= low && value <= high;
}

// A value of a setting with published bounds: mnav_errorInvalid for a NaN,
// mnav_errorLimit outside the bounds, an infinity included.
static mnavBakeDefResult Bounded(float value, float low, float high, mnavBakeSetting setting)
{
    if (isnan(value))
    {
        return Refuse(mnav_errorInvalid, setting);
    }
    return InRange(value, low, high) ? s_accept : Refuse(mnav_errorLimit, setting);
}

// A length converted to cells against a most of cells: mnav_errorInvalid
// for a NaN, a value below 0, or 0 where it must be positive; past the
// most, an infinity included, the status given.
static mnavBakeDefResult Cells(float value, float unit, bool roundUp, int32_t most, mnavResult past,
                               bool positive, mnavBakeSetting setting, int32_t* cells)
{
    if (!InRange(value, 0.0f, INFINITY) || (positive && value == 0.0f))
    {
        return Refuse(mnav_errorInvalid, setting);
    }
    return mnavToCells(value, unit, roundUp, most, cells) ? s_accept : Refuse(past, setting);
}

static mnavBakeDefResult CheckLimits(const mnavBakeLimits* limits)
{
    const struct
    {
        int32_t value;
        int32_t max;
        mnavBakeSetting setting;
    } counts[] = {
        {limits->inputTriangles, MNAV_MAX_INPUT_TRIANGLES, mnav_settingInputTriangles},
        {limits->tileTriangles, MNAV_MAX_INPUT_TRIANGLES, mnav_settingTileTriangles},
        {limits->tileSpans, MNAV_MAX_TILE_SPANS, mnav_settingTileSpans},
        {limits->tilePolygons, MNAV_MAX_TILE_POLYGONS, mnav_settingTilePolygons},
        {limits->tileVertices, MNAV_MAX_TILE_VERTICES, mnav_settingTileVertices},
        {limits->tileLinks, MNAV_MAX_TILE_LINKS, mnav_settingTileLinks},
        {limits->tiles, MNAV_MAX_TILES, mnav_settingTiles},
        {limits->links, MNAV_MAX_LINKS, mnav_settingLinks},
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
    if (limits->memoryBytes == 0)
    {
        return Refuse(mnav_errorInvalid, mnav_settingMemoryBytes);
    }
    return s_accept;
}

static mnavBakeDefResult CheckGrid(const mnavBakeDef* def)
{
    if ((def->allocator.alloc == nullptr) != (def->allocator.free == nullptr))
    {
        return Refuse(mnav_errorInvalid, mnav_settingAllocator);
    }
    if (!isfinite(def->origin.x) || !isfinite(def->origin.y) || !isfinite(def->origin.z))
    {
        return Refuse(mnav_errorInvalid, mnav_settingOrigin);
    }
    mnavBakeDefResult r =
        Bounded(def->cellSize, MNAV_MIN_CELL_SIZE, MNAV_MAX_CELL_SIZE, mnav_settingCellSize);
    if (r.result == mnav_success)
    {
        r = Bounded(def->cellHeight, MNAV_MIN_CELL_SIZE, MNAV_MAX_CELL_SIZE,
                    mnav_settingCellHeight);
    }
    if (r.result != mnav_success)
    {
        return r;
    }
    if (def->tileCells < MNAV_MIN_TILE_CELLS || def->tileCells > MNAV_MAX_TILE_CELLS)
    {
        return Refuse(mnav_errorLimit, mnav_settingTileCells);
    }
    if (def->tier > mnav_tierDynamic)
    {
        return Refuse(mnav_errorInvalid, mnav_settingTier);
    }
    return s_accept;
}

// Converts the agent; the grid settings are already valid. Bounds the def
// sets for itself (the radius within a tile) and the unnamed 2^30 cells are
// mnav_errorInvalid; MNAV_MAX_ ones are mnav_errorLimit.
static mnavBakeDefResult ConvertAgent(const mnavBakeDef* def, mnavBakeCells* cells)
{
    const mnavAgentProfile* agent = &def->agent;
    mnavBakeDefResult r =
        Cells(agent->radius, def->cellSize, true, def->tileCells, mnav_errorInvalid, false,
              mnav_settingAgentRadius, &cells->agentRadius);
    if (r.result == mnav_success && cells->agentRadius + BORDER_MARGIN_CELLS > def->tileCells)
    {
        r = Refuse(mnav_errorInvalid, mnav_settingAgentRadius);
    }
    if (r.result == mnav_success)
    {
        r = Cells(agent->height, def->cellHeight, true, MNAV_MAX_HEIGHT_CELLS, mnav_errorLimit,
                  true, mnav_settingAgentHeight, &cells->agentHeight);
    }
    if (r.result == mnav_success)
    {
        r = Cells(agent->stepHeight, def->cellHeight, false, MNAV_MAX_HEIGHT_CELLS, mnav_errorLimit,
                  false, mnav_settingAgentStepHeight, &cells->agentStep);
    }
    if (r.result == mnav_success &&
        !(agent->maxSlopeDegrees >= 0.0f && agent->maxSlopeDegrees < 90.0f))
    {
        r = Refuse(mnav_errorInvalid, mnav_settingAgentMaxSlope);
    }
    if (r.result != mnav_success)
    {
        return r;
    }
    // A height that rounds to 0 cells still needs one free cell above.
    if (cells->agentHeight == 0)
    {
        cells->agentHeight = 1;
    }
    // A cell's area rounds once; every platform rounds it the same. An
    // area larger than any tile is valid (it drops every island) up to
    // 2^30 cells, past which it can only be a mistake.
    float cellArea = def->cellSize * def->cellSize;
    r = Cells(def->minRegionArea, cellArea, true, 1 << 30, mnav_errorInvalid, false,
              mnav_settingMinRegionArea, &cells->minRegion);
    if (r.result == mnav_success && !InRange(def->maxEdgeError, 0.0f, INFINITY))
    {
        r = Refuse(mnav_errorInvalid, mnav_settingMaxEdgeError);
    }
    if (r.result == mnav_success && def->maxEdgeError > MNAV_MAX_CELL_SIZE * MNAV_MAX_TILE_CELLS)
    {
        r = Refuse(mnav_errorLimit, mnav_settingMaxEdgeError);
    }
    if (r.result != mnav_success)
    {
        return r;
    }
    cells->edgeError = def->maxEdgeError / def->cellSize;
    r = Cells(def->maxEdgeLength, def->cellSize, false, 1 << 30, mnav_errorInvalid, false,
              mnav_settingMaxEdgeLength, &cells->edgeLength);
    // Sixteenths of a cell and of a cell height are exact: the division
    // only lowers the exponent.
    if (r.result == mnav_success)
    {
        r = Cells(def->detailSampleDistance, def->cellSize / 16.0f, false, 1 << 30,
                  mnav_errorInvalid, false, mnav_settingDetailSampleDistance, &cells->detailSample);
    }
    if (r.result == mnav_success)
    {
        r = Cells(def->detailMaxError, def->cellHeight / 16.0f, false, 1 << 30, mnav_errorInvalid,
                  false, mnav_settingDetailMaxError, &cells->detailError);
    }
    if (r.result != mnav_success)
    {
        return r;
    }
    // Heights come one per cell; sampling closer than a cell adds nothing.
    if (cells->detailSample > 0 && cells->detailSample < 16)
    {
        cells->detailSample = 16;
    }
    cells->border = cells->agentRadius + BORDER_MARGIN_CELLS;
    cells->cosMaxSlope = mnavCosDegrees(agent->maxSlopeDegrees);
    cells->tileSize = (float)def->tileCells * def->cellSize;
    return s_accept;
}

mnavBakeDefResult mnavCheckBakeDef(const mnavBakeDef* def, mnavBakeCells* cells)
{
    if (def == nullptr)
    {
        return Refuse(mnav_errorInvalid, mnav_settingNone);
    }
    if (def->cookie != MNAV_BAKE_DEF_COOKIE)
    {
        return Refuse(mnav_errorInvalid, mnav_settingCookie);
    }
    if (def->version != MNAV_ABI_VERSION)
    {
        return Refuse(mnav_errorVersion, mnav_settingNone);
    }
    mnavBakeDefResult r = CheckGrid(def);
    if (r.result != mnav_success)
    {
        return r;
    }
    mnavBakeCells converted = {0};
    r = ConvertAgent(def, &converted);
    if (r.result == mnav_success)
    {
        r = CheckLimits(&def->limits);
    }
    if (r.result != mnav_success)
    {
        return r;
    }
    if (cells != nullptr)
    {
        *cells = converted;
    }
    return s_accept;
}
