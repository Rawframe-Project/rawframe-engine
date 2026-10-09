// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The tile format and its loader.

#include "tile.h"

#include "allocator.h"
#include "bytes.h"
#include "detail.h"
#include "polymesh.h"

#include "maul-nav/bake.h"
#include "maul-nav/base.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SUBCELLS 16
// The bytes of a vertex, a polygon's head and edge, a detail part, a
// detail vertex and a detail triangle.
#define VERTEX_BYTES   6
#define POLYGON_BYTES  2
#define EDGE_BYTES     5
#define PART_BYTES     2
#define TRIANGLE_BYTES 4

static const uint8_t MAGIC[4] = {'M', 'N', 'A', 'V'};

static size_t PayloadBytes(const mnavPolyMesh* mesh, const mnavDetailMesh* detail)
{
    size_t bytes = (size_t)mesh->vertexCount * VERTEX_BYTES;
    size_t extra = 0;
    for (int32_t p = 0; p < mesh->polygonCount; ++p)
    {
        bytes += POLYGON_BYTES + (size_t)mesh->polygons[p].count * EDGE_BYTES + PART_BYTES;
        extra += (size_t)(detail->parts[p].vertexCount - mesh->polygons[p].count);
    }
    return bytes + extra * VERTEX_BYTES + (size_t)detail->triangleCount * TRIANGLE_BYTES;
}

static void WritePayload(mnavByteWriter* w, const mnavPolyMesh* mesh, const mnavDetailMesh* detail)
{
    for (int32_t v = 0; v < mesh->vertexCount; ++v)
    {
        mnavPutU16(w, mesh->vertices[v].x);
        mnavPutU16(w, mesh->vertices[v].y);
        mnavPutU16(w, mesh->vertices[v].z);
    }
    for (int32_t p = 0; p < mesh->polygonCount; ++p)
    {
        const mnavPolygon* polygon = &mesh->polygons[p];
        mnavPutU8(w, polygon->count);
        mnavPutU8(w, polygon->area);
        for (int32_t k = 0; k < polygon->count; ++k)
        {
            mnavPutU16(w, polygon->vertices[k]);
            mnavPutU16(w, polygon->neighbors[k]);
            mnavPutU8(w, polygon->sides[k]);
        }
    }
    for (int32_t p = 0; p < mesh->polygonCount; ++p)
    {
        mnavPutU8(w, (uint64_t)(detail->parts[p].vertexCount - mesh->polygons[p].count));
        mnavPutU8(w, detail->parts[p].triangleCount);
    }
    for (int32_t p = 0; p < mesh->polygonCount; ++p)
    {
        const mnavDetailPart* part = &detail->parts[p];
        for (int32_t v = mesh->polygons[p].count; v < part->vertexCount; ++v)
        {
            const mnavDetailVertex* d = &detail->vertices[part->firstVertex + v];
            mnavPutU16(w, (uint64_t)d->x);
            mnavPutU16(w, (uint64_t)d->y);
            mnavPutU16(w, (uint64_t)d->z);
        }
    }
    for (int32_t t = 0; t < detail->triangleCount; ++t)
    {
        const mnavDetailTriangle* triangle = &detail->triangles[t];
        mnavPutU8(w, triangle->corners[0]);
        mnavPutU8(w, triangle->corners[1]);
        mnavPutU8(w, triangle->corners[2]);
        mnavPutU8(w, triangle->outline);
    }
}

