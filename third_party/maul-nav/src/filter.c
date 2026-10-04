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

static const int32_t s_offsetX[4] = {-1, 0, 1, 0};
static const int32_t s_offsetZ[4] = {0, 1, 0, -1};

void mnavFilterLowObstacles(mnavHeightfield* heightfield, int32_t step)
{
    int32_t columnCount = heightfield->frame.width * heightfield->frame.width;
    for (int32_t c = 0; c < columnCount; ++c)
    {
        bool belowWalkable = false;
        mnavAreaType belowArea = mnav_areaNone;
        int32_t belowTop = 0;
        for (uint32_t i = heightfield->columns[c]; i < heightfield->columns[c + 1]; ++i)
        {
            mnavSpan* span = &heightfield->spans[i];
            // The test reads the walkability the span had before this
            // filter, so a stack of obstacles does not climb by induction.
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

// Widens the range of floors within a step that one neighbor column
// offers a span with floor and ceiling. Returns true when the neighbor
// drops more than a step into space the agent fits through.
static bool ScanNeighbor(const mnavHeightfield* heightfield, int32_t column, int32_t floor,
                         int32_t ceiling, int32_t height, int32_t step, int32_t* reachLow,
                         int32_t* reachHigh)
{
    uint32_t first = heightfield->columns[column];
    uint32_t end = heightfield->columns[column + 1];
    // The space under the neighbor's lowest span reaches down without end.
    int32_t underCeiling = first < end ? (int32_t)heightfield->spans[first].bottom : OPEN_TOP;
    if (Min(ceiling, underCeiling) - floor >= height)
    {
        return true;
    }
    for (uint32_t i = first; i < end; ++i)
    {
        int32_t neighborFloor = heightfield->spans[i].top;
        int32_t neighborCeiling = Ceiling(heightfield, i, end);
        if (Min(ceiling, neighborCeiling) - Max(floor, neighborFloor) < height)
        {
            continue;
        }
        int32_t difference = neighborFloor - floor;
        if (difference >= -step && difference <= step)
        {
            *reachLow = Min(*reachLow, neighborFloor);
            *reachHigh = Max(*reachHigh, neighborFloor);
        }
        else if (difference < -step)
        {
            return true;
        }
    }
    return false;
}

static bool IsLedge(const mnavHeightfield* heightfield, int32_t x, int32_t z, uint32_t i,
                    uint32_t end, int32_t height, int32_t step)
{
    int32_t width = heightfield->frame.width;
    int32_t floor = heightfield->spans[i].top;
    int32_t ceiling = Ceiling(heightfield, i, end);
    int32_t reachLow = floor;
    int32_t reachHigh = floor;
    for (int32_t direction = 0; direction < 4; ++direction)
    {
        int32_t nx = x + s_offsetX[direction];
        int32_t nz = z + s_offsetZ[direction];
        if (nx < 0 || nz < 0 || nx >= width || nz >= width)
        {
            return true;
        }
        if (ScanNeighbor(heightfield, nx + nz * width, floor, ceiling, height, step, &reachLow,
                         &reachHigh))
        {
            return true;
        }
    }
    return reachHigh - reachLow > step;
}

void mnavFilterLedges(mnavHeightfield* heightfield, int32_t height, int32_t step)
{
    // Only areas change here, and the scan reads only heights, so the
    // order of the scan cannot reach the result.
    int32_t width = heightfield->frame.width;
    for (int32_t z = 0; z < width; ++z)
    {
        for (int32_t x = 0; x < width; ++x)
        {
            uint32_t end = heightfield->columns[x + z * width + 1];
            for (uint32_t i = heightfield->columns[x + z * width]; i < end; ++i)
            {
                if (heightfield->spans[i].area != mnav_areaNone &&
                    IsLedge(heightfield, x, z, i, end, height, step))
                {
                    heightfield->spans[i].area = mnav_areaNone;
                }
            }
        }
    }
}

void mnavFilterLowHeight(mnavHeightfield* heightfield, int32_t height)
{
    int32_t columnCount = heightfield->frame.width * heightfield->frame.width;
    for (int32_t c = 0; c < columnCount; ++c)
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
}

void mnavFilterWalkable(mnavHeightfield* heightfield, int32_t height, int32_t step)
{
    mnavFilterLowObstacles(heightfield, step);
    mnavFilterLedges(heightfield, height, step);
    mnavFilterLowHeight(heightfield, height);
}
