// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Rasterizing triangles into the fragments of one tile.

#include "raster.h"

#include "allocator.h"

#include "maul-nav/bake.h"

#include <math.h>
#include <string.h>

// A clipped polygon in cell units: (u, h, w) per vertex, u and w across
// the tile's cells, h in cell heights. Clipping a triangle by one slab adds
// at most one vertex per side, so 12 is ample.
#define POLY_MAX 12
#define AXIS_U   0
#define AXIS_W   2

typedef struct Poly
{
    float v[POLY_MAX][3];
    int32_t count;
} Poly;

bool mnavMakeTileFrame(const mnavBakeDef* def, const mnavBakeCells* cells, int32_t tileX,
                       int32_t tileZ, mnavTileFrame* frame)
{
    int64_t limit = MNAV_MAX_EXTENT_CELLS + MNAV_MAX_TILE_CELLS;
    int64_t startX = (int64_t)tileX * def->tileCells - cells->border;
    int64_t startZ = (int64_t)tileZ * def->tileCells - cells->border;
    if (startX < -limit || startX > limit || startZ < -limit || startZ > limit)
    {
        return false;
    }
    // Both starts fit in 24 bits, so they convert to binary32 exactly, and
    // each minimum is one rounding.
    frame->width = def->tileCells + 2 * cells->border;
    frame->minX = (float)startX * def->cellSize;
    frame->minZ = (float)startZ * def->cellSize;
    frame->cellSize = def->cellSize;
    frame->cellHeight = def->cellHeight;
    return true;
}

int32_t mnavTriangleArea(const mnavVec3 corners[3], mnavAreaType area, float cosMaxSlope)
{
    float ax = corners[1].x - corners[0].x;
    float ay = corners[1].y - corners[0].y;
    float az = corners[1].z - corners[0].z;
    float bx = corners[2].x - corners[0].x;
    float by = corners[2].y - corners[0].y;
    float bz = corners[2].z - corners[0].z;
    // The normal of a counter-clockwise triangle seen from above points up.
    float nx = ay * bz - az * by;
    float ny = az * bx - ax * bz;
    float nz = ax * by - ay * bx;
    float length = sqrtf(nx * nx + ny * ny + nz * nz);
    if (length == 0.0f)
    {
        return -1;
    }
    if (area == mnav_areaNone || ny < cosMaxSlope * length)
    {
        return mnav_areaNone;
    }
    return area;
}

static void ToCells(const mnavTileFrame* frame, const mnavVec3 corners[3], Poly* poly)
{
    for (int32_t i = 0; i < 3; ++i)
    {
        poly->v[i][AXIS_U] = (corners[i].x - frame->minX) / frame->cellSize;
        poly->v[i][1] = corners[i].y / frame->cellHeight;
        poly->v[i][AXIS_W] = (corners[i].z - frame->minZ) / frame->cellSize;
    }
    poly->count = 3;
}

static void Bounds(const Poly* poly, int32_t axis, float* low, float* high)
{
    *low = poly->v[0][axis];
    *high = poly->v[0][axis];
    for (int32_t i = 1; i < poly->count; ++i)
    {
        float value = poly->v[i][axis];
        *low = value < *low ? value : *low;
        *high = value > *high ? value : *high;
    }
}

// The cells a polygon's extent [low, high] covers along an axis: from
// floor(low) to ceil(high) - 1, or floor(low) alone for no extent.
static void CellRange(float low, float high, int32_t* first, int32_t* last)
{
    float start = floorf(low);
    float end = high > low ? ceilf(high) - 1.0f : start;
    *first = (int32_t)start;
    *last = (int32_t)end;
}

static void Copy(float to[3], const float from[3])
{
    to[0] = from[0];
    to[1] = from[1];
    to[2] = from[2];
}

// Splits a polygon at axis = cut into the part below and the part above.
// A vertex on the cut goes to both.
static void Split(const Poly* in, int32_t axis, float cut, Poly* below, Poly* above)
{
    below->count = 0;
    above->count = 0;
    for (int32_t i = 0, j = in->count - 1; i < in->count; j = i, ++i)
    {
        const float* a = in->v[j];
        const float* b = in->v[i];
        float da = a[axis] - cut;
        float db = b[axis] - cut;
        if ((da < 0.0f && db > 0.0f) || (da > 0.0f && db < 0.0f))
        {
            float t = da / (da - db);
            float point[3];
            for (int32_t k = 0; k < 3; ++k)
            {
                point[k] = a[k] + (b[k] - a[k]) * t;
            }
            point[axis] = cut;
            Copy(below->v[below->count++], point);
            Copy(above->v[above->count++], point);
        }
        if (db <= 0.0f)
        {
            Copy(below->v[below->count++], b);
        }
        if (db >= 0.0f)
        {
            Copy(above->v[above->count++], b);
        }
    }
}

