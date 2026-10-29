// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Clipping a polygon on the ground to a circle.

#include "clip.h"

#include "polymesh.h"

#include "maul-nav/base.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// The corners of a 32-gon inscribed in the unit circle, counterclockwise
// from +X toward +Z, written out so that every platform has the same.
static const double s_ring[32][2] = {
    {1.0, 0.0},
    {0.9807852804032304, 0.19509032201612825},
    {0.9238795325112867, 0.3826834323650898},
    {0.8314696123025452, 0.5555702330196022},
    {0.7071067811865476, 0.7071067811865475},
    {0.5555702330196023, 0.8314696123025452},
    {0.38268343236508984, 0.9238795325112867},
    {0.19509032201612833, 0.9807852804032304},
    {0.0, 1.0},
    {-0.1950903220161282, 0.9807852804032304},
    {-0.3826834323650897, 0.9238795325112867},
    {-0.555570233019602, 0.8314696123025455},
    {-0.7071067811865475, 0.7071067811865476},
    {-0.8314696123025453, 0.5555702330196022},
    {-0.9238795325112867, 0.3826834323650899},
    {-0.9807852804032304, 0.1950903220161286},
    {-1.0, 0.0},
    {-0.9807852804032304, -0.19509032201612836},
    {-0.9238795325112868, -0.38268343236508967},
    {-0.8314696123025455, -0.555570233019602},
    {-0.7071067811865477, -0.7071067811865475},
    {-0.5555702330196022, -0.8314696123025452},
    {-0.38268343236509034, -0.9238795325112865},
    {-0.19509032201612866, -0.9807852804032303},
    {0.0, -1.0},
    {0.1950903220161283, -0.9807852804032304},
    {0.38268343236509, -0.9238795325112866},
    {0.5555702330196018, -0.8314696123025455},
    {0.7071067811865474, -0.7071067811865477},
    {0.8314696123025452, -0.5555702330196022},
    {0.9238795325112865, -0.3826834323650904},
    {0.9807852804032303, -0.19509032201612872},
};

// Twice the ground area of triangle abc, positive counterclockwise seen
// from above with X right and Z down the page.
static double Twice(mnavPos3 a, mnavPos3 b, mnavPos3 c)
{
    return (b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z);
}

// Keeps the part of the n corners in a on the kept side of the line from p
// to q, with sign the polygon's turn, writing it to b. Returns the corners
// kept, or -1 when they would pass room.
static int32_t Cut(const mnavPos3* a, int32_t n, mnavPos3 p, mnavPos3 q, double sign, mnavPos3* b,
                   int32_t room)
{
    int32_t m = 0;
    for (int32_t k = 0; k < n; ++k)
    {
        mnavPos3 s = a[k];
        mnavPos3 t = a[(k + 1) % n];
        double ds = Twice(p, q, s) * sign;
        double dt = Twice(p, q, t) * sign;
        bool keep = ds >= 0.0;
        bool cross = (ds >= 0.0) != (dt >= 0.0);
        if (m + (keep ? 1 : 0) + (cross ? 1 : 0) > room)
        {
            return -1;
        }
        if (keep)
        {
            b[m++] = s;
        }
        if (cross)
        {
            double u = ds / (ds - dt);
            b[m++] =
                (mnavPos3){s.x + u * (t.x - s.x), s.y + u * (t.y - s.y), s.z + u * (t.z - s.z)};
        }
    }
    return m;
}

int32_t mnavClipToCircle(const mnavPos3* corners, int32_t count, mnavPos3 center, double radius,
                         mnavPos3* out, int32_t capacity)
{
    int32_t room = capacity < MNAV_CLIP_CORNERS ? capacity : MNAV_CLIP_CORNERS;
    if (count > room)
    {
        return -1;
    }
    mnavPos3 a[MNAV_CLIP_CORNERS];
    mnavPos3 b[MNAV_CLIP_CORNERS];
    memcpy(a, corners, (size_t)count * sizeof(mnavPos3));
    int32_t n = count;
    // The polygons' corners wind one way on the ground; the 32-gon's
    // edges are kept with the same turn.
    double turn = 0.0;
    for (int32_t k = 1; k + 1 < count; ++k)
    {
        turn += Twice(corners[0], corners[k], corners[k + 1]);
    }
    double sign = turn < 0.0 ? -1.0 : 1.0;
    for (int32_t e = 0; e < 32 && n > 0; ++e)
    {
        mnavPos3 p = {center.x + radius * s_ring[e][0], 0.0, center.z + radius * s_ring[e][1]};
        mnavPos3 q = {center.x + radius * s_ring[(e + 1) % 32][0], 0.0,
                      center.z + radius * s_ring[(e + 1) % 32][1]};
        // A circle too small for its corners to differ at the center's
        // place, as one of no radius is, holds no area: an edge of one
        // point would keep every corner.
        if (p.x == q.x && p.z == q.z)
        {
            return 0;
        }
        // The 32-gon turns counterclockwise in X and Z; flipped to the
        // polygon's way when it turns the other.
        int32_t m = sign < 0.0 ? Cut(a, n, q, p, sign, b, room) : Cut(a, n, p, q, sign, b, room);
        if (m < 0)
        {
            return -1;
        }
        memcpy(a, b, (size_t)m * sizeof(mnavPos3));
        n = m;
    }
    memcpy(out, a, (size_t)n * sizeof(mnavPos3));
    return n;
}
