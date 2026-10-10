// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Debug drawing for flight volumes (mnav-0015): the open blocks in a
// box, each a wire box drawn once, and a flight path's steps.

#include "draw.h"
#include "flight_frame.h"
#include "flight_space.h"

#include "maul-nav/base.h"
#include "maul-nav/draw.h"
#include "maul-nav/flight.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

static bool Finite(mnavPos3 p)
{
    return isfinite(p.x) && isfinite(p.y) && isfinite(p.z);
}

// Draws a block's twelve edges.
static void DrawBlock(mnavDebugBuffer* buffer, const mnavFlightFrame* frame,
                      const mnavFlightBlock* b)
{
    mnavPos3 corner[8];
    for (int32_t c = 0; c < 8; ++c)
    {
        const double v[3] = {(double)b->x + ((c & 1) != 0 ? b->size : 0),
                             (double)b->y + ((c & 2) != 0 ? b->size : 0),
                             (double)b->z + ((c & 4) != 0 ? b->size : 0)};
        corner[c] = mnavFlightToWorld(frame, v);
    }
    // Corners differing in one bit share an edge.
    uint16_t value = b->size < 65535 ? (uint16_t)b->size : (uint16_t)65535;
    for (int32_t c = 0; c < 8; ++c)
    {
        for (int32_t bit = 1; bit < 8; bit *= 2)
        {
            if ((c & bit) == 0)
            {
                mnavDrawLine(buffer, corner[c], corner[c | bit], mnav_debugFlightBlock, value);
            }
        }
    }
}

static int32_t Larger(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

mnavResult mnavDebugFlight(const mnavFlightVolume* volume, mnavPos3 low, mnavPos3 high,
                           mnavDebugBuffer* buffer)
{
    if (volume == nullptr || !mnavGoodBuffer(buffer) || !Finite(low) || !Finite(high) ||
        low.x > high.x || low.y > high.y || low.z > high.z)
    {
        return mnav_errorInvalid;
    }
    mnavFlightFrame frame = mnavMakeFlightFrame(volume);
    double v[3];
    int32_t lo[3];
    int32_t hi[3];
    if (!mnavFlightToVoxels(&frame, low, v, lo) || !mnavFlightToVoxels(&frame, high, v, hi))
    {
        return mnav_errorRange;
    }
    // Below the floor and above the ceiling nothing is open.
    lo[1] = Larger(lo[1], 0);
    hi[1] = hi[1] < frame.layers - 1 ? hi[1] : frame.layers - 1;
    double voxels = ((double)hi[0] - lo[0] + 1.0) * ((double)hi[1] - lo[1] + 1.0) *
                    ((double)hi[2] - lo[2] + 1.0);
    if (lo[1] <= hi[1] && voxels > (double)MNAV_MAX_FLIGHT_DEBUG_VOXELS)
    {
        return mnav_errorRange;
    }
    mnavFlightCursor cursor = mnavMakeFlightCursor(volume);
    for (int32_t z = lo[2]; z <= hi[2]; ++z)
    {
        for (int32_t y = lo[1]; y <= hi[1]; ++y)
        {
            for (int32_t x = lo[0]; x <= hi[0];)
            {
                mnavFlightBlock b;
                if (mnavFlightHolder(&cursor, x, y, z, &b) != MNAV_SPACE_OPEN)
                {
                    x += 1;
                    continue;
                }
                // A block is drawn from the first of its voxels in the box.
                if (y == Larger(b.y, lo[1]) && z == Larger(b.z, lo[2]))
                {
                    DrawBlock(buffer, &frame, &b);
                }
                x = b.x + b.size;
            }
        }
    }
    return mnavDrawResult(buffer);
}

mnavResult mnavDebugFlightPath(const mnavFlightPath* path, mnavDebugBuffer* buffer)
{
    if (path == nullptr || !mnavGoodBuffer(buffer) || path->pointCount < 0 ||
        (path->pointCount > 0 && path->points == nullptr))
    {
        return mnav_errorInvalid;
    }
    for (int32_t i = 1; i < path->pointCount; ++i)
    {
        mnavDrawLine(buffer, path->points[i - 1], path->points[i], mnav_debugPath, 0);
    }
    return mnavDrawResult(buffer);
}