static mnavResult Grow(mnavMemory* memory, mnavFragmentList* list)
{
    if (list->count >= list->limit)
    {
        return mnav_errorLimit;
    }
    int32_t capacity = list->capacity < 1024 ? 1024 : list->capacity * 2;
    capacity = capacity > list->limit ? list->limit : capacity;
    void* items = nullptr;
    mnavResult result =
        mnavAllocate(memory, (size_t)capacity, sizeof(mnavFragment), alignof(mnavFragment), &items);
    if (result != mnav_success)
    {
        return result;
    }
    if (list->count > 0)
    {
        memcpy(items, list->items, (size_t)list->count * sizeof(mnavFragment));
    }
    mnavReleaseFragments(memory, list);
    list->items = items;
    list->capacity = capacity;
    return mnav_success;
}

static mnavResult Emit(mnavMemory* memory, const Poly* cell, int32_t x, int32_t z,
                       mnavAreaType area, mnavFragmentList* list)
{
    if (cell->count < 3)
    {
        return mnav_success;
    }
    float low = 0.0f;
    float high = 0.0f;
    Bounds(cell, 1, &low, &high);
    // Validated input keeps heights within MNAV_MAX_HEIGHT_CELLS; the
    // clamps only absorb the last rounding of the division.
    int32_t bottom = (int32_t)floorf(low) + MNAV_HEIGHT_OFFSET;
    int32_t top = (int32_t)ceilf(high) + MNAV_HEIGHT_OFFSET;
    bottom = bottom < 0 ? 0 : (bottom > UINT16_MAX - 1 ? UINT16_MAX - 1 : bottom);
    top = top <= bottom ? bottom + 1 : (top > UINT16_MAX ? UINT16_MAX : top);
    return mnavAddFragment(
        memory, list,
        (mnavFragment){(uint16_t)x, (uint16_t)z, (uint16_t)bottom, (uint16_t)top, area});
}

mnavResult mnavAddFragment(mnavMemory* memory, mnavFragmentList* list, mnavFragment fragment)
{
    if (list->count == list->capacity)
    {
        mnavResult result = Grow(memory, list);
        if (result != mnav_success)
        {
            return result;
        }
    }
    list->items[list->count++] = fragment;
    return mnav_success;
}

bool mnavTriangleTouchesTile(const mnavTileFrame* frame, const mnavVec3 corners[3])
{
    Poly poly;
    ToCells(frame, corners, &poly);
    float low = 0.0f;
    float high = 0.0f;
    int32_t first = 0;
    int32_t last = 0;
    Bounds(&poly, AXIS_U, &low, &high);
    CellRange(low, high, &first, &last);
    if (last < 0 || first >= frame->width)
    {
        return false;
    }
    Bounds(&poly, AXIS_W, &low, &high);
    CellRange(low, high, &first, &last);
    return last >= 0 && first < frame->width;
}

// Rasterizes one row of cells, z, of a polygon already clipped to it.
static mnavResult RasterizeRow(mnavMemory* memory, const mnavTileFrame* frame, const Poly* row,
                               int32_t z, mnavAreaType area, mnavFragmentList* list)
{
    float low = 0.0f;
    float high = 0.0f;
    int32_t first = 0;
    int32_t last = 0;
    Bounds(row, AXIS_U, &low, &high);
    CellRange(low, high, &first, &last);
    int32_t start = first < 0 ? 0 : first;
    int32_t end = last >= frame->width ? frame->width - 1 : last;
    Poly rest = *row;
    Poly cell;
    Poly next;
    if (start > first)
    {
        Split(row, AXIS_U, (float)start, &cell, &rest);
    }
    for (int32_t x = start; x <= end; ++x)
    {
        if (x < last)
        {
            Split(&rest, AXIS_U, (float)(x + 1), &cell, &next);
            rest = next;
        }
        else
        {
            cell = rest;
        }
        mnavResult result = Emit(memory, &cell, x, z, area, list);
        if (result != mnav_success)
        {
            return result;
        }
    }
    return mnav_success;
}

mnavResult mnavRasterizeTriangle(mnavMemory* memory, const mnavTileFrame* frame,
                                 const mnavVec3 corners[3], mnavAreaType area,
                                 mnavFragmentList* list)
{
    Poly rest;
    ToCells(frame, corners, &rest);
    float low = 0.0f;
    float high = 0.0f;
    int32_t first = 0;
    int32_t last = 0;
    Bounds(&rest, AXIS_W, &low, &high);
    CellRange(low, high, &first, &last);
    int32_t start = first < 0 ? 0 : first;
    int32_t end = last >= frame->width ? frame->width - 1 : last;
    Poly row;
    Poly next;
    if (start > first)
    {
        Split(&rest, AXIS_W, (float)start, &row, &next);
        rest = next;
    }
    for (int32_t z = start; z <= end; ++z)
    {
        if (z < last)
        {
            Split(&rest, AXIS_W, (float)(z + 1), &row, &next);
            rest = next;
        }
        else
        {
            row = rest;
        }
        if (row.count < 3)
        {
            continue;
        }
        mnavResult result = RasterizeRow(memory, frame, &row, z, area, list);
        if (result != mnav_success)
        {
            return result;
        }
    }
    return mnav_success;
}

void mnavReleaseFragments(mnavMemory* memory, mnavFragmentList* list)
{
    mnavRelease(memory, list->items, (size_t)list->capacity, sizeof(mnavFragment),
                alignof(mnavFragment));
    list->items = nullptr;
    list->capacity = 0;
}
