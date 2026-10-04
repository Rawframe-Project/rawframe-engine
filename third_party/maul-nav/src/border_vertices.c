// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Removing the tile-border vertices from a tile's polygons.

#include "border_vertices.h"

#include "allocator.h"
#include "contour.h"
#include "planar.h"
#include "polymesh.h"
#include "triangulate.h"

#include "maul-nav/bake.h"

#include <stdint.h>
#include <string.h>

// An edge of a polygon around the vertex that does not touch it, and
// whether the hole's chain has taken it.
typedef struct HoleEdge
{
    uint16_t from;
    uint16_t to;
    bool used;
} HoleEdge;

// The scratch one removal uses, kept between vertices. The hole's arrays
// share holeCapacity: the hole, its ring, the triangulation's scratch and
// output, and the merged polygons.
typedef struct Work
{
    mnavMemory* memory;
    mnavPolyMesh* mesh;
    int32_t* touching;
    int32_t touchingCount;
    int32_t touchingCapacity;
    HoleEdge* edges;
    int32_t edgeCount;
    int32_t edgeCapacity;
    uint16_t* hole;
    mnavContourVertex* ring;
    mnavEarScratch ears;
    int32_t* triangles;
    mnavPolygon* polygons;
    int32_t holeCount;
    int32_t holeCapacity;
    bool closed;
    uint8_t* dead;
} Work;

static int32_t Find(const mnavPolygon* polygon, uint16_t vertex)
{
    for (int32_t k = 0; k < polygon->count; ++k)
    {
        if (polygon->vertices[k] == vertex)
        {
            return k;
        }
    }
    return -1;
}

static mnavResult CollectTouching(Work* work, uint16_t vertex)
{
    const mnavPolyMesh* mesh = work->mesh;
    work->touchingCount = 0;
    for (int32_t p = 0; p < mesh->polygonCount; ++p)
    {
        if (Find(&mesh->polygons[p], vertex) < 0)
        {
            continue;
        }
        mnavResult result = mnavReserve(work->memory, (void**)&work->touching,
                                        &work->touchingCapacity, work->touchingCount,
                                        work->touchingCount + 1, sizeof(int32_t), alignof(int32_t));
        if (result != mnav_success)
        {
            return result;
        }
        work->touching[work->touchingCount++] = p;
    }
    return mnav_success;
}

// Whether the polygons around the vertex share one area type.
static bool SameArea(const Work* work)
{
    const mnavPolygon* polygons = work->mesh->polygons;
    for (int32_t t = 1; t < work->touchingCount; ++t)
    {
        if (polygons[work->touching[t]].area != polygons[work->touching[0]].area)
        {
            return false;
        }
    }
    return true;
}

static void ReleaseHole(Work* work)
{
    mnavMemory* memory = work->memory;
    size_t old = (size_t)work->holeCapacity;
    mnavRelease(memory, work->polygons, old, sizeof(mnavPolygon), alignof(mnavPolygon));
    mnavRelease(memory, work->triangles, old * 3, sizeof(int32_t), alignof(int32_t));
    mnavRelease(memory, work->ears.ears, old, sizeof(uint8_t), alignof(uint8_t));
    mnavRelease(memory, work->ears.indices, old, sizeof(int32_t), alignof(int32_t));
    mnavRelease(memory, work->ring, old, sizeof(mnavContourVertex), alignof(mnavContourVertex));
    mnavRelease(memory, work->hole, old, sizeof(uint16_t), alignof(uint16_t));
    work->polygons = nullptr;
    work->triangles = nullptr;
    work->ears = (mnavEarScratch){nullptr, nullptr};
    work->ring = nullptr;
    work->hole = nullptr;
    work->holeCapacity = 0;
}

static mnavResult ReserveHole(Work* work, int32_t count)
{
    if (count <= work->holeCapacity)
    {
        return mnav_success;
    }
    ReleaseHole(work);
    mnavMemory* memory = work->memory;
    size_t n = (size_t)count;
    mnavResult result =
        mnavAllocate(memory, n, sizeof(uint16_t), alignof(uint16_t), (void**)&work->hole);
    if (result == mnav_success)
    {
        result = mnavAllocate(memory, n, sizeof(mnavContourVertex), alignof(mnavContourVertex),
                              (void**)&work->ring);
    }
    if (result == mnav_success)
    {
        result =
            mnavAllocate(memory, n, sizeof(int32_t), alignof(int32_t), (void**)&work->ears.indices);
    }
    if (result == mnav_success)
    {
        result =
            mnavAllocate(memory, n, sizeof(uint8_t), alignof(uint8_t), (void**)&work->ears.ears);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(memory, n * 3, sizeof(int32_t), alignof(int32_t),
                              (void**)&work->triangles);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(memory, n, sizeof(mnavPolygon), alignof(mnavPolygon),
                              (void**)&work->polygons);
    }
    work->holeCapacity = count;
    return result;
}

