// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Terrains (mnav-0003). A cell between samples (c, r), (c + 1, r),
// (c + 1, r + 1) and (c, r + 1) is two triangles split along the diagonal
// from (c, r) to (c + 1, r + 1), wound so that they face up; a sample's
// ground place is computed in binary64 and rounded once.

#include "terrain.h"

#include "raster.h"

#include "maul-nav/bake.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

static mnavInputResult Refuse(mnavResult result, mnavInputElement element, int32_t index)
{
    return (mnavInputResult){result, element, index};
}

// A sample's place.
static mnavVec3 Sample(const mnavTerrain* t, int32_t column, int32_t row)
{
    return (mnavVec3){
        (float)((double)t->origin.x + (double)column * (double)t->spacingX),
        (float)((double)t->origin.y +
                (double)t->heights[(size_t)row * (size_t)t->columns + (size_t)column]),
        (float)((double)t->origin.z + (double)row * (double)t->spacingZ)};
}

static bool GoodShape(const mnavTerrain* t)
{
    return t->columns >= 2 && t->columns <= MNAV_MAX_TERRAIN_SIDE && t->rows >= 2 &&
           t->rows <= MNAV_MAX_TERRAIN_SIDE && t->heights != nullptr && isfinite(t->spacingX) &&
           isfinite(t->spacingZ) && t->spacingX > 0.0f && t->spacingZ > 0.0f &&
           isfinite(t->origin.x) && isfinite(t->origin.y) && isfinite(t->origin.z);
}

// Whether a place lies within the extent input may have.
static bool InExtent(const mnavBakeDef* def, mnavVec3 v)
{
    float ground = def->cellSize * (float)MNAV_MAX_EXTENT_CELLS;
    float vertical = def->cellHeight * (float)MNAV_MAX_HEIGHT_CELLS;
    return fabsf(v.x) <= ground && fabsf(v.z) <= ground && fabsf(v.y) <= vertical;
}

static mnavInputResult CheckSamples(const mnavBakeDef* def, const mnavTerrain* t)
{
    for (int32_t r = 0; r < t->rows; ++r)
    {
        for (int32_t c = 0; c < t->columns; ++c)
        {
            int32_t i = r * t->columns + c;
            if (!isfinite(t->heights[i]))
            {
                return Refuse(mnav_errorInvalid, mnav_elementSample, i);
            }
            if (!InExtent(def, Sample(t, c, r)))
            {
                return Refuse(mnav_errorRange, mnav_elementSample, i);
            }
        }
    }
    return Refuse(mnav_success, mnav_elementNone, -1);
}

mnavInputResult mnavCheckTerrain(const mnavBakeDef* def, const mnavTerrain* terrain)
{
    if (!GoodShape(terrain))
    {
        return Refuse(mnav_errorInvalid, mnav_elementNone, -1);
    }
    if (mnavTerrainTriangles(terrain) > def->limits.inputTriangles)
    {
        return Refuse(mnav_errorLimit, mnav_elementNone, -1);
    }
    mnavInputResult result = CheckSamples(def, terrain);
    if (result.result != mnav_success || terrain->areas == nullptr)
    {
        return result;
    }
    int32_t cells = (terrain->columns - 1) * (terrain->rows - 1);
    for (int32_t i = 0; i < cells; ++i)
    {
        if (terrain->areas[i] >= MNAV_AREA_TYPES)
        {
            return Refuse(mnav_errorInvalid, mnav_elementCell, i);
        }
    }
    return result;
}

int64_t mnavTerrainTriangles(const mnavTerrain* terrain)
{
    return 2 * (int64_t)(terrain->columns - 1) * (int64_t)(terrain->rows - 1);
}

// The cells along one axis whose span, from the first sample at origin
// with the spacing given, meets low to high; false when none do.
static bool Span(double origin, double spacing, int32_t samples, double low, double high,
                 int32_t* first, int32_t* last)
{
    double a = floor((low - origin) / spacing) - 1.0;
    double b = floor((high - origin) / spacing) + 1.0;
    double cells = (double)samples - 2.0;
    a = a < 0.0 ? 0.0 : a;
    b = b > cells ? cells : b;
    if (a > b)
    {
        return false;
    }
    *first = (int32_t)a;
    *last = (int32_t)b;
    return true;
}

bool mnavTerrainCells(const mnavTerrain* terrain, const mnavTileFrame* frame, int32_t* c0,
                      int32_t* c1, int32_t* r0, int32_t* r1)
{
    double side = (double)frame->width * (double)frame->cellSize;
    return Span((double)terrain->origin.x, (double)terrain->spacingX, terrain->columns,
                (double)frame->minX, (double)frame->minX + side, c0, c1) &&
           Span((double)terrain->origin.z, (double)terrain->spacingZ, terrain->rows,
                (double)frame->minZ, (double)frame->minZ + side, r0, r1);
}

bool mnavTerrainTriangle(const mnavTerrain* terrain, int32_t column, int32_t row, int32_t k,
                         mnavVec3 corners[3], mnavAreaType* area)
{
    *area = terrain->areas != nullptr
                ? terrain->areas[(size_t)row * (size_t)(terrain->columns - 1) + (size_t)column]
                : mnav_areaWalkable;
    if (*area == mnav_areaNone)
    {
        return false;
    }
    mnavVec3 a = Sample(terrain, column, row);
    mnavVec3 c = Sample(terrain, column + 1, row + 1);
    corners[0] = a;
    corners[1] = k == 0 ? Sample(terrain, column, row + 1) : c;
    corners[2] = k == 0 ? c : Sample(terrain, column + 1, row);
    return true;
}
