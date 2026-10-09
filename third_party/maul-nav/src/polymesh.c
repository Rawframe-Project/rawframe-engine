// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A tile's polygon mesh.

#include "polymesh.h"

#include "allocator.h"
#include "contour.h"
#include "triangulate.h"

#include "maul-nav/bake.h"

#include <stdint.h>
#include <string.h>

// Vertices are found again by ground position through hash buckets.
#define BUCKETS 4096
// Vertices at one ground position within this many cell heights weld.
#define WELD_HEIGHT 2

typedef struct Builder
{
    mnavMemory* memory;
    mnavPolyMesh* mesh;
    int32_t maxVertices;
    int32_t maxPolygons;
    int32_t* buckets;
    // The bucket chain, one entry per vertex, sized with the vertices.
    int32_t* chain;
    // Per ring: its welded vertex per ring vertex, the triangulation's
    // scratch and output, and the ring's polygons while they merge.
    int32_t* welded;
    mnavEarScratch ears;
    int32_t* triangles;
    mnavPolygon* polygons;
    int32_t* mergeTable;
    int32_t ringCapacity;
} Builder;

static uint32_t Bucket(int32_t x, int32_t z)
{
    uint32_t hash = (uint32_t)x * 0x8DA6B343u + (uint32_t)z * 0xCB1AB31Fu;
    return hash & (BUCKETS - 1);
}

// Grows the vertices, their removable flags and the bucket chain
// together to hold needed vertices.
static mnavResult GrowVertices(Builder* builder, int32_t needed)
{
    mnavPolyMesh* mesh = builder->mesh;
    if (needed <= mesh->vertexCapacity)
    {
        return mnav_success;
    }
    int32_t capacity = mesh->vertexCapacity < 64 ? 64 : mesh->vertexCapacity * 2;
    capacity = capacity > builder->maxVertices ? builder->maxVertices : capacity;
    capacity = capacity < needed ? needed : capacity;
    mnavMeshVertex* vertices = nullptr;
    uint8_t* removable = nullptr;
    int32_t* chain = nullptr;
    mnavResult result = mnavAllocate(builder->memory, (size_t)capacity, sizeof(mnavMeshVertex),
                                     alignof(mnavMeshVertex), (void**)&vertices);
    if (result == mnav_success)
    {
        result = mnavAllocate(builder->memory, (size_t)capacity, sizeof(uint8_t), alignof(uint8_t),
                              (void**)&removable);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(builder->memory, (size_t)capacity, sizeof(int32_t), alignof(int32_t),
                              (void**)&chain);
    }
    if (result != mnav_success)
    {
        mnavRelease(builder->memory, removable, (size_t)capacity, sizeof(uint8_t),
                    alignof(uint8_t));
        mnavRelease(builder->memory, vertices, (size_t)capacity, sizeof(mnavMeshVertex),
                    alignof(mnavMeshVertex));
        return result;
    }
    size_t count = (size_t)mesh->vertexCount;
    if (count > 0)
    {
        memcpy(vertices, mesh->vertices, count * sizeof(mnavMeshVertex));
        memcpy(removable, mesh->removable, count * sizeof(uint8_t));
        memcpy(chain, builder->chain, count * sizeof(int32_t));
    }
    size_t old = (size_t)mesh->vertexCapacity;
    mnavRelease(builder->memory, builder->chain, old, sizeof(int32_t), alignof(int32_t));
    mnavRelease(builder->memory, mesh->removable, old, sizeof(uint8_t), alignof(uint8_t));
    mnavRelease(builder->memory, mesh->vertices, old, sizeof(mnavMeshVertex),
                alignof(mnavMeshVertex));
    mesh->vertices = vertices;
    mesh->removable = removable;
    builder->chain = chain;
    mesh->vertexCapacity = capacity;
    return mnav_success;
}