static void WriteHeader(mnavByteWriter* w, const mnavTileInfo* info, const mnavPolyMesh* mesh,
                        const mnavDetailMesh* detail, size_t payload, uint64_t hash)
{
    int32_t extra = detail->vertexCount;
    for (int32_t p = 0; p < mesh->polygonCount; ++p)
    {
        extra -= mesh->polygons[p].count;
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        mnavPutU8(w, MAGIC[k]);
    }
    mnavPutU16(w, MNAV_TILE_FORMAT);
    mnavPutU16(w, MNAV_TILE_HEADER_BYTES);
    mnavPutU16(w, info->generator.major);
    mnavPutU16(w, info->generator.minor);
    mnavPutU16(w, info->generator.patch);
    mnavPutU16(w, 0);
    mnavPutU64(w, info->fingerprint);
    mnavPutU64(w, hash);
    mnavPutU32(w, payload);
    mnavPutU32(w, (uint32_t)info->x);
    mnavPutU32(w, (uint32_t)info->z);
    mnavPutU16(w, (uint64_t)info->tileCells);
    mnavPutU16(w, 0);
    mnavPutU32(w, mnavFloatBits(info->cellSize));
    mnavPutU32(w, mnavFloatBits(info->cellHeight));
    mnavPutU16(w, (uint64_t)info->agentHeight);
    mnavPutU16(w, (uint64_t)info->agentRadius);
    mnavPutU16(w, (uint64_t)info->agentStep);
    mnavPutU16(w, 0);
    mnavPutU64(w, mnavDoubleBits(info->origin.x));
    mnavPutU64(w, mnavDoubleBits(info->origin.y));
    mnavPutU64(w, mnavDoubleBits(info->origin.z));
    mnavPutU16(w, (uint64_t)mesh->vertexCount);
    mnavPutU16(w, (uint64_t)mesh->polygonCount);
    mnavPutU32(w, (uint64_t)extra);
    mnavPutU32(w, (uint64_t)detail->triangleCount);
    mnavPutU32(w, 0);
}

mnavResult mnavEncodeTile(mnavMemory* memory, const mnavTileInfo* info, const mnavPolyMesh* mesh,
                          const mnavDetailMesh* detail, uint8_t** bytes, size_t* size)
{
    *bytes = nullptr;
    *size = 0;
    size_t payload = PayloadBytes(mesh, detail);
    size_t total = MNAV_TILE_HEADER_BYTES + payload;
    uint8_t* out = nullptr;
    mnavResult result = mnavAllocate(memory, total, 1, 1, (void**)&out);
    if (result != mnav_success)
    {
        return result;
    }
    mnavByteWriter w = {out + MNAV_TILE_HEADER_BYTES};
    WritePayload(&w, mesh, detail);
    uint64_t hash = mnavHash64(MNAV_HASH_INIT, out + MNAV_TILE_HEADER_BYTES, (int32_t)payload);
    w.at = out;
    WriteHeader(&w, info, mesh, detail, payload, hash);
    *bytes = out;
    *size = total;
    return mnav_success;
}

void mnavReleaseTileBytes(mnavMemory* memory, uint8_t* bytes, size_t size)
{
    mnavRelease(memory, bytes, size, 1, 1);
}

static mnavTileResult Refuse(mnavResult result, mnavTileSection section, int32_t index)
{
    return (mnavTileResult){result, section, index};
}

static mnavTileResult Accept(void)
{
    return (mnavTileResult){mnav_success, mnav_tileHeader, -1};
}

// The counts the header gives the payload.
typedef struct Counts
{
    int32_t vertices;
    int32_t polygons;
    int64_t detailVertices;
    int64_t detailTriangles;
    size_t payload;
} Counts;

static bool Finite(double v)
{
    return isfinite(v);
}

// Checks the settings the header records.
static bool ValidInfo(const mnavTileInfo* info)
{
    int32_t reach = MNAV_MAX_EXTENT_CELLS / (info->tileCells > 0 ? info->tileCells : 1);
    bool grid = info->tileCells >= MNAV_MIN_TILE_CELLS && info->tileCells <= MNAV_MAX_TILE_CELLS &&
                info->x >= -reach && info->x <= reach && info->z >= -reach && info->z <= reach;
    bool cells = isfinite(info->cellSize) && info->cellSize >= MNAV_MIN_CELL_SIZE &&
                 info->cellSize <= MNAV_MAX_CELL_SIZE && isfinite(info->cellHeight) &&
                 info->cellHeight >= MNAV_MIN_CELL_SIZE && info->cellHeight <= MNAV_MAX_CELL_SIZE;
    bool agent = info->agentHeight >= 1 && info->agentHeight <= MNAV_MAX_HEIGHT_CELLS &&
                 info->agentStep <= MNAV_MAX_HEIGHT_CELLS && info->agentRadius <= info->tileCells;
    bool origin = Finite(info->origin.x) && Finite(info->origin.y) && Finite(info->origin.z);
    return grid && cells && agent && origin;
}

