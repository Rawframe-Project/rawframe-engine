// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Exact tests on contour vertices on the ground plane.

#include "planar.h"

#include "contour.h"

#include <stdint.h>

int64_t mnavArea2(const mnavContourVertex* a, const mnavContourVertex* b,
                  const mnavContourVertex* c)
{
    return (int64_t)(b->x - a->x) * (c->z - a->z) - (int64_t)(c->x - a->x) * (b->z - a->z);
}

bool mnavLeft(const mnavContourVertex* a, const mnavContourVertex* b, const mnavContourVertex* c)
{
    return mnavArea2(a, b, c) < 0;
}

bool mnavLeftOn(const mnavContourVertex* a, const mnavContourVertex* b, const mnavContourVertex* c)
{
    return mnavArea2(a, b, c) <= 0;
}

bool mnavSameGround(const mnavContourVertex* a, const mnavContourVertex* b)
{
    return a->x == b->x && a->z == b->z;
}

bool mnavIntersectProper(const mnavContourVertex* a, const mnavContourVertex* b,
                         const mnavContourVertex* c, const mnavContourVertex* d)
{
    if (mnavArea2(a, b, c) == 0 || mnavArea2(a, b, d) == 0 || mnavArea2(c, d, a) == 0 ||
        mnavArea2(c, d, b) == 0)
    {
        return false;
    }
    return (mnavLeft(a, b, c) != mnavLeft(a, b, d)) && (mnavLeft(c, d, a) != mnavLeft(c, d, b));
}

// Whether c lies on the closed segment ab.
static bool Between(const mnavContourVertex* a, const mnavContourVertex* b,
                    const mnavContourVertex* c)
{
    if (mnavArea2(a, b, c) != 0)
    {
        return false;
    }
    if (a->x != b->x)
    {
        return (a->x <= c->x && c->x <= b->x) || (a->x >= c->x && c->x >= b->x);
    }
    return (a->z <= c->z && c->z <= b->z) || (a->z >= c->z && c->z >= b->z);
}

bool mnavIntersect(const mnavContourVertex* a, const mnavContourVertex* b,
                   const mnavContourVertex* c, const mnavContourVertex* d)
{
    return mnavIntersectProper(a, b, c, d) || Between(a, b, c) || Between(a, b, d) ||
           Between(c, d, a) || Between(c, d, b);
}

bool mnavInCone(const mnavContourVertex* prev, const mnavContourVertex* at,
                const mnavContourVertex* next, const mnavContourVertex* p, bool loose)
{
    if (mnavLeftOn(prev, at, next))
    {
        if (loose)
        {
            return mnavLeftOn(at, p, prev) && mnavLeftOn(p, at, next);
        }
        return mnavLeft(at, p, prev) && mnavLeft(p, at, next);
    }
    return !(mnavLeftOn(at, p, next) && mnavLeftOn(p, at, prev));
}