// Lists the edges of the polygons around the vertex that do not touch it,
// in polygon order.
static mnavResult CollectEdges(Work* work, uint16_t vertex)
{
    const mnavPolygon* polygons = work->mesh->polygons;
    int32_t count = 0;
    for (int32_t t = 0; t < work->touchingCount; ++t)
    {
        count += polygons[work->touching[t]].count - 2;
    }
    mnavResult result = mnavReserve(work->memory, (void**)&work->edges, &work->edgeCapacity, 0,
                                    count, sizeof(HoleEdge), alignof(HoleEdge));
    if (result == mnav_success)
    {
        result = ReserveHole(work, count + 1);
    }
    if (result != mnav_success)
    {
        return result;
    }
    work->edgeCount = 0;
    for (int32_t t = 0; t < work->touchingCount; ++t)
    {
        const mnavPolygon* polygon = &polygons[work->touching[t]];
        for (int32_t k = 0; k < polygon->count; ++k)
        {
            uint16_t from = polygon->vertices[k];
            uint16_t to = polygon->vertices[(k + 1) % polygon->count];
            if (from != vertex && to != vertex)
            {
                work->edges[work->edgeCount++] = (HoleEdge){from, to, false};
            }
        }
    }
    return mnav_success;
}

// Chains the edges into the hole's outline, each edge extending its end
// or its start, the first fitting edge in order each time. Returns false
// when the edges do not form one chain.
static bool Chain(Work* work)
{
    HoleEdge* edges = work->edges;
    uint16_t* hole = work->hole;
    hole[0] = edges[0].from;
    hole[1] = edges[0].to;
    edges[0].used = true;
    int32_t count = 2;
    for (int32_t placed = 1; placed < work->edgeCount; ++placed)
    {
        int32_t found = -1;
        bool atStart = false;
        for (int32_t e = 0; e < work->edgeCount && found < 0; ++e)
        {
            if (!edges[e].used && (edges[e].from == hole[count - 1] || edges[e].to == hole[0]))
            {
                found = e;
                atStart = edges[e].from != hole[count - 1];
            }
        }
        if (found < 0)
        {
            return false;
        }
        edges[found].used = true;
        if (atStart)
        {
            memmove(hole + 1, hole, (size_t)count * sizeof(uint16_t));
            hole[0] = edges[found].from;
        }
        else
        {
            hole[count] = edges[found].to;
        }
        count += 1;
    }
    // A closed chain, round a vertex inside the mesh, repeats its start.
    work->closed = hole[0] == hole[count - 1];
    count -= work->closed ? 1 : 0;
    work->holeCount = count;
    return count >= 3;
}

static mnavContourVertex Ground(const mnavPolyMesh* mesh, uint16_t index)
{
    const mnavMeshVertex* v = &mesh->vertices[index];
    return (mnavContourVertex){v->x, v->y, v->z, 0, 0};
}

// Whether the hole keeps the outline's shape: a closed chain lies round
// a vertex inside the mesh; an open one must close across the vertex's
// own place, the vertex lying on the segment from the chain's end back
// to its start.
static bool KeepsOutline(const Work* work, uint16_t vertex)
{
    if (work->closed)
    {
        return true;
    }
    const mnavPolyMesh* mesh = work->mesh;
    mnavContourVertex a = Ground(mesh, work->hole[work->holeCount - 1]);
    mnavContourVertex b = Ground(mesh, work->hole[0]);
    mnavContourVertex v = Ground(mesh, vertex);
    bool inX = (a.x <= v.x && v.x <= b.x) || (b.x <= v.x && v.x <= a.x);
    bool inZ = (a.z <= v.z && v.z <= b.z) || (b.z <= v.z && v.z <= a.z);
    return mnavArea2(&a, &b, &v) == 0 && inX && inZ;
}

// Triangulates the hole and merges its triangles. Returns the number of
// polygons, or -1 when the triangulation does not finish.
static int32_t Fill(Work* work)
{
    const mnavPolyMesh* mesh = work->mesh;
    for (int32_t k = 0; k < work->holeCount; ++k)
    {
        work->ring[k] = Ground(mesh, work->hole[k]);
    }
    bool complete = true;
    int32_t triangles =
        mnavTriangulate(work->ring, work->holeCount, work->ears, work->triangles, &complete);
    if (!complete)
    {
        return -1;
    }
    const mnavPolygon* first = &mesh->polygons[work->touching[0]];
    uint32_t region = first->region;
    for (int32_t t = 1; t < work->touchingCount; ++t)
    {
        region = mesh->polygons[work->touching[t]].region == region ? region : MNAV_MIXED_REGION;
    }
    int32_t count = 0;
    for (int32_t t = 0; t < triangles; ++t)
    {
        const int32_t* corner = &work->triangles[t * 3];
        uint16_t a = work->hole[corner[0]];
        uint16_t b = work->hole[corner[1]];
        uint16_t c = work->hole[corner[2]];
        if (a == b || a == c || b == c)
        {
            continue;
        }
        mnavPolygon* polygon = &work->polygons[count++];
        memset(polygon, 0xFF, sizeof(*polygon));
        polygon->vertices[0] = a;
        polygon->vertices[1] = b;
        polygon->vertices[2] = c;
        memset(polygon->sides, 0, sizeof(polygon->sides));
        polygon->count = 3;
        polygon->area = first->area;
        polygon->region = region;
    }
    return mnavMergePolygons(mesh->vertices, work->polygons, count);
}