static mnavTileResult ReadHeader(const uint8_t* bytes, size_t size, mnavTileInfo* info,
                                 Counts* counts)
{
    if (size < MNAV_TILE_HEADER_BYTES || memcmp(bytes, MAGIC, sizeof(MAGIC)) != 0)
    {
        return Refuse(mnav_errorInvalid, mnav_tileHeader, -1);
    }
    mnavByteReader r = {bytes + sizeof(MAGIC), MNAV_TILE_HEADER_BYTES - sizeof(MAGIC), false};
    if (mnavGetU16(&r) != MNAV_TILE_FORMAT)
    {
        return Refuse(mnav_errorVersion, mnav_tileHeader, -1);
    }
    uint64_t headerBytes = mnavGetU16(&r);
    info->generator.major = (uint16_t)mnavGetU16(&r);
    info->generator.minor = (uint16_t)mnavGetU16(&r);
    info->generator.patch = (uint16_t)mnavGetU16(&r);
    uint64_t reserved = mnavGetU16(&r);
    info->fingerprint = mnavGetU64(&r);
    uint64_t hash = mnavGetU64(&r);
    counts->payload = (size_t)mnavGetU32(&r);
    info->x = (int32_t)(uint32_t)mnavGetU32(&r);
    info->z = (int32_t)(uint32_t)mnavGetU32(&r);
    info->tileCells = (int32_t)mnavGetU16(&r);
    reserved |= mnavGetU16(&r);
    uint32_t cellSize = (uint32_t)mnavGetU32(&r);
    uint32_t cellHeight = (uint32_t)mnavGetU32(&r);
    memcpy(&info->cellSize, &cellSize, sizeof(cellSize));
    memcpy(&info->cellHeight, &cellHeight, sizeof(cellHeight));
    info->agentHeight = (int32_t)mnavGetU16(&r);
    info->agentRadius = (int32_t)mnavGetU16(&r);
    info->agentStep = (int32_t)mnavGetU16(&r);
    reserved |= mnavGetU16(&r);
    double* origin[3] = {&info->origin.x, &info->origin.y, &info->origin.z};
    for (int32_t k = 0; k < 3; ++k)
    {
        uint64_t bits = mnavGetU64(&r);
        memcpy(origin[k], &bits, sizeof(bits));
    }
    counts->vertices = (int32_t)mnavGetU16(&r);
    counts->polygons = (int32_t)mnavGetU16(&r);
    counts->detailVertices = (int64_t)mnavGetU32(&r);
    counts->detailTriangles = (int64_t)mnavGetU32(&r);
    reserved |= mnavGetU32(&r);
    if (headerBytes != MNAV_TILE_HEADER_BYTES || reserved != 0 || !ValidInfo(info))
    {
        return Refuse(mnav_errorInvalid, mnav_tileHeader, -1);
    }
    // The payload's hash takes a 32-bit size; no tile within the caps
    // comes near it.
    if (counts->payload != size - MNAV_TILE_HEADER_BYTES || counts->payload > INT32_MAX ||
        mnavHash64(MNAV_HASH_INIT, bytes + MNAV_TILE_HEADER_BYTES, (int32_t)counts->payload) !=
            hash)
    {
        return Refuse(mnav_errorInvalid, mnav_tilePayload, -1);
    }
    return Accept();
}

// Whether the payload can hold what the header counts, at the fewest
// bytes each takes, so nothing larger than the input is allocated.
static bool Fits(const Counts* counts)
{
    int64_t least = (int64_t)counts->vertices * VERTEX_BYTES +
                    (int64_t)counts->polygons * (POLYGON_BYTES + 3 * EDGE_BYTES + PART_BYTES) +
                    counts->detailVertices * VERTEX_BYTES +
                    counts->detailTriangles * TRIANGLE_BYTES;
    return least <= (int64_t)counts->payload && counts->detailTriangles >= counts->polygons;
}

static mnavResult AllocateMesh(mnavMemory* memory, const Counts* counts, int32_t tileCells,
                               mnavPolyMesh* mesh, mnavDetailMesh* detail)
{
    size_t vertices = (size_t)counts->vertices;
    size_t polygons = (size_t)counts->polygons;
    mesh->tileCells = tileCells;
    mnavResult result = mnavAllocate(memory, vertices, sizeof(mnavMeshVertex),
                                     alignof(mnavMeshVertex), (void**)&mesh->vertices);
    if (result == mnav_success)
    {
        mesh->vertexCapacity = counts->vertices;
        result = mnavAllocate(memory, vertices, sizeof(uint8_t), alignof(uint8_t),
                              (void**)&mesh->removable);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(memory, polygons, sizeof(mnavPolygon), alignof(mnavPolygon),
                              (void**)&mesh->polygons);
    }
    if (result == mnav_success)
    {
        mesh->polygonCapacity = counts->polygons;
        result = mnavAllocate(memory, polygons, sizeof(mnavDetailPart), alignof(mnavDetailPart),
                              (void**)&detail->parts);
    }
    if (result == mnav_success)
    {
        detail->partCount = counts->polygons;
        if (vertices > 0)
        {
            memset(mesh->removable, 0, vertices);
        }
    }
    return result;
}

