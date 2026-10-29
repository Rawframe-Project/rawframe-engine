// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The walkable filters.

#include "filter.h"

#include "heightfield.h"

#include "maul-nav/bake.h"

#include <stdint.h>

// The top of the open space above a column's highest span.
#define OPEN_TOP 65535

static void LowObstaclesColumn(mnavHeightfield* heightfield, int32_t c, int32_t step)
{
    bool belowWalkable = false;
    mnavAreaType belowArea = mnav_areaNone;
    int32_t belowTop = 0;
    for (uint32_t i = heightfield->columns[c]; i < heightfield->columns[c + 1]; ++i)
    {
        mnavSpan* span = &heightfield->spans[i];
        // The test reads the walkability the span had before this filter,
        // so a stack of obstacles does not climb by induction.
        bool walkable = span->area != mnav_areaNone;
        if (!walkable && belowWalkable && (int32_t)span->top - belowTop <= step)
        {
            span->area = belowArea;
        }
        belowWalkable = walkable;
        belowArea = span->area;
        belowTop = span->top;
    }
}

// Gives a non-walkable span the area of the walkable span directly below
// it when its top is at most step cells higher.
static void LowObstacles(mnavHeightfield* heightfield, int32_t step)
{
    int32_t columnCount = heightfield->frame.width * heightfield->frame.width;
    for (int32_t c = 0; c < columnCount; ++c)
    {
        LowObstaclesColumn(heightfield, c, step);
    }
}

// The free space above span i of a column ending at end.
static int32_t Ceiling(const mnavHeightfield* heightfield, uint32_t i, uint32_t end)
{
    return i + 1 < end ? (int32_t)heightfield->spans[i + 1].bottom : OPEN_TOP;
}

