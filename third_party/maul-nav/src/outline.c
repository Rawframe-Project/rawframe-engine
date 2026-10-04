// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// 2D outlines filled into the fragments of one tile (mnav-0002). Each row
// of cells is crossed at its centers' line: an edge from a to b crosses it
// when exactly one end lies at or below the line, and a cell is inside
// when its center lies at or after an even crossing and before the next,
// in x. Everything is binary64 from the binary32 input, so the same input
// fills the same cells everywhere.

#include "outline.h"

#include "allocator.h"
#include "raster.h"

#include "maul-nav/bake.h"
#include "maul-nav/base.h"

#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <string.h>

// A flat triangle at height 0 rasterizes to this range of cell heights.
#define FLAT_BOTTOM MNAV_HEIGHT_OFFSET
#define FLAT_TOP    (MNAV_HEIGHT_OFFSET + 1)

static double Center(float min, float cellSize, int32_t i)
{
    return (double)min + ((double)i + 0.5) * (double)cellSize;
}

bool mnavRingTouchesTile(const mnavTileFrame* frame, const mnavVec2* points, int32_t count)
{
    float minX = points[0].x;
    float maxX = minX;
    float minY = points[0].y;
    float maxY = minY;
    for (int32_t i = 1; i < count; ++i)
    {
        mnavVec2 p = points[i];
        minX = p.x < minX ? p.x : minX;
        maxX = p.x > maxX ? p.x : maxX;
        minY = p.y < minY ? p.y : minY;
        maxY = p.y > maxY ? p.y : maxY;
    }
    double size = (double)frame->width * (double)frame->cellSize;
    return (double)maxX >= (double)frame->minX && (double)minX <= (double)frame->minX + size &&
           (double)maxY >= (double)frame->minZ && (double)minY <= (double)frame->minZ + size;
}

bool mnavOutlineTouchesTile(const mnavTileFrame* frame, const mnavOutline* outline)
{
    return mnavRingTouchesTile(frame, outline->points, outline->pointCount);
}

// The crossings of a ring's edges with the line z = at, sorted.
static int32_t Crossings(const mnavVec2* points, int32_t pointCount, double at, double* out)
{
    int32_t count = 0;
    for (int32_t i = 0; i < pointCount; ++i)
    {
        mnavVec2 a = points[i];
        mnavVec2 b = points[(i + 1) % pointCount];
        if (((double)a.y <= at) == ((double)b.y <= at))
        {
            continue;
        }
        double x = (double)a.x +
                   (at - (double)a.y) * ((double)b.x - (double)a.x) / ((double)b.y - (double)a.y);
        int32_t k = count++;
        while (k > 0 && out[k - 1] > x)
        {
            out[k] = out[k - 1];
            k -= 1;
        }
        out[k] = x;
    }
    return count;
}

// The first cell along an axis starting at min whose center lies at or
// after v, or width.
static int32_t FirstAtOrAfter(const mnavTileFrame* frame, float min, double v)
{
    double guess = ceil((v - (double)min) / (double)frame->cellSize - 0.5);
    int32_t i = guess < 0.0 ? 0 : (guess > (double)frame->width ? frame->width : (int32_t)guess);
    while (i > 0 && Center(min, frame->cellSize, i - 1) >= v)
    {
        i -= 1;
    }
    while (i < frame->width && Center(min, frame->cellSize, i) < v)
    {
        i += 1;
    }
    return i;
}