// The index of the welded vertex at v, added if there is none.
static mnavResult Weld(Builder* builder, const mnavContourVertex* v, int32_t* indexOut)
{
    mnavPolyMesh* mesh = builder->mesh;
    uint32_t bucket = Bucket(v->x, v->z);
    for (int32_t i = builder->buckets[bucket]; i >= 0; i = builder->chain[i])
    {
        const mnavMeshVertex* w = &mesh->vertices[i];
        int32_t dy = (int32_t)w->y - v->y;
        if (w->x == v->x && w->z == v->z && dy >= -WELD_HEIGHT && dy <= WELD_HEIGHT)
        {
            *indexOut = i;
            return mnav_success;
        }
    }
    int32_t index = mesh->vertexCount;
    if (index >= builder->maxVertices)
    {
        return mnav_errorLimit;
    }
    mnavResult result = GrowVertices(builder, index + 1);
    if (result != mnav_success)
    {
        return result;
    }
    mesh->vertices[index] = (mnavMeshVertex){(uint16_t)v->x, (uint16_t)v->y, (uint16_t)v->z};
    mesh->removable[index] = 0;
    builder->chain[index] = builder->buckets[bucket];
    builder->buckets[bucket] = index;
    mesh->vertexCount += 1;
    *indexOut = index;
    return mnav_success;
}

// The squared length of the edge two polygons share, when merging them
// gives a strictly convex polygon of at most MNAV_POLYGON_VERTICES
// vertices; -1 otherwise. ea and eb receive the shared edge in each.
static int64_t MergeValue(const mnavMeshVertex* vertices, const mnavPolygon* a,
                          const mnavPolygon* b, int32_t* ea, int32_t* eb)
{
    int32_t na = a->count;
    int32_t nb = b->count;
    if (na + nb - 2 > MNAV_POLYGON_VERTICES)
    {
        return -1;
    }
    *ea = -1;
    for (int32_t i = 0; i < na && *ea < 0; ++i)
    {
        uint16_t a0 = a->vertices[i];
        uint16_t a1 = a->vertices[(i + 1) % na];
        for (int32_t j = 0; j < nb; ++j)
        {
            if (b->vertices[j] == a1 && b->vertices[(j + 1) % nb] == a0)
            {
                *ea = i;
                *eb = j;
                break;
            }
        }
    }
    if (*ea < 0)
    {
        return -1;
    }
    // The merged polygon turns the same way at both ends of the shared
    // edge, strictly.
    const mnavMeshVertex* p = &vertices[a->vertices[(*ea + na - 1) % na]];
    const mnavMeshVertex* q = &vertices[a->vertices[*ea]];
    const mnavMeshVertex* r = &vertices[b->vertices[(*eb + 2) % nb]];
    int64_t turnA = (int64_t)(q->x - p->x) * (r->z - p->z) - (int64_t)(r->x - p->x) * (q->z - p->z);
    p = &vertices[b->vertices[(*eb + nb - 1) % nb]];
    q = &vertices[b->vertices[*eb]];
    r = &vertices[a->vertices[(*ea + 2) % na]];
    int64_t turnB = (int64_t)(q->x - p->x) * (r->z - p->z) - (int64_t)(r->x - p->x) * (q->z - p->z);
    if (turnA >= 0 || turnB >= 0)
    {
        return -1;
    }
    const mnavMeshVertex* s = &vertices[a->vertices[*ea]];
    const mnavMeshVertex* t = &vertices[a->vertices[(*ea + 1) % na]];
    int64_t dx = (int64_t)s->x - t->x;
    int64_t dz = (int64_t)s->z - t->z;
    return dx * dx + dz * dz;
}

// Replaces a with the union of a and b across their shared edge.
static void Merge(mnavPolygon* a, const mnavPolygon* b, int32_t ea, int32_t eb)
{
    uint16_t merged[MNAV_POLYGON_VERTICES];
    int32_t n = 0;
    for (int32_t i = 0; i < a->count - 1; ++i)
    {
        merged[n++] = a->vertices[(ea + 1 + i) % a->count];
    }
    for (int32_t i = 0; i < b->count - 1; ++i)
    {
        merged[n++] = b->vertices[(eb + 1 + i) % b->count];
    }
    for (int32_t i = 0; i < MNAV_POLYGON_VERTICES; ++i)
    {
        a->vertices[i] = i < n ? merged[i] : (uint16_t)MNAV_NO_INDEX;
    }
    a->count = (uint8_t)n;
}

int32_t mnavMergeTableSize(int32_t count)
{
    int32_t size = 16;
    while (size < 2 * MNAV_POLYGON_VERTICES * count)
    {
        size *= 2;
    }
    return size;
}

