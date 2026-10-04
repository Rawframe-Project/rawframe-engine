// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A small triangulation kept Delaunay by edge flips.

#include "delaunay.h"

#include "invariant.h"

#include <stdint.h>

int64_t mnavDetailArea2(const mnavDetailVertex* a, const mnavDetailVertex* b,
                        const mnavDetailVertex* c)
{
    return (int64_t)(b->x - a->x) * (c->z - a->z) - (int64_t)(c->x - a->x) * (b->z - a->z);
}

// Whether d lies strictly inside the circumcircle of (a, b, c), which is
// wound negatively. Coordinates up to 2^14 keep every term within 2^60.
static bool InCircle(const mnavDetailVertex* a, const mnavDetailVertex* b,
                     const mnavDetailVertex* c, const mnavDetailVertex* d)
{
    int64_t adx = a->x - d->x;
    int64_t adz = a->z - d->z;
    int64_t bdx = b->x - d->x;
    int64_t bdz = b->z - d->z;
    int64_t cdx = c->x - d->x;
    int64_t cdz = c->z - d->z;
    int64_t determinant = (adx * adx + adz * adz) * (bdx * cdz - cdx * bdz) +
                          (bdx * bdx + bdz * bdz) * (cdx * adz - adx * cdz) +
                          (cdx * cdx + cdz * cdz) * (adx * bdz - bdx * adz);
    return determinant < 0;
}

static void Write(mnavTriangulation* tr, int32_t t, uint8_t a, uint8_t b, uint8_t c)
{
    tr->triangles[t] = (mnavDetailTriangle){{a, b, c}, 0};
    tr->changed[t] = 1;
}

static const mnavDetailVertex* Corner(const mnavTriangulation* tr, int32_t t, int32_t k)
{
    return &tr->vertices[tr->triangles[t].corners[k % 3]];
}

// The triangle across the edge from corner k of t to the next, and in
// far the corner of it that is not on the edge; -1 when none.
static int32_t Across(const mnavTriangulation* tr, int32_t t, int32_t k, int32_t* far)
{
    uint8_t u = tr->triangles[t].corners[k];
    uint8_t v = tr->triangles[t].corners[(k + 1) % 3];
    for (int32_t n = 0; n < tr->count; ++n)
    {
        const uint8_t* c = tr->triangles[n].corners;
        for (int32_t j = 0; j < 3 && n != t; ++j)
        {
            if (c[j] == v && c[(j + 1) % 3] == u)
            {
                *far = (j + 2) % 3;
                return n;
            }
        }
    }
    return -1;
}

// Flips the edge from corner k of t when the triangle across has its far
// corner strictly inside t's circumcircle, which with exact arithmetic
// means the two form a strictly convex quadrilateral (across a concave
// one's diagonal the far corner always lies outside). Afterwards t is
// (u, x, w) and the other (x, v, w),
// for the edge u to v, t's third corner w and the far corner x. Returns
// the other triangle, or -1 when nothing flipped.
static int32_t Flip(mnavTriangulation* tr, int32_t t, int32_t k)
{
    int32_t far = 0;
    int32_t n = Across(tr, t, k, &far);
    if (n < 0)
    {
        return -1;
    }
    uint8_t u = tr->triangles[t].corners[k];
    uint8_t v = tr->triangles[t].corners[(k + 1) % 3];
    uint8_t w = tr->triangles[t].corners[(k + 2) % 3];
    uint8_t x = tr->triangles[n].corners[far];
    const mnavDetailVertex* p = tr->vertices;
    if (!InCircle(&p[u], &p[v], &p[w], &p[x]))
    {
        return -1;
    }
    Write(tr, t, u, x, w);
    Write(tr, n, x, v, w);
    return n;
}