static mnavTileResult ReadVertices(mnavByteReader* r, mnavPolyMesh* mesh, int32_t count)
{
    for (int32_t v = 0; v < count; ++v)
    {
        mnavMeshVertex* vertex = &mesh->vertices[v];
        vertex->x = (uint16_t)mnavGetU16(r);
        vertex->y = (uint16_t)mnavGetU16(r);
        vertex->z = (uint16_t)mnavGetU16(r);
        if (r->exhausted || vertex->x > mesh->tileCells || vertex->z > mesh->tileCells)
        {
            return Refuse(mnav_errorInvalid, mnav_tileVertices, v);
        }
        mesh->vertexCount += 1;
    }
    return Accept();
}

// Reads one polygon's head and edges and checks each value alone.
static bool ReadPolygon(mnavByteReader* r, const mnavPolyMesh* mesh, int32_t polygons,
                        mnavPolygon* polygon)
{
    *polygon = (mnavPolygon){0};
    memset(polygon->vertices, 0xFF, sizeof(polygon->vertices));
    memset(polygon->neighbors, 0xFF, sizeof(polygon->neighbors));
    uint64_t count = mnavGetU8(r);
    uint64_t area = mnavGetU8(r);
    if (count < 3 || count > MNAV_POLYGON_VERTICES || area == mnav_areaNone ||
        area >= MNAV_AREA_TYPES)
    {
        return false;
    }
    polygon->count = (uint8_t)count;
    polygon->area = (mnavAreaType)area;
    for (int32_t k = 0; k < polygon->count; ++k)
    {
        uint64_t vertex = mnavGetU16(r);
        uint64_t neighbor = mnavGetU16(r);
        uint64_t side = mnavGetU8(r);
        bool linked = neighbor == MNAV_NO_INDEX || neighbor < (uint64_t)polygons;
        if (vertex >= (uint64_t)mesh->vertexCount || !linked || side > 4)
        {
            return false;
        }
        polygon->vertices[k] = (uint16_t)vertex;
        polygon->neighbors[k] = (uint16_t)neighbor;
        polygon->sides[k] = (uint8_t)side;
    }
    return !r->exhausted;
}

static int64_t Area2(const mnavMeshVertex* a, const mnavMeshVertex* b, const mnavMeshVertex* c)
{
    return (int64_t)(b->x - a->x) * (c->z - a->z) - (int64_t)(c->x - a->x) * (b->z - a->z);
}

// Whether the polygon is convex with a positive area: no edge of zero
// length, and every vertex on the inner side of every edge or on it. A
// vertex used twice fails it too, as the edges round it double back.
static bool Convex(const mnavPolyMesh* mesh, const mnavPolygon* polygon)
{
    int64_t area = 0;
    for (int32_t k = 0; k < polygon->count; ++k)
    {
        const mnavMeshVertex* a = &mesh->vertices[polygon->vertices[k]];
        const mnavMeshVertex* b = &mesh->vertices[polygon->vertices[(k + 1) % polygon->count]];
        if (a->x == b->x && a->z == b->z)
        {
            return false;
        }
        for (int32_t j = 0; j < polygon->count; ++j)
        {
            if (Area2(a, b, &mesh->vertices[polygon->vertices[j]]) > 0)
            {
                return false;
            }
        }
        area += (int64_t)a->x * b->z - (int64_t)b->x * a->z;
    }
    // Wound like the rings, a polygon's area by this sum is negative.
    return area < 0;
}

// The tile side, 1 to 4, an edge from a to b lies on, or 0.
static uint8_t SideOf(const mnavMeshVertex* a, const mnavMeshVertex* b, int32_t size)
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