mnavResult mnavVisitRing(const mnavTileFrame* frame, const mnavVec2* points, int32_t pointCount,
                         double* crossings, mnavCellVisit visit, void* context)
{
    float low = points[0].y;
    float high = low;
    for (int32_t i = 1; i < pointCount; ++i)
    {
        low = points[i].y < low ? points[i].y : low;
        high = points[i].y > high ? points[i].y : high;
    }
    // Rows whose centers lie outside the ring's range cross none of its
    // edges.
    int32_t last = FirstAtOrAfter(frame, frame->minZ, (double)high);
    last = last < frame->width ? last : frame->width - 1;
    for (int32_t z = FirstAtOrAfter(frame, frame->minZ, (double)low); z <= last; ++z)
    {
        int32_t count =
            Crossings(points, pointCount, Center(frame->minZ, frame->cellSize, z), crossings);
        for (int32_t k = 0; k + 1 < count; k += 2)
        {
            int32_t first = FirstAtOrAfter(frame, frame->minX, crossings[k]);
            int32_t end = FirstAtOrAfter(frame, frame->minX, crossings[k + 1]);
            for (int32_t x = first; x < end; ++x)
            {
                mnavResult result = visit(context, x, z);
                if (result != mnav_success)
                {
                    return result;
                }
            }
        }
    }
    return mnav_success;
}

// Marks the cells an outline holds, or adds their fragments when not
// blocked.
typedef struct Filler
{
    mnavMemory* memory;
    const mnavTileFrame* frame;
    uint8_t* blocked;
    mnavFragmentList* list;
    double* crossings;
    mnavAreaType area;
} Filler;

static mnavResult FillCell(void* context, int32_t x, int32_t z)
{
    Filler* f = context;
    size_t cell = (size_t)z * (size_t)f->frame->width + (size_t)x;
    if (f->area == mnav_areaNone)
    {
        f->blocked[cell] = 1;
        return mnav_success;
    }
    if (f->blocked[cell] != 0)
    {
        return mnav_success;
    }
    return mnavAddFragment(
        f->memory, f->list,
        (mnavFragment){(uint16_t)x, (uint16_t)z, FLAT_BOTTOM, FLAT_TOP, f->area});
}

static mnavResult Fill(Filler* f, const mnavOutline* outline)
{
    f->area = outline->area;
    return mnavVisitRing(f->frame, outline->points, outline->pointCount, f->crossings, FillCell, f);
}

// Fills the outlines of one kind, obstructions or walkable ones, that
// touch the tile.
static mnavResult FillAll(Filler* f, const mnavOutline* outlines, int32_t count, bool obstructions)
{
    for (int32_t o = 0; o < count; ++o)
    {
        const mnavOutline* outline = &outlines[o];
        if ((outline->area == mnav_areaNone) != obstructions ||
            !mnavOutlineTouchesTile(f->frame, outline))
        {
            continue;
        }
        mnavResult result = Fill(f, outline);
        if (result != mnav_success)
        {
            return result;
        }
    }
    return mnav_success;
}

mnavResult mnavCollectOutlines(mnavMemory* memory, const mnavBakeDef* def,
                               const mnavTileFrame* frame, const mnavOutline* outlines,
                               int32_t outlineCount, mnavFragmentList* list)
{
    int32_t touching = 0;
    int32_t points = 0;
    for (int32_t o = 0; o < outlineCount; ++o)
    {
        if (mnavOutlineTouchesTile(frame, &outlines[o]))
        {
            touching += 1;
            points = outlines[o].pointCount > points ? outlines[o].pointCount : points;
        }
    }
    if (touching > def->limits.tileTriangles)
    {
        return mnav_errorLimit;
    }
    if (touching == 0)
    {
        return mnav_success;
    }
    size_t cells = (size_t)frame->width * (size_t)frame->width;
    Filler f = {memory, frame, nullptr, list, nullptr, mnav_areaNone};
    mnavResult result =
        mnavAllocate(memory, cells, sizeof(uint8_t), alignof(uint8_t), (void**)&f.blocked);
    if (result == mnav_success)
    {
        memset(f.blocked, 0, cells);
        result = mnavAllocate(memory, (size_t)points, sizeof(double), alignof(double),
                              (void**)&f.crossings);
    }
    if (result == mnav_success)
    {
        result = FillAll(&f, outlines, outlineCount, true);
    }
    if (result == mnav_success)
    {
        result = FillAll(&f, outlines, outlineCount, false);
    }
    mnavRelease(memory, f.crossings, (size_t)points, sizeof(double), alignof(double));
    mnavRelease(memory, f.blocked, cells, sizeof(uint8_t), alignof(uint8_t));
    return result;
}