static uint32_t EdgeHash(uint16_t from, uint16_t to)
{
    uint32_t h = (uint32_t)from * 0x9E3779B1u ^ (uint32_t)to * 0x85EBCA77u;
    return h ^ (h >> 15);
}

// Lists every polygon edge in the table, keyed by its ends in order: an
// entry is the polygon times MNAV_POLYGON_VERTICES plus the edge.
static void ListMergeEdges(const mnavPolygon* polygons, int32_t count, int32_t* table,
                           uint32_t mask)
{
    memset(table, 0xFF, ((size_t)mask + 1) * sizeof(int32_t));
    for (int32_t p = 0; p < count; ++p)
    {
        const mnavPolygon* polygon = &polygons[p];
        for (int32_t i = 0; i < polygon->count; ++i)
        {
            uint16_t from = polygon->vertices[i];
            uint16_t to = polygon->vertices[(i + 1) % polygon->count];
            uint32_t slot = EdgeHash(from, to) & mask;
            while (table[slot] >= 0)
            {
                slot = (slot + 1u) & mask;
            }
            table[slot] = p * MNAV_POLYGON_VERTICES + i;
        }
    }
}

// The best merge, as a scan of every pair j < k in order would find it:
// the greatest value, then the first pair. Only polygons sharing an edge
// can merge, so polygon j looks up its edges' reverses in the table.
typedef struct Best
{
    int64_t value;
    int32_t a;
    int32_t b;
    int32_t ea;
    int32_t eb;
} Best;

static void Consider(const mnavMeshVertex* vertices, const mnavPolygon* polygons, int32_t j,
                     int32_t k, Best* best)
{
    int32_t ea = 0;
    int32_t eb = 0;
    int64_t value = MergeValue(vertices, &polygons[j], &polygons[k], &ea, &eb);
    bool first = value == best->value && (j < best->a || (j == best->a && k < best->b));
    if (value > best->value || (value > 0 && first))
    {
        *best = (Best){value, j, k, ea, eb};
    }
}

static Best FindBest(const mnavMeshVertex* vertices, const mnavPolygon* polygons, int32_t count,
                     const int32_t* table, uint32_t mask)
{
    Best best = {0, -1, -1, 0, 0};
    for (int32_t j = 0; j < count; ++j)
    {
        const mnavPolygon* polygon = &polygons[j];
        for (int32_t i = 0; i < polygon->count; ++i)
        {
            uint16_t from = polygon->vertices[i];
            uint16_t to = polygon->vertices[(i + 1) % polygon->count];
            for (uint32_t slot = EdgeHash(to, from) & mask; table[slot] >= 0;
                 slot = (slot + 1u) & mask)
            {
                int32_t k = table[slot] / MNAV_POLYGON_VERTICES;
                int32_t edge = table[slot] % MNAV_POLYGON_VERTICES;
                const mnavPolygon* other = &polygons[k];
                if (k > j && other->vertices[edge] == to &&
                    other->vertices[(edge + 1) % other->count] == from)
                {
                    Consider(vertices, polygons, j, k, &best);
                }
            }
        }
    }
    return best;
}

int32_t mnavMergePolygons(const mnavMeshVertex* vertices, mnavPolygon* polygons, int32_t count,
                          int32_t* table)
{
    for (;;)
    {
        uint32_t mask = (uint32_t)mnavMergeTableSize(count) - 1u;
        ListMergeEdges(polygons, count, table, mask);
        Best best = FindBest(vertices, polygons, count, table, mask);
        if (best.a < 0)
        {
            return count;
        }
        Merge(&polygons[best.a], &polygons[best.b], best.ea, best.eb);
        polygons[best.b] = polygons[count - 1];
        count -= 1;
    }
}

