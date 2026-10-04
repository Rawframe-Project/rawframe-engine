// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Triangulating a simple ring by ear clipping.

#include "triangulate.h"

#include "contour.h"
#include "planar.h"

#include <stdint.h>

// The remaining ring: n of the original vertices, in order.
typedef struct Ring
{
    const mnavContourVertex* vertices;
    int32_t* indices;
    int32_t n;
} Ring;

static int32_t Next(int32_t i, int32_t n)
{
    return i + 1 < n ? i + 1 : 0;
}

static int32_t Prev(int32_t i, int32_t n)
{
    return i > 0 ? i - 1 : n - 1;
}

static const mnavContourVertex* At(const Ring* ring, int32_t i)
{
    return &ring->vertices[ring->indices[i]];
}

// Whether the segment from i to j crosses no edge of the ring other than
// those at i and j; loose lets it touch.
static bool Clear(const Ring* ring, int32_t i, int32_t j, bool loose)
{
    const mnavContourVertex* d0 = At(ring, i);
    const mnavContourVertex* d1 = At(ring, j);
    for (int32_t k = 0; k < ring->n; ++k)
    {
        int32_t k1 = Next(k, ring->n);
        if (k == i || k1 == i || k == j || k1 == j)
        {
            continue;
        }
        const mnavContourVertex* p0 = At(ring, k);
        const mnavContourVertex* p1 = At(ring, k1);
        if (mnavSameGround(d0, p0) || mnavSameGround(d1, p0) || mnavSameGround(d0, p1) ||
            mnavSameGround(d1, p1))
        {
            continue;
        }
        bool meets = loose ? mnavIntersectProper(d0, d1, p0, p1) : mnavIntersect(d0, d1, p0, p1);
        if (meets)
        {
            return false;
        }
    }
    return true;
}

// Whether no other vertex of the ring lies strictly inside the triangle
// of i, the vertex after it, and j. The edge test above skips edges that
// touch the positions of i and j, as it must for the two copies of a hole
// bridge's ends, and so cannot see a vertex both of whose edges run to
// those positions; this test can.
static bool Empty(const Ring* ring, int32_t i, int32_t j)
{
    int32_t middle = Next(i, ring->n);
    const mnavContourVertex* a = At(ring, i);
    const mnavContourVertex* b = At(ring, middle);
    const mnavContourVertex* c = At(ring, j);
    for (int32_t k = 0; k < ring->n; ++k)
    {
        if (k == i || k == middle || k == j)
        {
            continue;
        }
        const mnavContourVertex* p = At(ring, k);
        if (mnavLeft(a, b, p) && mnavLeft(b, c, p) && mnavLeft(c, a, p))
        {
            return false;
        }
    }
    return true;
}

// Whether i to j, which skips one vertex, is a diagonal inside the ring.
static bool Diagonal(const Ring* ring, int32_t i, int32_t j, bool loose)
{
    int32_t n = ring->n;
    bool inCone =
        mnavInCone(At(ring, Prev(i, n)), At(ring, i), At(ring, Next(i, n)), At(ring, j), loose);
    return inCone && Clear(ring, i, j, loose) && Empty(ring, i, j);
}

static int64_t Length2(const Ring* ring, int32_t i, int32_t j)
{
    int64_t dx = At(ring, j)->x - At(ring, i)->x;
    int64_t dz = At(ring, j)->z - At(ring, i)->z;
    return dx * dx + dz * dz;
}

// The vertex before the ear to cut, the ear with the shortest diagonal,
// the first in ring order on ties; or -1. Strict ears come from the flags,
// loose ones are tested afresh.
static int32_t ChooseEar(const Ring* ring, const uint8_t* ears, bool loose)
{
    int32_t best = -1;
    int64_t bestLength = 0;
    for (int32_t i = 0; i < ring->n; ++i)
    {
        int32_t i1 = Next(i, ring->n);
        int32_t i2 = Next(i1, ring->n);
        bool ear = loose ? Diagonal(ring, i, i2, true) : ears[i1] != 0;
        if (!ear)
        {
            continue;
        }
        int64_t length = Length2(ring, i, i2);
        if (best < 0 || length < bestLength)
        {
            best = i;
            bestLength = length;
        }
    }
    return best;
}

int32_t mnavTriangulate(const mnavContourVertex* ring, int32_t count, mnavEarScratch scratch,
                        int32_t* triangles, bool* complete)
{
    Ring r = {ring, scratch.indices, count};
    uint8_t* ears = scratch.ears;
    for (int32_t i = 0; i < count; ++i)
    {
        r.indices[i] = i;
    }
    for (int32_t i = 0; i < count; ++i)
    {
        ears[Next(i, count)] = Diagonal(&r, i, Next(Next(i, count), count), false);
    }
    int32_t written = 0;
    *complete = true;
    while (r.n > 3)
    {
        int32_t i = ChooseEar(&r, ears, false);
        i = i >= 0 ? i : ChooseEar(&r, ears, true);
        if (i < 0)
        {
            *complete = false;
            return written;
        }
        int32_t i1 = Next(i, r.n);
        int32_t i2 = Next(i1, r.n);
        triangles[written * 3 + 0] = r.indices[i];
        triangles[written * 3 + 1] = r.indices[i1];
        triangles[written * 3 + 2] = r.indices[i2];
        written += 1;
        // Remove the ear's tip and re-test the ears beside it.
        r.n -= 1;
        for (int32_t k = i1; k < r.n; ++k)
        {
            r.indices[k] = r.indices[k + 1];
            ears[k] = ears[k + 1];
        }
        i1 = i1 >= r.n ? 0 : i1;
        i = Prev(i1, r.n);
        ears[i] = Diagonal(&r, Prev(i, r.n), i1, false);
        ears[i1] = Diagonal(&r, i, Next(i1, r.n), false);
    }
    triangles[written * 3 + 0] = r.indices[0];
    triangles[written * 3 + 1] = r.indices[1];
    triangles[written * 3 + 2] = r.indices[2];
    return written + 1;
}
