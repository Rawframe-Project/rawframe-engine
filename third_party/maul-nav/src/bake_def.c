// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The bake def: its default, its check and its conversion to cells.

#include "bake_def.h"

#include "scalar.h"

#include "maul-nav/bake.h"

#include <math.h>
#include <stdbool.h>

// The cells added to the agent's radius for a tile's border, so that the
// erosion and the region filters near a tile's edge see the same cells as
// the neighbor tile does.
#define BORDER_MARGIN_CELLS 3

mnavBakeDefResult mnavValidateBakeDef(const mnavBakeDef* def, mnavBakeCells* cellsOut)
{
    return mnavCheckBakeDef(def, cellsOut);
}

static mnavBakeDefResult Refuse(mnavBakeSetting setting)
{
    return (mnavBakeDefResult){mnav_errorInvalid, setting};
}

static bool InRange(float value, float low, float high)
{
    // A NaN fails both comparisons.
    return value >= low && value <= high;
}

static mnavBakeSetting CheckLimits(const mnavBakeLimits* limits)
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
        if (counts[i].value < 1 || counts[i].value > counts[i].max)
        {
            return counts[i].setting;
        }
    }
    if (limits->memoryBytes == 0)
    {
        return mnav_settingMemoryBytes;
    }
    return mnav_settingNone;
}

static mnavBakeSetting CheckGrid(const mnavBakeDef* def)
{
    if ((def->allocator.alloc == nullptr) != (def->allocator.free == nullptr))
    {
        return mnav_settingAllocator;
    }
    if (!isfinite(def->origin.x) || !isfinite(def->origin.y) || !isfinite(def->origin.z))
    {
        return mnav_settingOrigin;
    }
    if (!InRange(def->cellSize, MNAV_MIN_CELL_SIZE, MNAV_MAX_CELL_SIZE))
    {
        return mnav_settingCellSize;
    }
    if (!InRange(def->cellHeight, MNAV_MIN_CELL_SIZE, MNAV_MAX_CELL_SIZE))
    {
        return mnav_settingCellHeight;
    }
    if (def->tileCells < MNAV_MIN_TILE_CELLS || def->tileCells > MNAV_MAX_TILE_CELLS)
    {
        return mnav_settingTileCells;
    }
    if (def->tier > mnav_tierDynamic)
    {
        return mnav_settingTier;
    }
    return mnav_settingNone;
}

// Converts the agent; the grid settings are already valid.
static mnavBakeSetting ConvertAgent(const mnavBakeDef* def, mnavBakeCells* cells)
{
    const mnavAgentProfile* agent = &def->agent;
    if (!InRange(agent->radius, 0.0f, INFINITY) ||
        !mnavToCells(agent->radius, def->cellSize, true, def->tileCells, &cells->agentRadius) ||
        cells->agentRadius + BORDER_MARGIN_CELLS > def->tileCells)
    {
        return mnav_settingAgentRadius;
    }
    if (!InRange(agent->height, 0.0f, INFINITY) || agent->height == 0.0f ||
        !mnavToCells(agent->height, def->cellHeight, true, MNAV_MAX_HEIGHT_CELLS,
                     &cells->agentHeight))
    {
        return mnav_settingAgentHeight;
    }
    if (!InRange(agent->stepHeight, 0.0f, INFINITY) ||
        !mnavToCells(agent->stepHeight, def->cellHeight, false, MNAV_MAX_HEIGHT_CELLS,
                     &cells->agentStep))
    {
        return mnav_settingAgentStepHeight;
    }
    if (!(agent->maxSlopeDegrees >= 0.0f && agent->maxSlopeDegrees < 90.0f))
    {
        return mnav_settingAgentMaxSlope;
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
    if (!InRange(def->minRegionArea, 0.0f, INFINITY) ||
        !mnavToCells(def->minRegionArea, cellArea, true, 1 << 30, &cells->minRegion))
    {
        return mnav_settingMinRegionArea;
    }
    if (!InRange(def->maxEdgeError, 0.0f, MNAV_MAX_CELL_SIZE * MNAV_MAX_TILE_CELLS))
    {
        return mnav_settingMaxEdgeError;
    }
    cells->edgeError = def->maxEdgeError / def->cellSize;
    if (!InRange(def->maxEdgeLength, 0.0f, INFINITY) ||
        !mnavToCells(def->maxEdgeLength, def->cellSize, false, 1 << 30, &cells->edgeLength))
    {
        return mnav_settingMaxEdgeLength;
    }
    // Sixteenths of a cell and of a cell height are exact: the division
    // only lowers the exponent.
    if (!InRange(def->detailSampleDistance, 0.0f, INFINITY) ||
        !mnavToCells(def->detailSampleDistance, def->cellSize / 16.0f, false, 1 << 30,
                     &cells->detailSample))
    {
        return mnav_settingDetailSampleDistance;
    }
    // Heights come one per cell; sampling closer than a cell adds nothing.
    if (cells->detailSample > 0 && cells->detailSample < 16)
    {
        cells->detailSample = 16;
    }
    if (!InRange(def->detailMaxError, 0.0f, INFINITY) ||
        !mnavToCells(def->detailMaxError, def->cellHeight / 16.0f, false, 1 << 30,
                     &cells->detailError))
    {
        return mnav_settingDetailMaxError;
    }
    cells->border = cells->agentRadius + BORDER_MARGIN_CELLS;
    cells->cosMaxSlope = mnavCosDegrees(agent->maxSlopeDegrees);
    cells->tileSize = (float)def->tileCells * def->cellSize;
    return mnav_settingNone;
}

mnavBakeDefResult mnavCheckBakeDef(const mnavBakeDef* def, mnavBakeCells* cells)
{
    if (def == nullptr)
    {
        return Refuse(mnav_settingNone);
    }
    if (def->cookie != MNAV_BAKE_DEF_COOKIE)
    {
        return Refuse(mnav_settingCookie);
    }
    if (def->version != MNAV_ABI_VERSION)
    {
        return (mnavBakeDefResult){mnav_errorVersion, mnav_settingNone};
    }
    mnavBakeSetting setting = CheckGrid(def);
    if (setting != mnav_settingNone)
    {
        return Refuse(setting);
    }
    mnavBakeCells converted = {0};
    setting = ConvertAgent(def, &converted);
    if (setting == mnav_settingNone)
    {
        setting = CheckLimits(&def->limits);
    }
    if (setting != mnav_settingNone)
    {
        return Refuse(setting);
    }
    if (cells != nullptr)
    {
        *cells = converted;
    }
    return (mnavBakeDefResult){mnav_success, mnav_settingNone};
}