// Replaces the polygons around the vertex, in order, with the hole's.
static mnavResult Commit(Work* work, int32_t added, int32_t maxPolygons)
{
    mnavPolyMesh* mesh = work->mesh;
    int32_t kept = mesh->polygonCount - work->touchingCount;
    if (kept + added > maxPolygons)
    {
        return mnav_errorLimit;
    }
    mnavResult result =
        mnavReserve(work->memory, (void**)&mesh->polygons, &mesh->polygonCapacity,
                    mesh->polygonCount, kept + added, sizeof(mnavPolygon), alignof(mnavPolygon));
    if (result != mnav_success)
    {
        return result;
    }
    int32_t write = 0;
    int32_t next = 0;
    for (int32_t p = 0; p < mesh->polygonCount; ++p)
    {
        if (next < work->touchingCount && work->touching[next] == p)
        {
            next += 1;
            continue;
        }
        mesh->polygons[write++] = mesh->polygons[p];
    }
    memcpy(mesh->polygons + write, work->polygons, (size_t)added * sizeof(mnavPolygon));
    mesh->polygonCount = write + added;
    return mnav_success;
}

// Removes one vertex when it can be, marking it dead.
static mnavResult RemoveVertex(Work* work, uint16_t vertex, int32_t maxPolygons)
{
    mnavResult result = CollectTouching(work, vertex);
    if (result != mnav_success || work->touchingCount == 0 || !SameArea(work))
    {
        return result;
    }
    result = CollectEdges(work, vertex);
    if (result != mnav_success || !Chain(work) || !KeepsOutline(work, vertex))
    {
        return result;
    }
    int32_t added = Fill(work);
    if (added < 0)
    {
        return mnav_success;
    }
    result = Commit(work, added, maxPolygons);
    if (result == mnav_success)
    {
        work->dead[vertex] = 1;
    }
    return result;
}

// Drops the dead vertices and renumbers the polygons once.
static void Compact(mnavPolyMesh* mesh, const uint8_t* dead, uint16_t* remap)
{
    int32_t write = 0;
    for (int32_t v = 0; v < mesh->vertexCount; ++v)
    {
        remap[v] = (uint16_t)write;
        if (dead[v] == 0)
        {
            mesh->vertices[write] = mesh->vertices[v];
            mesh->removable[write] = mesh->removable[v];
            write += 1;
        }
    }
    mesh->vertexCount = write;
    for (int32_t p = 0; p < mesh->polygonCount; ++p)
    {
        mnavPolygon* polygon = &mesh->polygons[p];
        for (int32_t k = 0; k < polygon->count; ++k)
        {
            polygon->vertices[k] = remap[polygon->vertices[k]];
        }
    }
}

mnavResult mnavRemoveBorderVertices(mnavMemory* memory, mnavPolyMesh* mesh, int32_t maxPolygons)
{
    Work work = {0};
    work.memory = memory;
    work.mesh = mesh;
    size_t count = (size_t)mesh->vertexCount;
    uint16_t* remap = nullptr;
    mnavResult result =
        mnavAllocate(memory, count, sizeof(uint8_t), alignof(uint8_t), (void**)&work.dead);
    if (result == mnav_success)
    {
        result = mnavAllocate(memory, count, sizeof(uint16_t), alignof(uint16_t), (void**)&remap);
    }
    if (result == mnav_success && count > 0)
    {
        memset(work.dead, 0, count);
    }
    for (int32_t v = 0; v < mesh->vertexCount && result == mnav_success; ++v)
    {
        if (mesh->removable[v] != 0)
        {
            result = RemoveVertex(&work, (uint16_t)v, maxPolygons);
        }
    }
    if (result == mnav_success)
    {
        Compact(mesh, work.dead, remap);
    }
    ReleaseHole(&work);
    mnavRelease(memory, work.edges, (size_t)work.edgeCapacity, sizeof(HoleEdge), alignof(HoleEdge));
    mnavRelease(memory, work.touching, (size_t)work.touchingCapacity, sizeof(int32_t),
                alignof(int32_t));
    mnavRelease(memory, remap, count, sizeof(uint16_t), alignof(uint16_t));
    mnavRelease(memory, work.dead, count, sizeof(uint8_t), alignof(uint8_t));
    return result;
}