static void ReleaseRing(Builder* builder)
{
    mnavMemory* memory = builder->memory;
    int32_t old = builder->ringCapacity;
    mnavRelease(memory, builder->polygons, (size_t)old, sizeof(mnavPolygon), alignof(mnavPolygon));
    mnavRelease(memory, builder->triangles, (size_t)old * 3, sizeof(int32_t), alignof(int32_t));
    mnavRelease(memory, builder->ears.ears, (size_t)old, sizeof(uint8_t), alignof(uint8_t));
    mnavRelease(memory, builder->ears.indices, (size_t)old, sizeof(int32_t), alignof(int32_t));
    mnavRelease(memory, builder->welded, (size_t)old, sizeof(int32_t), alignof(int32_t));
    if (builder->mergeTable != nullptr)
    {
        mnavRelease(memory, builder->mergeTable, (size_t)mnavMergeTableSize(old), sizeof(int32_t),
                    alignof(int32_t));
    }
    builder->mergeTable = nullptr;
    builder->polygons = nullptr;
    builder->triangles = nullptr;
    builder->ears = (mnavEarScratch){nullptr, nullptr};
    builder->welded = nullptr;
    builder->ringCapacity = 0;
}

static mnavResult ReserveRing(Builder* builder, int32_t count)
{
    if (count <= builder->ringCapacity)
    {
        return mnav_success;
    }
    ReleaseRing(builder);
    mnavMemory* memory = builder->memory;
    size_t n = (size_t)count;
    mnavResult result =
        mnavAllocate(memory, n, sizeof(int32_t), alignof(int32_t), (void**)&builder->welded);
    if (result == mnav_success)
    {
        result = mnavAllocate(memory, n, sizeof(int32_t), alignof(int32_t),
                              (void**)&builder->ears.indices);
    }
    if (result == mnav_success)
    {
        result =
            mnavAllocate(memory, n, sizeof(uint8_t), alignof(uint8_t), (void**)&builder->ears.ears);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(memory, n * 3, sizeof(int32_t), alignof(int32_t),
                              (void**)&builder->triangles);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(memory, n, sizeof(mnavPolygon), alignof(mnavPolygon),
                              (void**)&builder->polygons);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(memory, (size_t)mnavMergeTableSize(count), sizeof(int32_t),
                              alignof(int32_t), (void**)&builder->mergeTable);
    }
    builder->ringCapacity = count;
    return result;
}

static mnavResult Store(Builder* builder, const mnavPolygon* polygon)
{
    mnavPolyMesh* mesh = builder->mesh;
    if (mesh->polygonCount >= builder->maxPolygons)
    {
        return mnav_errorLimit;
    }
    mnavResult result = mnavReserve(
        builder->memory, (void**)&mesh->polygons, &mesh->polygonCapacity, mesh->polygonCount,
        mesh->polygonCount + 1, sizeof(mnavPolygon), alignof(mnavPolygon));
    if (result == mnav_success)
    {
        mesh->polygons[mesh->polygonCount++] = *polygon;
    }
    return result;
}

// Twice the signed area of triangle abc on the ground; negative for the
// winding of the rings.
static int64_t TwiceArea(const mnavMeshVertex* v, int32_t a, int32_t b, int32_t c)
{
    int64_t abx = (int64_t)v[b].x - v[a].x;
    int64_t abz = (int64_t)v[b].z - v[a].z;
    int64_t acx = (int64_t)v[c].x - v[a].x;
    int64_t acz = (int64_t)v[c].z - v[a].z;
    return abx * acz - abz * acx;
}

// Triangulates one ring, welds its vertices, merges its triangles and
// stores the polygons.
static mnavResult AddRing(Builder* builder, const mnavContourSet* set, const mnavContour* contour)
{
    const mnavContourVertex* ring = set->vertices + contour->first;
    mnavResult result = ReserveRing(builder, contour->count);
    if (result != mnav_success)
    {
        return result;
    }
    bool complete = true;
    int32_t triangleCount =
        mnavTriangulate(ring, contour->count, builder->ears, builder->triangles, &complete);
    for (int32_t k = 0; k < contour->count && result == mnav_success; ++k)
    {
        result = Weld(builder, &ring[k], &builder->welded[k]);
        if (result == mnav_success && (ring[k].flags & MNAV_VERTEX_TILE_BORDER) != 0)
        {
            builder->mesh->removable[builder->welded[k]] = 1;
        }
    }
    int32_t count = 0;
    for (int32_t t = 0; t < triangleCount && result == mnav_success; ++t)
    {
        const int32_t* corner = &builder->triangles[t * 3];
        int32_t a = builder->welded[corner[0]];
        int32_t b = builder->welded[corner[1]];
        int32_t c = builder->welded[corner[2]];
        // A triangle with no area, as the last of a ring whose points lie
        // in a line, covers nothing and could never load; one turned the
        // wrong way means the ring was cut short.
        int64_t area = a == b || a == c || b == c ? 0 : TwiceArea(builder->mesh->vertices, a, b, c);
        if (area >= 0)
        {
            complete = complete && area == 0;
            continue;
        }
        mnavPolygon* polygon = &builder->polygons[count++];
        memset(polygon, 0xFF, sizeof(*polygon));
        polygon->vertices[0] = (uint16_t)a;
        polygon->vertices[1] = (uint16_t)b;
        polygon->vertices[2] = (uint16_t)c;
        memset(polygon->sides, 0, sizeof(polygon->sides));
        polygon->count = 3;
        polygon->area = contour->area;
        polygon->region = contour->region;
    }
    builder->mesh->failedRings += complete ? 0 : 1;
    if (result == mnav_success)
    {
        count = mnavMergePolygons(builder->mesh->vertices, builder->polygons, count,
                                  builder->mergeTable);
    }
    for (int32_t p = 0; p < count && result == mnav_success; ++p)
    {
        result = Store(builder, &builder->polygons[p]);
    }
    return result;
}

