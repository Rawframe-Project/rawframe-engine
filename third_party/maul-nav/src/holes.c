// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Merging hole contours into their outlines.

#include "holes.h"

#include "allocator.h"
#include "contour.h"
#include "planar.h"

#include <stdint.h>
#include <string.h>

// A hole and its lowest-left vertex, by which holes are ordered.
typedef struct Hole
{
    int32_t contour;
    int32_t minX;
    int32_t minZ;
    int32_t leftmost;
} Hole;

// An outline vertex a hole vertex could bridge to.
typedef struct Candidate
{
    int32_t vertex;
    int64_t distance;
} Candidate;

typedef struct Merger
{
    mnavMemory* memory;
    mnavContourSet* set;
    Hole* holes;
    int32_t holeCount;
    Candidate* candidates;
    int32_t candidateCapacity;
} Merger;

// Whether segment d0 d1 crosses an edge of a contour, skipping the edges
// at vertex skip (or none for -1) and edges sharing an end with it.
static bool CrossesContour(const mnavContourVertex* d0, const mnavContourVertex* d1, int32_t skip,
                           const mnavContourVertex* vertices, int32_t count)
{
    for (int32_t k = 0; k < count; ++k)
    {
        int32_t k1 = (k + 1) % count;
        const mnavContourVertex* p0 = &vertices[k];
        const mnavContourVertex* p1 = &vertices[k1];
        bool incident = k == skip || k1 == skip || mnavSameGround(d0, p0) ||
                        mnavSameGround(d1, p0) || mnavSameGround(d0, p1) || mnavSameGround(d1, p1);
        if (!incident && mnavIntersect(d0, d1, p0, p1))
        {
            return true;
        }
    }
    return false;
}

// Whether point p lies in the cone at vertex i of a contour.
static bool InCone(const mnavContourVertex* vertices, int32_t count, int32_t i,
                   const mnavContourVertex* p)
{
    return mnavInCone(&vertices[(i + count - 1) % count], &vertices[i], &vertices[(i + 1) % count],
                      p, false);
}

static const mnavContourVertex* VerticesOf(const mnavContourSet* set, int32_t contour)
{
    return set->vertices + set->contours[contour].first;
}

static bool HoleBefore(const Hole* a, const Hole* b)
{
    if (a->minX != b->minX)
    {
        return a->minX < b->minX;
    }
    if (a->minZ != b->minZ)
    {
        return a->minZ < b->minZ;
    }
    return a->contour < b->contour;
}

static bool CandidateBefore(const Candidate* a, const Candidate* b)
{
    return a->distance != b->distance ? a->distance < b->distance : a->vertex < b->vertex;
}

// The bridge from hole h of the sorted holes, whose region's holes end at
// end, to an outline vertex: tried from the hole's lowest-left vertex
// round, each time through the nearest outline vertex whose diagonal
// crosses neither the outline nor a hole not yet merged.
static mnavResult FindBridge(Merger* merger, int32_t outline, int32_t h, int32_t end,
                             int32_t* outlineVertex, int32_t* holeVertex)
{
    mnavContourSet* set = merger->set;
    int32_t outlineCount = set->contours[outline].count;
    mnavResult result =
        mnavReserve(merger->memory, (void**)&merger->candidates, &merger->candidateCapacity, 0,
                    outlineCount, sizeof(Candidate), alignof(Candidate));
    if (result != mnav_success)
    {
        return result;
    }
    const mnavContour* hole = &set->contours[merger->holes[h].contour];
    const mnavContourVertex* ring = VerticesOf(set, outline);
    *outlineVertex = -1;
    for (int32_t tried = 0; tried < hole->count && *outlineVertex < 0; ++tried)
    {
        *holeVertex = (merger->holes[h].leftmost + tried) % hole->count;
        const mnavContourVertex* corner = &set->vertices[hole->first + *holeVertex];
        int32_t count = 0;
        for (int32_t j = 0; j < outlineCount; ++j)
        {
            if (!InCone(ring, outlineCount, j, corner))
            {
                continue;
            }
            int64_t dx = ring[j].x - corner->x;
            int64_t dz = ring[j].z - corner->z;
            Candidate candidate = {j, dx * dx + dz * dz};
            int32_t at = count++;
            while (at > 0 && CandidateBefore(&candidate, &merger->candidates[at - 1]))
            {
                merger->candidates[at] = merger->candidates[at - 1];
                --at;
            }
            merger->candidates[at] = candidate;
        }
        for (int32_t k = 0; k < count && *outlineVertex < 0; ++k)
        {
            int32_t j = merger->candidates[k].vertex;
            bool crosses = CrossesContour(&ring[j], corner, j, ring, outlineCount);
            for (int32_t other = h; other < end && !crosses; ++other)
            {
                const mnavContour* o = &set->contours[merger->holes[other].contour];
                crosses = CrossesContour(&ring[j], corner, -1,
                                         VerticesOf(set, merger->holes[other].contour), o->count);
            }
            *outlineVertex = crosses ? -1 : j;
        }
    }
    return mnav_success;
}

