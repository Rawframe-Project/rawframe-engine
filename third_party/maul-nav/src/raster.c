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

// A polygon within one row of cells, in (u, h) alone: the row's cuts are
// across u, and the cells need only their heights. Each coordinate of a
// cut's point is computed on its own, so leaving w out changes no other.
typedef struct RowPoly
{
    float u[POLY_MAX];
    float h[POLY_MAX];
    int32_t count;
} RowPoly;

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

// floorf and ceilf, without a call into the C library: a float of 2^23 or
// more in magnitude is a whole number already, and below that the
// truncation to an integer is exact and moved one toward the floor or the
// ceiling when it went the other way. Equal to the C library's but for the
// sign of a zero, which the integers and differences taken from them
// cannot tell.
static float Floor(float x)
{
    if (!(x > -8388608.0f && x < 8388608.0f))
    {
        return x;
    }
    float t = (float)(int32_t)x;
    return t > x ? t - 1.0f : t;
}

static float Ceil(float x)
{
    if (!(x > -8388608.0f && x < 8388608.0f))
    {
        return x;
    }
    float t = (float)(int32_t)x;
    return t < x ? t + 1.0f : t;
}

// (int32_t)Floor(x) and (int32_t)Ceil(x), the integer taken straight from
// the truncation.
static int32_t FloorToInt(float x)
{
    if (!(x > -8388608.0f && x < 8388608.0f))
    {
        return (int32_t)x;
    }
    int32_t i = (int32_t)x;
    return (float)i > x ? i - 1 : i;
}