// Flips the stacked edges, each the edge from corner 0 of its triangle,
// opposite the corner 2 just inserted, pushing the two new edges opposite
// it after each flip.
static void Legalize(mnavTriangulation* tr, int32_t top)
{
    while (top > 0)
    {
        int32_t t = tr->stack[--top];
        int32_t n = Flip(tr, t, 0);
        if (n >= 0)
        {
            MNAV_ASSERT(top + 2 <= tr->stackCapacity);
            tr->stack[top++] = t;
            tr->stack[top++] = n;
        }
    }
}

void mnavMakeDelaunay(mnavTriangulation* triangulation)
{
    // Each flip raises the triangulation's smallest angles, so the passes
    // end; the bound only guards the proof.
    int32_t passes = 0;
    for (bool flipped = true; flipped; ++passes)
    {
        MNAV_ASSERT(passes <= triangulation->count * triangulation->count + 1);
        flipped = false;
        for (int32_t t = 0; t < triangulation->count; ++t)
        {
            for (int32_t k = 0; k < 3; ++k)
            {
                flipped = Flip(triangulation, t, k) >= 0 || flipped;
            }
        }
    }
}

int32_t mnavLocate(const mnavTriangulation* triangulation, int32_t x, int32_t z, int32_t* edge)
{
    mnavDetailVertex p = {x, 0, z};
    for (int32_t t = 0; t < triangulation->count; ++t)
    {
        int64_t sides[3];
        bool inside = true;
        for (int32_t k = 0; k < 3; ++k)
        {
            sides[k] =
                mnavDetailArea2(Corner(triangulation, t, k), Corner(triangulation, t, k + 1), &p);
            inside = inside && sides[k] <= 0;
        }
        if (inside)
        {
            *edge = sides[0] == 0 ? 0 : (sides[1] == 0 ? 1 : (sides[2] == 0 ? 2 : -1));
            return t;
        }
    }
    *edge = -1;
    return -1;
}

// Splits t at v inside it into three, each with v as corner 2.
static void SplitInside(mnavTriangulation* tr, uint8_t v, int32_t t)
{
    uint8_t a = tr->triangles[t].corners[0];
    uint8_t b = tr->triangles[t].corners[1];
    uint8_t c = tr->triangles[t].corners[2];
    int32_t s = tr->count;
    Write(tr, t, a, b, v);
    Write(tr, s, b, c, v);
    Write(tr, s + 1, c, a, v);
    tr->count += 2;
    tr->stack[0] = t;
    tr->stack[1] = s;
    tr->stack[2] = s + 1;
    Legalize(tr, 3);
}

bool mnavInsertVertex(mnavTriangulation* triangulation, int32_t v, int32_t t, int32_t edge)
{
    mnavTriangulation* tr = triangulation;
    uint8_t p = (uint8_t)v;
    if (edge < 0)
    {
        if (tr->count + 2 > tr->capacity || tr->stackCapacity < 3)
        {
            return false;
        }
        SplitInside(tr, p, t);
        return true;
    }
    int32_t far = 0;
    int32_t n = Across(tr, t, edge, &far);
    if (n < 0 || tr->count + 2 > tr->capacity || tr->stackCapacity < 4)
    {
        return false;
    }
    // The edge a to b of t = (a, b, c) and n = (b, a, d) splits at p into
    // four triangles, each with p as corner 2.
    uint8_t a = tr->triangles[t].corners[edge];
    uint8_t b = tr->triangles[t].corners[(edge + 1) % 3];
    uint8_t c = tr->triangles[t].corners[(edge + 2) % 3];
    uint8_t d = tr->triangles[n].corners[far];
    int32_t s = tr->count;
    Write(tr, t, c, a, p);
    Write(tr, n, b, c, p);
    Write(tr, s, d, b, p);
    Write(tr, s + 1, a, d, p);
    tr->count += 2;
    tr->stack[0] = t;
    tr->stack[1] = n;
    tr->stack[2] = s;
    tr->stack[3] = s + 1;
    Legalize(tr, 4);
    return true;
}