// Splices a hole into an outline through the bridge between outline
// vertex a and hole vertex b: the outline from a round to a, then the
// hole from b round to b.
static mnavResult Splice(Merger* merger, int32_t outline, int32_t hole, int32_t a, int32_t b)
{
    mnavContourSet* set = merger->set;
    int32_t n = set->contours[outline].count;
    int32_t m = set->contours[hole].count;
    int32_t first = set->vertexCount;
    mnavResult result = mnavReserve(merger->memory, (void**)&set->vertices, &set->vertexCapacity,
                                    set->vertexCount, set->vertexCount + n + m + 2,
                                    sizeof(mnavContourVertex), alignof(mnavContourVertex));
    if (result != mnav_success)
    {
        return result;
    }
    mnavContourVertex* out = set->vertices + first;
    const mnavContourVertex* ring = VerticesOf(set, outline);
    const mnavContourVertex* inner = VerticesOf(set, hole);
    for (int32_t i = 0; i <= n; ++i)
    {
        out[i] = ring[(a + i) % n];
    }
    for (int32_t i = 0; i <= m; ++i)
    {
        out[n + 1 + i] = inner[(b + i) % m];
    }
    set->vertexCount += n + m + 2;
    set->contours[outline].first = first;
    set->contours[outline].count = n + m + 2;
    set->contours[hole].count = 0;
    return mnav_success;
}

static void Drop(mnavContourSet* set, int32_t contour)
{
    set->contours[contour].count = 0;
    set->droppedHoles += 1;
}

static void FindLeftmost(const mnavContourSet* set, Hole* hole)
{
    const mnavContourVertex* vertices = VerticesOf(set, hole->contour);
    hole->minX = vertices[0].x;
    hole->minZ = vertices[0].z;
    hole->leftmost = 0;
    for (int32_t i = 1; i < set->contours[hole->contour].count; ++i)
    {
        if (vertices[i].x < hole->minX ||
            (vertices[i].x == hole->minX && vertices[i].z < hole->minZ))
        {
            hole->minX = vertices[i].x;
            hole->minZ = vertices[i].z;
            hole->leftmost = i;
        }
    }
}

// Groups the holes by region in contour order, each group sorted left to
// right; starts[r] to starts[r + 1] are region r's holes. outlines[r] is
// region r's outline contour, or -1.
static void Group(Merger* merger, uint32_t regionCount, int32_t* starts, int32_t* outlines)
{
    mnavContourSet* set = merger->set;
    memset(starts, 0, ((size_t)regionCount + 2) * sizeof(int32_t));
    for (uint32_t r = 0; r <= regionCount; ++r)
    {
        outlines[r] = -1;
    }
    for (int32_t c = 0; c < set->count; ++c)
    {
        uint32_t region = set->contours[c].region;
        if (set->contours[c].hole)
        {
            starts[region + 1] += 1;
        }
        else if (outlines[region] < 0)
        {
            outlines[region] = c;
        }
    }
    for (uint32_t r = 0; r <= regionCount; ++r)
    {
        starts[r + 1] += starts[r];
    }
    for (int32_t c = 0; c < set->count; ++c)
    {
        if (set->contours[c].hole)
        {
            Hole hole = {c, 0, 0, 0};
            FindLeftmost(set, &hole);
            int32_t region = (int32_t)set->contours[c].region;
            merger->holes[starts[region]++] = hole;
        }
    }
    for (uint32_t r = regionCount + 1; r > 0; --r)
    {
        starts[r] = starts[r - 1];
    }
    starts[0] = 0;
    for (uint32_t r = 0; r <= regionCount; ++r)
    {
        for (int32_t i = starts[r] + 1; i < starts[r + 1]; ++i)
        {
            Hole hole = merger->holes[i];
            int32_t j = i;
            while (j > starts[r] && HoleBefore(&hole, &merger->holes[j - 1]))
            {
                merger->holes[j] = merger->holes[j - 1];
                --j;
            }
            merger->holes[j] = hole;
        }
    }
}