static int32_t CeilToInt(float x)
{
    if (!(x > -8388608.0f && x < 8388608.0f))
    {
        return (int32_t)x;
    }
    int32_t i = (int32_t)x;
    return (float)i < x ? i + 1 : i;
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
    float start = Floor(low);
    float end = high > low ? Ceil(high) - 1.0f : start;
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

// Split for a row's polygon at u = cut, as Split does it.
static void SplitRow(const RowPoly* in, float cut, RowPoly* below, RowPoly* above)
{
    below->count = 0;
    above->count = 0;
    // Each corner's offset from the cut, carried to the next edge.
    float da = in->u[in->count - 1] - cut;
    for (int32_t i = 0, j = in->count - 1; i < in->count; j = i, ++i)
    {
        float db = in->u[i] - cut;
        if ((da < 0.0f && db > 0.0f) || (da > 0.0f && db < 0.0f))
        {
            float t = da / (da - db);
            float h = in->h[j] + (in->h[i] - in->h[j]) * t;
            below->u[below->count] = cut;
            below->h[below->count++] = h;
            above->u[above->count] = cut;
            above->h[above->count++] = h;
        }
        if (db <= 0.0f)
        {
            below->u[below->count] = in->u[i];
            below->h[below->count++] = in->h[i];
        }
        if (db >= 0.0f)
        {
            above->u[above->count] = in->u[i];
            above->h[above->count++] = in->h[i];
        }
        da = db;
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

// Adds the fragment of cell (x, z) whose piece spans heights low to high.
static mnavResult EmitHeights(mnavMemory* memory, float low, float high, int32_t x, int32_t z,
                              mnavAreaType area, mnavFragmentList* list)
{
    // Validated input keeps heights within MNAV_MAX_HEIGHT_CELLS; the
    // clamps only absorb the last rounding of the division.
    int32_t bottom = FloorToInt(low) + MNAV_HEIGHT_OFFSET;
    int32_t top = CeilToInt(high) + MNAV_HEIGHT_OFFSET;
    bottom = bottom < 0 ? 0 : (bottom > UINT16_MAX - 1 ? UINT16_MAX - 1 : bottom);
    top = top <= bottom ? bottom + 1 : (top > UINT16_MAX ? UINT16_MAX : top);
    return mnavAddFragment(
        memory, list,
        (mnavFragment){(uint16_t)x, (uint16_t)z, (uint16_t)bottom, (uint16_t)top, area});
}

static mnavResult Emit(mnavMemory* memory, const RowPoly* cell, int32_t x, int32_t z,
                       mnavAreaType area, mnavFragmentList* list)
{
    // The loops cut only strictly inside a polygon's range, and a cut
    // gives both sides its crossing points, so each piece keeps three
    // corners or more; this and the row's check guard against that ever
    // failing, and no input has reached them.
    if (cell->count < 3)
    {
        return mnav_success;
    }
    float low = cell->h[0];
    float high = cell->h[0];
    for (int32_t i = 1; i < cell->count; ++i)
    {
        low = cell->h[i] < low ? cell->h[i] : low;
        high = cell->h[i] > high ? cell->h[i] : high;
    }
    return EmitHeights(memory, low, high, x, z, area, list);
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

// Whether the cells a triangle's corners span along one ground axis, in
// cells from the tile's corner, meet the tile's: the same values
// rasterizing computes, one axis at a time. Corners all a cell or more
// before the tile, or two past its far side, are out without dividing:
// rounding is monotone, so their cells lie at -1 and below, or at the
// tile's width and beyond. Likewise corners whose nearest lies from the
// tile's corner to a cell short of its far side are in without dividing:
// the first cell is from 0 to width - 1, and the last no lower.
static bool SpansTile(const mnavTileFrame* frame, float a, float b, float c, float min)
{
    float d[3] = {a - min, b - min, c - min};
    float before = -frame->cellSize;
    float past = (float)(frame->width + 2) * frame->cellSize;
    if ((d[0] <= before && d[1] <= before && d[2] <= before) ||
        (d[0] > past && d[1] > past && d[2] > past))
    {
        return false;
    }
    float nearest = d[0] < d[1] ? d[0] : d[1];
    nearest = d[2] < nearest ? d[2] : nearest;
    if (nearest >= 0.0f && nearest < (float)(frame->width - 1) * frame->cellSize)
    {
        return true;
    }
    float u[3] = {d[0] / frame->cellSize, d[1] / frame->cellSize, d[2] / frame->cellSize};
    float low = u[0];
    float high = u[0];
    for (int32_t i = 1; i < 3; ++i)
    {
        low = u[i] < low ? u[i] : low;
        high = u[i] > high ? u[i] : high;
    }
    int32_t first = 0;
    int32_t last = 0;
    CellRange(low, high, &first, &last);
    return last >= 0 && first < frame->width;
}

bool mnavTriangleTouchesTile(const mnavTileFrame* frame, const mnavVec3 corners[3])
{
    return SpansTile(frame, corners[0].x, corners[1].x, corners[2].x, frame->minX) &&
           SpansTile(frame, corners[0].z, corners[1].z, corners[2].z, frame->minZ);
}

// Rasterizes one row of cells, z, of a polygon already clipped to it. The
// part still to cut and the next one swap places rather than copy.
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
    RowPoly buffers[3];
    buffers[1].count = 0;
    buffers[2].count = 0;
    RowPoly* rest = &buffers[0];
    RowPoly* next = &buffers[1];
    RowPoly* cell = &buffers[2];
    for (int32_t i = 0; i < row->count; ++i)
    {
        rest->u[i] = row->v[i][AXIS_U];
        rest->h[i] = row->v[i][1];
    }
    rest->count = row->count;
    if (start > first)
    {
        SplitRow(rest, (float)start, cell, next);
        RowPoly* swap = rest;
        rest = next;
        next = swap;
    }
    for (int32_t x = start; x <= end; ++x)
    {
        const RowPoly* piece = rest;
        if (x < last)
        {
            SplitRow(rest, (float)(x + 1), cell, next);
            RowPoly* swap = rest;
            rest = next;
            next = swap;
            piece = cell;
        }
        mnavResult result = Emit(memory, piece, x, z, area, list);
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
    // The part still to cut and the next one swap places rather than copy.
    Poly buffers[3];
    buffers[1].count = 0;
    buffers[2].count = 0;
    Poly* rest = &buffers[0];
    Poly* next = &buffers[1];
    Poly* row = &buffers[2];
    ToCells(frame, corners, rest);
    float low = 0.0f;
    float high = 0.0f;
    int32_t first = 0;
    int32_t last = 0;
    Bounds(rest, AXIS_W, &low, &high);
    CellRange(low, high, &first, &last);
    if (first == last)
    {
        // One row holds the triangle; when one cell does too, no cut is
        // made, and its fragment spans the corners' heights, or there is
        // none when the cell is off the tile.
        float lowU = 0.0f;
        float highU = 0.0f;
        int32_t firstU = 0;
        int32_t lastU = 0;
        Bounds(rest, AXIS_U, &lowU, &highU);
        CellRange(lowU, highU, &firstU, &lastU);
        if (firstU == lastU)
        {
            if (first < 0 || first >= frame->width || firstU < 0 || firstU >= frame->width)
            {
                return mnav_success;
            }
            Bounds(rest, 1, &low, &high);
            return EmitHeights(memory, low, high, firstU, first, area, list);
        }
    }
    int32_t start = first < 0 ? 0 : first;
    int32_t end = last >= frame->width ? frame->width - 1 : last;
    if (start > first)
    {
        Split(rest, AXIS_W, (float)start, row, next);
        Poly* swap = rest;
        rest = next;
        next = swap;
    }
    for (int32_t z = start; z <= end; ++z)
    {
        const Poly* piece = rest;
        if (z < last)
        {
            Split(rest, AXIS_W, (float)(z + 1), row, next);
            Poly* swap = rest;
            rest = next;
            next = swap;
            piece = row;
        }
        if (piece->count < 3)
        {
            continue;
        }
        mnavResult result = RasterizeRow(memory, frame, piece, z, area, list);
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