// Whether each edge's neighbor runs the same edge back to it, and each
// edge without one carries exactly the tile side it lies on. A polygon
// naming itself fails, as it never runs its own edge backward.
static bool Linked(const mnavPolyMesh* mesh, int32_t p)
{
    const mnavPolygon* polygon = &mesh->polygons[p];
    for (int32_t k = 0; k < polygon->count; ++k)
    {
        uint16_t a = polygon->vertices[k];
        uint16_t b = polygon->vertices[(k + 1) % polygon->count];
        uint16_t n = polygon->neighbors[k];
        if (n == MNAV_NO_INDEX)
        {
            if (polygon->sides[k] !=
                SideOf(&mesh->vertices[a], &mesh->vertices[b], mesh->tileCells))
            {
                return false;
            }
            continue;
        }
        const mnavPolygon* other = &mesh->polygons[n];
        bool back = false;
        for (int32_t j = 0; j < other->count; ++j)
        {
            back =
                back || (other->vertices[j] == b && other->vertices[(j + 1) % other->count] == a &&
                         other->neighbors[j] == p);
        }
        if (polygon->sides[k] != 0 || !back)
        {
            return false;
        }
    }
    return true;
}

static mnavTileResult ReadPolygons(mnavByteReader* r, mnavPolyMesh* mesh, int32_t count)
{
    for (int32_t p = 0; p < count; ++p)
    {
        if (!ReadPolygon(r, mesh, count, &mesh->polygons[p]))
        {
            return Refuse(mnav_errorInvalid, mnav_tilePolygons, p);
        }
        mesh->polygonCount += 1;
    }
    for (int32_t p = 0; p < count; ++p)
    {
        if (!Convex(mesh, &mesh->polygons[p]) || !Linked(mesh, p))
        {
            return Refuse(mnav_errorInvalid, mnav_tilePolygons, p);
        }
    }
    return Accept();
}

// Reads each polygon's detail counts; the offsets are their sums, which
// must match the header.
static mnavTileResult ReadParts(mnavByteReader* r, const mnavPolyMesh* mesh, const Counts* counts,
                                mnavDetailMesh* detail)
{
    int64_t vertices = 0;
    int64_t extra = 0;
    int64_t triangles = 0;
    for (int32_t p = 0; p < mesh->polygonCount; ++p)
    {
        int64_t count = mesh->polygons[p].count + (int64_t)mnavGetU8(r);
        int64_t parts = (int64_t)mnavGetU8(r);
        if (r->exhausted || count > MNAV_DETAIL_VERTICES || parts < 1 ||
            parts > 2 * MNAV_DETAIL_VERTICES)
        {
            return Refuse(mnav_errorInvalid, mnav_tileDetailParts, p);
        }
        detail->parts[p] =
            (mnavDetailPart){(int32_t)vertices, (int32_t)triangles, (uint8_t)count, (uint8_t)parts};
        vertices += count;
        extra += count - mesh->polygons[p].count;
        triangles += parts;
    }
    if (extra != counts->detailVertices || triangles != counts->detailTriangles)
    {
        return Refuse(mnav_errorInvalid, mnav_tileDetailParts, -1);
    }
    return Accept();
}

// Reads the vertices beyond each polygon's own, which come first in each
// part from the polygon's vertices.
static mnavTileResult ReadDetailVertices(mnavMemory* memory, mnavByteReader* r,
                                         const mnavPolyMesh* mesh, mnavDetailMesh* detail)
{
    int32_t total = 0;
    for (int32_t p = 0; p < mesh->polygonCount; ++p)
    {
        total += detail->parts[p].vertexCount;
    }
    mnavResult result = mnavAllocate(memory, (size_t)total, sizeof(mnavDetailVertex),
                                     alignof(mnavDetailVertex), (void**)&detail->vertices);
    if (result != mnav_success)
    {
        return Refuse(result, mnav_tileDetailVertices, -1);
    }
    detail->vertexCapacity = total;
    int32_t limit = mesh->tileCells * SUBCELLS;
    int32_t extra = 0;
    for (int32_t p = 0; p < mesh->polygonCount; ++p)
    {
        const mnavPolygon* polygon = &mesh->polygons[p];
        mnavDetailVertex* out = &detail->vertices[detail->parts[p].firstVertex];
        for (int32_t k = 0; k < polygon->count; ++k)
        {
            const mnavMeshVertex* v = &mesh->vertices[polygon->vertices[k]];
            out[k] = (mnavDetailVertex){v->x * SUBCELLS, v->y, v->z * SUBCELLS};
        }
        for (int32_t k = polygon->count; k < detail->parts[p].vertexCount; ++k)
        {
            out[k].x = (int32_t)mnavGetU16(r);
            out[k].y = (int32_t)mnavGetU16(r);
            out[k].z = (int32_t)mnavGetU16(r);
            if (r->exhausted || out[k].x > limit || out[k].z > limit)
            {
                return Refuse(mnav_errorInvalid, mnav_tileDetailVertices, extra);
            }
            extra += 1;
        }
    }
    detail->vertexCount = total;
    return Accept();
}