static int32_t Min(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

static int32_t Max(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

// One neighbor column, n, of a span with floor and ceiling: true when it
// drops more than a step into space the agent fits through; else widens
// low to high with its floors within a step that the agent fits on.
static bool Drops(const mnavSpan* spans, const uint32_t* columns, int32_t n, int32_t floor,
                  int32_t ceiling, int32_t height, int32_t step, int32_t* low, int32_t* high)
{
    uint32_t first = columns[n];
    uint32_t last = columns[n + 1];
    // The space under the neighbor's lowest span reaches down without end.
    int32_t under = first < last ? (int32_t)spans[first].bottom : OPEN_TOP;
    if (Min(ceiling, under) - floor >= height)
    {
        return true;
    }
    for (uint32_t k = first; k < last; ++k)
    {
        int32_t neighborFloor = spans[k].top;
        int32_t neighborCeiling = k + 1 < last ? (int32_t)spans[k + 1].bottom : OPEN_TOP;
        if (Min(ceiling, neighborCeiling) - Max(floor, neighborFloor) < height)
        {
            continue;
        }
        int32_t difference = neighborFloor - floor;
        if (difference < -step)
        {
            return true;
        }
        if (difference <= step)
        {
            *low = Min(*low, neighborFloor);
            *high = Max(*high, neighborFloor);
        }
    }
    return false;
}

// Whether walkable span i of column c, ending its column at end, is a
// ledge: a neighbor column drops more than a step into space the agent
// fits through, or the floors within a step it can reach on the four
// neighbors differ by more than a step. Any neighbor that drops is enough,
// and only when none does are the floors in reach compared, so the order
// of the neighbors cannot change the answer. Column c is not on the
// tile's edge.
static bool IsLedge(const mnavHeightfield* heightfield, int32_t c, uint32_t i, uint32_t end,
                    int32_t height, int32_t step)
{
    const mnavSpan* spans = heightfield->spans;
    const uint32_t* columns = heightfield->columns;
    int32_t width = heightfield->frame.width;
    int32_t floor = spans[i].top;
    int32_t ceiling = Ceiling(heightfield, i, end);
    int32_t low = floor;
    int32_t high = floor;
    return Drops(spans, columns, c - 1, floor, ceiling, height, step, &low, &high) ||
           Drops(spans, columns, c + width, floor, ceiling, height, step, &low, &high) ||
           Drops(spans, columns, c + 1, floor, ceiling, height, step, &low, &high) ||
           Drops(spans, columns, c - width, floor, ceiling, height, step, &low, &high) ||
           high - low > step;
}

// Drops for a span alone in its column, whose ceiling is open, and a
// neighbor column holding the one span n, without branches.
static int32_t DropsAlone(const mnavSpan* n, int32_t floor, int32_t height, int32_t step,
                          int32_t* low, int32_t* high)
{
    int32_t top = n->top;
    int32_t difference = top - floor;
    bool fits = OPEN_TOP - Max(floor, top) >= height;
    bool within = fits && difference >= -step && difference <= step;
    *low = within ? Min(*low, top) : *low;
    *high = within ? Max(*high, top) : *high;
    return (int32_t)((int32_t)n->bottom - floor >= height) | (int32_t)(fits && difference < -step);
}

// Whether every one of the columns c and its four neighbors holds one
// span.
static bool AllAlone(const uint32_t* columns, int32_t c, int32_t width)
{
    return columns[c + 1] == columns[c] + 1 && columns[c] == columns[c - 1] + 1 &&
           columns[c + 2] == columns[c + 1] + 1 &&
           columns[c + width + 1] == columns[c + width] + 1 &&
           columns[c - width + 1] == columns[c - width] + 1;
}

// Clears the walkable spans of column c that are ledges; edge columns
// have a neighbor out of the field, so every span of theirs is one.
static void FilterColumn(mnavHeightfield* heightfield, int32_t c, bool edge, int32_t height,
                         int32_t step)
{
    uint32_t end = heightfield->columns[c + 1];
    for (uint32_t i = heightfield->columns[c]; i < end; ++i)
    {
        if (heightfield->spans[i].area != mnav_areaNone &&
            (edge || IsLedge(heightfield, c, i, end, height, step)))
        {
            heightfield->spans[i].area = mnav_areaNone;
        }
    }
}

static void LowHeightColumn(mnavHeightfield* heightfield, int32_t c, int32_t height)
{
    uint32_t end = heightfield->columns[c + 1];
    for (uint32_t i = heightfield->columns[c]; i < end; ++i)
    {
        if (Ceiling(heightfield, i, end) - (int32_t)heightfield->spans[i].top < height)
        {
            heightfield->spans[i].area = mnav_areaNone;
        }
    }
}

// The ledge filter on column c, and the low height filter after it: both
// read only heights and clear areas, so the column's result is the one
// the two filters give one after the other over the whole field. Most
// columns, and their neighbors, hold one span, and those take DropsAlone,
// which answers as IsLedge does; that span's ceiling is open.
static void LedgesColumn(mnavHeightfield* heightfield, int32_t c, bool edge, int32_t height,
                         int32_t step)
{
    int32_t width = heightfield->frame.width;
    const uint32_t* columns = heightfield->columns;
    mnavSpan* spans = heightfield->spans;
    if (edge || !AllAlone(columns, c, width))
    {
        FilterColumn(heightfield, c, edge, height, step);
        LowHeightColumn(heightfield, c, height);
        return;
    }
    mnavSpan* span = &spans[columns[c]];
    if (span->area == mnav_areaNone)
    {
        return;
    }
    if (OPEN_TOP - (int32_t)span->top < height)
    {
        span->area = mnav_areaNone;
        return;
    }
    int32_t floor = span->top;
    int32_t low = floor;
    int32_t high = floor;
    int32_t drops = DropsAlone(&spans[columns[c - 1]], floor, height, step, &low, &high);
    drops |= DropsAlone(&spans[columns[c + width]], floor, height, step, &low, &high);
    drops |= DropsAlone(&spans[columns[c + 1]], floor, height, step, &low, &high);
    drops |= DropsAlone(&spans[columns[c - width]], floor, height, step, &low, &high);
    if (drops != 0 || high - low > step)
    {
        span->area = mnav_areaNone;
    }
}

void mnavFilterWalkable(mnavHeightfield* heightfield, int32_t height, int32_t step)
{
    LowObstacles(heightfield, step);
    int32_t width = heightfield->frame.width;
    for (int32_t z = 0; z < width; ++z)
    {
        bool edgeRow = z == 0 || z == width - 1;
        for (int32_t x = 0; x < width; ++x)
        {
            LedgesColumn(heightfield, x + z * width, edgeRow || x == 0 || x == width - 1, height,
                         step);
        }
    }
}