// An edge seen from its lower vertex: the polygons on each side and
// their edge numbers.
typedef struct Edge
{
    uint16_t high;
    uint16_t polygon[2];
    uint8_t edge[2];
    int32_t next;
} Edge;

static uint16_t EdgeEnd(const mnavPolygon* polygon, int32_t k)
{
    return polygon->vertices[(k + 1) % polygon->count];
}

// Lists every edge once, from its lower vertex, by the polygon that runs
// it upward. Returns the number of edges.
static int32_t ListEdges(const mnavPolyMesh* mesh, int32_t* first, Edge* edges)
{
    int32_t edgeCount = 0;
    for (int32_t p = 0; p < mesh->polygonCount; ++p)
    {
        const mnavPolygon* polygon = &mesh->polygons[p];
        for (int32_t k = 0; k < polygon->count; ++k)
        {
            uint16_t v0 = polygon->vertices[k];
            uint16_t v1 = EdgeEnd(polygon, k);
            if (v0 < v1)
            {
                edges[edgeCount] =
                    (Edge){v1, {(uint16_t)p, (uint16_t)p}, {(uint8_t)k, 0}, first[v0]};
                first[v0] = edgeCount++;
            }
        }
    }
    return edgeCount;
}

// The polygon that runs an edge downward finds it from the edge's lower
// vertex.
static void MatchEdges(const mnavPolyMesh* mesh, const int32_t* first, Edge* edges)
{
    for (int32_t p = 0; p < mesh->polygonCount; ++p)
    {
        const mnavPolygon* polygon = &mesh->polygons[p];
        for (int32_t k = 0; k < polygon->count; ++k)
        {
            uint16_t v0 = polygon->vertices[k];
            uint16_t v1 = EdgeEnd(polygon, k);
            for (int32_t e = v0 > v1 ? first[v1] : -1; e >= 0; e = edges[e].next)
            {
                if (edges[e].high == v0 && edges[e].polygon[0] == edges[e].polygon[1])
                {
                    edges[e].polygon[1] = (uint16_t)p;
                    edges[e].edge[1] = (uint8_t)k;
                    break;
                }
            }
        }
    }
}

// Links each polygon edge to the polygon across it (an edge list after
// Lengyel).
static mnavResult Link(mnavMemory* memory, mnavPolyMesh* mesh)
{
    size_t vertexCount = (size_t)mesh->vertexCount;
    size_t edgeCapacity = (size_t)mesh->polygonCount * MNAV_POLYGON_VERTICES;
    int32_t* first = nullptr;
    Edge* edges = nullptr;
    mnavResult result =
        mnavAllocate(memory, vertexCount, sizeof(int32_t), alignof(int32_t), (void**)&first);
    if (result == mnav_success)
    {
        result = mnavAllocate(memory, edgeCapacity, sizeof(Edge), alignof(Edge), (void**)&edges);
    }
    if (result == mnav_success)
    {
        for (size_t v = 0; v < vertexCount; ++v)
        {
            first[v] = -1;
        }
        for (int32_t p = 0; p < mesh->polygonCount; ++p)
        {
            memset(mesh->polygons[p].neighbors, 0xFF, sizeof(mesh->polygons[p].neighbors));
        }
        int32_t edgeCount = ListEdges(mesh, first, edges);
        MatchEdges(mesh, first, edges);
        for (int32_t e = 0; e < edgeCount; ++e)
        {
            const Edge* edge = &edges[e];
            if (edge->polygon[0] != edge->polygon[1])
            {
                mesh->polygons[edge->polygon[0]].neighbors[edge->edge[0]] = edge->polygon[1];
                mesh->polygons[edge->polygon[1]].neighbors[edge->edge[1]] = edge->polygon[0];
            }
        }
    }
    mnavRelease(memory, edges, edgeCapacity, sizeof(Edge), alignof(Edge));
    mnavRelease(memory, first, vertexCount, sizeof(int32_t), alignof(int32_t));
    return result;
}