static mnavTileResult ReadDetailTriangles(mnavMemory* memory, mnavByteReader* r,
                                          const Counts* counts, mnavDetailMesh* detail)
{
    size_t count = (size_t)counts->detailTriangles;
    mnavResult result = mnavAllocate(memory, count, sizeof(mnavDetailTriangle),
                                     alignof(mnavDetailTriangle), (void**)&detail->triangles);
    if (result != mnav_success)
    {
        return Refuse(result, mnav_tileDetailTriangles, -1);
    }
    detail->triangleCapacity = (int32_t)count;
    int32_t t = 0;
    for (int32_t p = 0; p < detail->partCount; ++p)
    {
        const mnavDetailPart* part = &detail->parts[p];
        for (int32_t k = 0; k < part->triangleCount; ++k, ++t)
        {
            mnavDetailTriangle* triangle = &detail->triangles[t];
            bool inside = true;
            for (int32_t c = 0; c < 3; ++c)
            {
                triangle->corners[c] = (uint8_t)mnavGetU8(r);
                inside = inside && triangle->corners[c] < part->vertexCount;
            }
            triangle->outline = (uint8_t)mnavGetU8(r);
            if (r->exhausted || !inside || triangle->outline > 7)
            {
                return Refuse(mnav_errorInvalid, mnav_tileDetailTriangles, t);
            }
        }
    }
    detail->triangleCount = t;
    return Accept();
}

// Reads the payload's sections in order, each checked before the next.
static mnavTileResult ReadPayload(mnavMemory* memory, mnavByteReader* r, const Counts* counts,
                                  mnavPolyMesh* mesh, mnavDetailMesh* detail)
{
    mnavTileResult result = ReadVertices(r, mesh, counts->vertices);
    if (result.result == mnav_success)
    {
        result = ReadPolygons(r, mesh, counts->polygons);
    }
    if (result.result == mnav_success)
    {
        result = ReadParts(r, mesh, counts, detail);
    }
    if (result.result == mnav_success)
    {
        result = ReadDetailVertices(memory, r, mesh, detail);
    }
    if (result.result == mnav_success)
    {
        result = ReadDetailTriangles(memory, r, counts, detail);
    }
    if (result.result == mnav_success && r->left != 0)
    {
        result = Refuse(mnav_errorInvalid, mnav_tilePayload, -1);
    }
    return result;
}

mnavTileResult mnavDecodeTile(mnavMemory* memory, const uint8_t* bytes, size_t size,
                              mnavTileInfo* info, mnavPolyMesh* mesh, mnavDetailMesh* detail)
{
    *info = (mnavTileInfo){0};
    *mesh = (mnavPolyMesh){0};
    *detail = (mnavDetailMesh){0};
    if (bytes == nullptr)
    {
        return Refuse(mnav_errorInvalid, mnav_tileHeader, -1);
    }
    Counts counts = {0};
    mnavTileResult result = ReadHeader(bytes, size, info, &counts);
    if (result.result == mnav_success && !Fits(&counts))
    {
        result = Refuse(mnav_errorInvalid, mnav_tilePayload, -1);
    }
    if (result.result == mnav_success)
    {
        mnavResult allocated = AllocateMesh(memory, &counts, info->tileCells, mesh, detail);
        result = allocated == mnav_success ? result : Refuse(allocated, mnav_tileHeader, -1);
    }
    if (result.result == mnav_success)
    {
        mnavByteReader r = {bytes + MNAV_TILE_HEADER_BYTES, counts.payload, false};
        result = ReadPayload(memory, &r, &counts, mesh, detail);
    }
    if (result.result != mnav_success)
    {
        mnavReleaseDetailMesh(memory, detail);
        mnavReleasePolyMesh(memory, mesh);
        *info = (mnavTileInfo){0};
    }
    return result;
}