// Copies the live contours into a fresh vertex pool, in order, dropping
// the merged and dropped holes.
static mnavResult Compact(mnavMemory* memory, mnavContourSet* set)
{
    int32_t total = 0;
    for (int32_t c = 0; c < set->count; ++c)
    {
        total += set->contours[c].count;
    }
    mnavContourVertex* pool = nullptr;
    mnavResult result = mnavAllocate(memory, (size_t)total, sizeof(mnavContourVertex),
                                     alignof(mnavContourVertex), (void**)&pool);
    if (result != mnav_success)
    {
        return result;
    }
    int32_t written = 0;
    int32_t kept = 0;
    for (int32_t c = 0; c < set->count; ++c)
    {
        mnavContour contour = set->contours[c];
        if (contour.count == 0)
        {
            continue;
        }
        memcpy(pool + written, set->vertices + contour.first,
               (size_t)contour.count * sizeof(mnavContourVertex));
        contour.first = written;
        written += contour.count;
        set->contours[kept++] = contour;
    }
    mnavRelease(memory, set->vertices, (size_t)set->vertexCapacity, sizeof(mnavContourVertex),
                alignof(mnavContourVertex));
    set->vertices = pool;
    set->vertexCount = total;
    set->vertexCapacity = total;
    set->count = kept;
    return mnav_success;
}

static mnavResult MergeRegions(Merger* merger, uint32_t regionCount, const int32_t* starts,
                               const int32_t* outlines)
{
    for (uint32_t r = 1; r <= regionCount; ++r)
    {
        for (int32_t h = starts[r]; h < starts[r + 1]; ++h)
        {
            int32_t hole = merger->holes[h].contour;
            if (outlines[r] < 0)
            {
                Drop(merger->set, hole);
                continue;
            }
            int32_t a = -1;
            int32_t b = -1;
            mnavResult result = FindBridge(merger, outlines[r], h, starts[r + 1], &a, &b);
            if (result == mnav_success && a >= 0)
            {
                result = Splice(merger, outlines[r], hole, a, b);
            }
            else if (result == mnav_success)
            {
                Drop(merger->set, hole);
            }
            if (result != mnav_success)
            {
                return result;
            }
        }
    }
    return mnav_success;
}

mnavResult mnavMergeHoles(mnavMemory* memory, mnavContourSet* set, uint32_t regionCount)
{
    int32_t holeCount = 0;
    for (int32_t c = 0; c < set->count; ++c)
    {
        holeCount += set->contours[c].hole;
    }
    if (holeCount == 0)
    {
        return mnav_success;
    }
    Merger merger = {memory, set, nullptr, holeCount, nullptr, 0};
    int32_t* starts = nullptr;
    int32_t* outlines = nullptr;
    size_t regions = (size_t)regionCount + 2;
    mnavResult result =
        mnavAllocate(memory, (size_t)holeCount, sizeof(Hole), alignof(Hole), (void**)&merger.holes);
    if (result == mnav_success)
    {
        result = mnavAllocate(memory, regions, sizeof(int32_t), alignof(int32_t), (void**)&starts);
    }
    if (result == mnav_success)
    {
        result =
            mnavAllocate(memory, regions, sizeof(int32_t), alignof(int32_t), (void**)&outlines);
    }
    if (result == mnav_success)
    {
        Group(&merger, regionCount, starts, outlines);
        result = MergeRegions(&merger, regionCount, starts, outlines);
    }
    if (result == mnav_success)
    {
        result = Compact(memory, set);
    }
    mnavRelease(memory, merger.candidates, (size_t)merger.candidateCapacity, sizeof(Candidate),
                alignof(Candidate));
    mnavRelease(memory, outlines, regions, sizeof(int32_t), alignof(int32_t));
    mnavRelease(memory, starts, regions, sizeof(int32_t), alignof(int32_t));
    mnavRelease(memory, merger.holes, (size_t)holeCount, sizeof(Hole), alignof(Hole));
    return result;
}