// The tile side, 1 to 4, an edge from a to b lies on, or 0.
static uint8_t Side(const mnavMeshVertex* a, const mnavMeshVertex* b, int32_t size)
{
    if (a->x == 0 && b->x == 0)
    {
        return 1;
    }
    if (a->z == size && b->z == size)
    {
        return 2;
    }
    if (a->x == size && b->x == size)
    {
        return 3;
    }
    return a->z == 0 && b->z == 0 ? 4 : 0;
}

// Marks the unlinked edges that lie on the tile's sides.
static void MarkSides(mnavPolyMesh* mesh)
{
    for (int32_t p = 0; p < mesh->polygonCount; ++p)
    {
        mnavPolygon* polygon = &mesh->polygons[p];
        for (int32_t k = 0; k < polygon->count; ++k)
        {
            const mnavMeshVertex* a = &mesh->vertices[polygon->vertices[k]];
            const mnavMeshVertex* b = &mesh->vertices[EdgeEnd(polygon, k)];
            polygon->sides[k] =
                polygon->neighbors[k] == MNAV_NO_INDEX ? Side(a, b, mesh->tileCells) : 0;
        }
    }
}

mnavResult mnavLinkPolyMesh(mnavMemory* memory, mnavPolyMesh* mesh)
{
    mnavResult result = Link(memory, mesh);
    if (result == mnav_success)
    {
        MarkSides(mesh);
    }
    return result;
}

mnavResult mnavBuildPolyMesh(mnavMemory* memory, const mnavContourSet* set, int32_t tileCells,
                             int32_t maxVertices, int32_t maxPolygons, mnavPolyMesh* mesh)
{
    *mesh = (mnavPolyMesh){0};
    mesh->tileCells = tileCells;
    Builder builder = {0};
    builder.memory = memory;
    builder.mesh = mesh;
    builder.maxVertices = maxVertices;
    builder.maxPolygons = maxPolygons;
    mnavResult result =
        mnavAllocate(memory, BUCKETS, sizeof(int32_t), alignof(int32_t), (void**)&builder.buckets);
    if (result == mnav_success)
    {
        for (int32_t b = 0; b < BUCKETS; ++b)
        {
            builder.buckets[b] = -1;
        }
    }
    for (int32_t c = 0; c < set->count && result == mnav_success; ++c)
    {
        result = AddRing(&builder, set, &set->contours[c]);
    }
    ReleaseRing(&builder);
    mnavRelease(memory, builder.chain, (size_t)mesh->vertexCapacity, sizeof(int32_t),
                alignof(int32_t));
    mnavRelease(memory, builder.buckets, BUCKETS, sizeof(int32_t), alignof(int32_t));
    if (result != mnav_success)
    {
        mnavReleasePolyMesh(memory, mesh);
    }
    return result;
}

void mnavReleasePolyMesh(mnavMemory* memory, mnavPolyMesh* mesh)
{
    size_t vertices = (size_t)mesh->vertexCapacity;
    mnavRelease(memory, mesh->polygons, (size_t)mesh->polygonCapacity, sizeof(mnavPolygon),
                alignof(mnavPolygon));
    mnavRelease(memory, mesh->removable, vertices, sizeof(uint8_t), alignof(uint8_t));
    mnavRelease(memory, mesh->vertices, vertices, sizeof(mnavMeshVertex), alignof(mnavMeshVertex));
    *mesh = (mnavPolyMesh){0};
}
