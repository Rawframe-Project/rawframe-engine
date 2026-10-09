// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A tile's detail mesh.

#include "detail.h"

#include "allocator.h"
#include "compact.h"
#include "contour.h"
#include "delaunay.h"
#include "height_patch.h"
#include "polymesh.h"
#include "region.h"
#include "triangulate.h"

#include "maul-nav/bake.h"

#include <stdint.h>
#include <string.h>

// Sixteenths of a cell per cell.
#define SUBCELLS 16
// The most points on one edge: its two ends and the samples between.
#define EDGE_POINTS    (MNAV_DETAIL_EDGE_SAMPLES + 2)
#define PART_TRIANGLES (2 * MNAV_DETAIL_VERTICES)

// One polygon's detail while it is built, and the triangulation's scratch.
typedef struct Part
{
    mnavDetailVertex vertices[MNAV_DETAIL_VERTICES];
    int32_t vertexCount;
    // The outline, as indices into vertices in polygon order.
    uint8_t hull[MNAV_DETAIL_VERTICES];
    int32_t hullCount;
    mnavDetailTriangle triangles[PART_TRIANGLES];
    int32_t triangleCount;
    mnavContourVertex ring[MNAV_DETAIL_VERTICES];
    int32_t indices[MNAV_DETAIL_VERTICES];
    uint8_t ears[MNAV_DETAIL_VERTICES];
    int32_t corners[3 * MNAV_DETAIL_VERTICES];
    // An edge's points and those kept.
    mnavDetailVertex points[EDGE_POINTS];
    int32_t kept[EDGE_POINTS];
    // The flips' stack: each flip adds one edge, and an insertion flips
    // at most once per triangle.
    int32_t stack[PART_TRIANGLES + 8];
    // The triangles an insertion wrote.
    uint8_t changed[PART_TRIANGLES];
} Part;

// A grid sample inside a polygon: the triangle holding it (-1 for none)
// and the edge it lies on, its height off the detail surface in whole
// sixteenths of a cell height when beyond the maximum error (-1 when
// within), and whether it is in the detail.
typedef struct Candidate
{
    mnavDetailVertex point;
    int32_t triangle;
    int32_t edge;
    int64_t value;
    bool added;
} Candidate;

typedef struct Builder
{
    mnavMemory* memory;
    const mnavCompactField* field;
    const mnavRegionMap* regions;
    const mnavPolyMesh* mesh;
    mnavDetailSettings settings;
    mnavDetailMesh* detail;
    mnavHeightPatch patch;
    Part* part;
    Candidate* candidates;
    int32_t candidateCount;
    int32_t candidateCapacity;
} Builder;

// The integer square root, rounded down, of v from 0 to 2^62.
static int64_t SquareRoot(int64_t v)
{
    int64_t root = 0;
    for (int64_t bit = (int64_t)1 << 30; bit > 0; bit >>= 1)
    {
        if ((root + bit) * (root + bit) <= v)
        {
            root += bit;
        }
    }
    return root;
}

// n / d to the nearest integer, ties away from zero; d is positive.
static int64_t RoundDivide(int64_t n, int64_t d)
{
    return n >= 0 ? (n + d / 2) / d : -((-n + d / 2) / d);
}

// The floor's height at a point in sixteenths, near reference; the
// reference itself, counted, when the patch has none within the radius.
// Detail positions are never negative, as mesh vertices are not.
static int32_t Height(Builder* builder, int32_t x, int32_t z, int32_t reference)
{
    uint16_t h = 0;
    if (mnavPatchHeight(&builder->patch, x / SUBCELLS, z / SUBCELLS, reference,
                        builder->settings.radius, &h))
    {
        return h;
    }
    builder->detail->fallbackHeights += 1;
    return reference;
}

// Places the points of the edge from a to b at most a sample distance
// apart, at most MNAV_DETAIL_EDGE_SAMPLES between the ends, with their
// heights.
// Returns the number of segments.
static int32_t PlacePoints(Builder* builder, const mnavDetailVertex* a, const mnavDetailVertex* b)
{
    Part* part = builder->part;
    int64_t dx = b->x - a->x;
    int64_t dz = b->z - a->z;
    int64_t dy = b->y - a->y;
    int64_t segments = 1;
    int64_t sample = builder->settings.sample;
    if (sample > 0)
    {
        // The fewest segments no longer than the sample distance: the
        // smallest k with (k * sample)^2 at least the length squared.
        int64_t length2 = dx * dx + dz * dz;
        segments = (SquareRoot(length2) + sample - 1) / sample;
        segments += segments * sample * segments * sample < length2 ? 1 : 0;
        segments = segments < 1 ? 1 : segments;
        segments = segments > EDGE_POINTS - 1 ? EDGE_POINTS - 1 : segments;
    }
    for (int64_t k = 0; k <= segments; ++k)
    {
        mnavDetailVertex* p = &part->points[k];
        p->x = a->x + (int32_t)RoundDivide(dx * k, segments);
        p->z = a->z + (int32_t)RoundDivide(dz * k, segments);
        int32_t reference = a->y + (int32_t)RoundDivide(dy * k, segments);
        bool end = k == 0 || k == segments;
        p->y = end ? reference : Height(builder, p->x, p->z, reference);
    }
    return (int32_t)segments;
}

// Keeps the ends and, between two kept points, the point whose height
// strays farthest from the line between them while that is more than the
// maximum error, the first on ties. Returns the number kept, in order.
static int32_t Simplify(const Part* part, int32_t segments, int32_t error, int32_t* kept)
{
    kept[0] = 0;
    kept[1] = segments;
    int32_t count = 2;
    for (int32_t k = 0; k < count - 1;)
    {
        int32_t a = kept[k];
        int32_t b = kept[k + 1];
        int64_t ya = part->points[a].y;
        int64_t yb = part->points[b].y;
        int64_t worst = 0;
        int32_t worstAt = -1;
        for (int32_t m = a + 1; m < b; ++m)
        {
            // The height off the line, times b - a.
            int64_t off = part->points[m].y * (int64_t)(b - a) - (ya * (b - m) + yb * (m - a));
            off = off < 0 ? -off : off;
            if (off > worst)
            {
                worst = off;
                worstAt = m;
            }
        }
        if (worstAt >= 0 && worst * SUBCELLS > (int64_t)error * (b - a))
        {
            memmove(&kept[k + 2], &kept[k + 1], (size_t)(count - k - 1) * sizeof(int32_t));
            kept[k + 1] = worstAt;
            count += 1;
        }
        else
        {
            k += 1;
        }
    }
    return count;
}

// Whether a comes after b in lexical (x, z) order.
static bool After(const mnavDetailVertex* a, const mnavDetailVertex* b)
{
    return a->x != b->x ? a->x > b->x : a->z > b->z;
}

// Adds the polygon's vertices and, edge by edge, the samples kept on it,
// building the outline in polygon order. Each edge is sampled from its
// lexically first end, so both polygons sharing it get the same points.
static void BuildOutline(Builder* builder, const mnavPolygon* polygon)
{
    Part* part = builder->part;
    int32_t n = polygon->count;
    for (int32_t k = 0; k < n; ++k)
    {
        const mnavMeshVertex* v = &builder->mesh->vertices[polygon->vertices[k]];
        part->vertices[k] = (mnavDetailVertex){v->x * SUBCELLS, v->y, v->z * SUBCELLS};
    }
    part->vertexCount = n;
    part->hullCount = 0;
    for (int32_t j = 0; j < n; ++j)
    {
        int32_t i = (j + 1) % n;
        bool swapped = After(&part->vertices[j], &part->vertices[i]);
        const mnavDetailVertex* a = &part->vertices[swapped ? i : j];
        const mnavDetailVertex* b = &part->vertices[swapped ? j : i];
        int32_t segments = PlacePoints(builder, a, b);
        int32_t kept = Simplify(part, segments, builder->settings.error, part->kept);
        part->hull[part->hullCount++] = (uint8_t)j;
        for (int32_t k = 1; k < kept - 1; ++k)
        {
            int32_t point = part->kept[swapped ? kept - 1 - k : k];
            part->vertices[part->vertexCount] = part->points[point];
            part->hull[part->hullCount++] = (uint8_t)part->vertexCount;
            part->vertexCount += 1;
        }
    }
}

// Whether the edge between vertices u and v runs along the outline.
static bool OnOutline(const Part* part, int32_t u, int32_t v)
{
    for (int32_t h = 0; h < part->hullCount; ++h)
    {
        int32_t next = part->hull[(h + 1) % part->hullCount];
        if ((part->hull[h] == u && next == v) || (part->hull[h] == v && next == u))
        {
            return true;
        }
    }
    return false;
}

// Triangulates the outline by ear clipping.
static void TriangulateOutline(Builder* builder)
{
    Part* part = builder->part;
    for (int32_t h = 0; h < part->hullCount; ++h)
    {
        const mnavDetailVertex* v = &part->vertices[part->hull[h]];
        part->ring[h] = (mnavContourVertex){v->x, v->y, v->z, 0, 0};
    }
    bool complete = true;
    mnavEarScratch scratch = {part->indices, part->ears};
    int32_t count = mnavTriangulate(part->ring, part->hullCount, scratch, part->corners, &complete);
    builder->detail->failedPolygons += complete ? 0 : 1;
    part->triangleCount = 0;
    for (int32_t t = 0; t < count; ++t)
    {
        mnavDetailTriangle* triangle = &part->triangles[part->triangleCount++];
        for (int32_t c = 0; c < 3; ++c)
        {
            triangle->corners[c] = part->hull[part->corners[t * 3 + c]];
        }
    }
}

// Flags each triangle's edges that run along the outline.
static void MarkOutline(Part* part)
{
    for (int32_t t = 0; t < part->triangleCount; ++t)
    {
        mnavDetailTriangle* triangle = &part->triangles[t];
        triangle->outline = 0;
        for (int32_t c = 0; c < 3; ++c)
        {
            bool on = OnOutline(part, triangle->corners[c], triangle->corners[(c + 1) % 3]);
            triangle->outline |= (uint8_t)(on ? 1u << c : 0u);
        }
    }
}

// Whether p lies inside the polygon's first n vertices at least half a
// sample distance from every edge: strictly on each edge's inner side,
// with 4 area^2 at least sample^2 length^2.
static bool WellInside(const Part* part, int32_t n, const mnavDetailVertex* p, int64_t sample)
{
    for (int32_t j = n - 1, i = 0; i < n; j = i++)
    {
        const mnavDetailVertex* a = &part->vertices[j];
        const mnavDetailVertex* b = &part->vertices[i];
        int64_t area = mnavDetailArea2(a, b, p);
        int64_t dx = b->x - a->x;
        int64_t dz = b->z - a->z;
        if (area >= 0 || 4 * area * area < sample * sample * (dx * dx + dz * dz))
        {
            return false;
        }
    }
    return true;
}

// Lists the grid points, every sample distance from the tile's corner,
// well inside the polygon, with their heights, in grid order.
static mnavResult Collect(Builder* builder, int32_t n)
{
    Part* part = builder->part;
    int64_t sample = builder->settings.sample;
    builder->candidateCount = 0;
    int32_t minX = INT32_MAX;
    int32_t minZ = INT32_MAX;
    int32_t maxX = 0;
    int32_t maxZ = 0;
    int64_t heights = 0;
    for (int32_t k = 0; k < n; ++k)
    {
        const mnavDetailVertex* v = &part->vertices[k];
        minX = v->x < minX ? v->x : minX;
        minZ = v->z < minZ ? v->z : minZ;
        maxX = v->x > maxX ? v->x : maxX;
        maxZ = v->z > maxZ ? v->z : maxZ;
        heights += v->y;
    }
    // No point lies half a sample distance from both sides of a polygon
    // narrower than the distance; the test also keeps the products below
    // within 64 bits.
    if (n < 3 || sample == 0 || maxX - minX < sample || maxZ - minZ < sample)
    {
        return mnav_success;
    }
    int32_t reference = (int32_t)(heights / n);
    for (int64_t gz = minZ / sample; gz * sample <= maxZ; ++gz)
    {
        for (int64_t gx = minX / sample; gx * sample <= maxX; ++gx)
        {
            mnavDetailVertex p = {(int32_t)(gx * sample), 0, (int32_t)(gz * sample)};
            if (!WellInside(part, n, &p, sample))
            {
                continue;
            }
            mnavResult result =
                mnavReserve(builder->memory, (void**)&builder->candidates,
                            &builder->candidateCapacity, builder->candidateCount,
                            builder->candidateCount + 1, sizeof(Candidate), alignof(Candidate));
            if (result != mnav_success)
            {
                return result;
            }
            p.y = Height(builder, p.x, p.z, reference);
            builder->candidates[builder->candidateCount++] = (Candidate){p, -1, -1, -1, false};
        }
    }
    return mnav_success;
}

// Locates a candidate and measures its height off the triangle holding
// it, against the maximum error.
static void Measure(const Builder* builder, const mnavTriangulation* tr, Candidate* candidate)
{
    const mnavDetailVertex* p = &candidate->point;
    candidate->value = -1;
    candidate->triangle = mnavLocate(tr, p->x, p->z, &candidate->edge);
    // Samples lie well inside the polygon (WellInside), which the
    // triangulation covers: one is always found, in a triangle with area,
    // and never on the outer edge, where an insertion fails. This check,
    // the area's and the insertion's guard the integer geometry against
    // disagreeing with that, which no input has made it do.
    if (candidate->triangle < 0)
    {
        return;
    }
    const mnavDetailTriangle* triangle = &tr->triangles[candidate->triangle];
    const mnavDetailVertex* a = &tr->vertices[triangle->corners[0]];
    const mnavDetailVertex* b = &tr->vertices[triangle->corners[1]];
    const mnavDetailVertex* c = &tr->vertices[triangle->corners[2]];
    // Barycentric weights, each times the triangle's signed area.
    int64_t wa = mnavDetailArea2(p, b, c);
    int64_t wb = mnavDetailArea2(a, p, c);
    int64_t wc = mnavDetailArea2(a, b, p);
    int64_t signedArea = wa + wb + wc;
    if (signedArea == 0)
    {
        return;
    }
    // The height off the plane, in sixteenths times twice the area.
    int64_t difference = (int64_t)p->y * signedArea - (wa * a->y + wb * b->y + wc * c->y);
    int64_t off = SUBCELLS * (difference < 0 ? -difference : difference);
    int64_t area = signedArea < 0 ? -signedArea : signedArea;
    if (off > (int64_t)builder->settings.error * area)
    {
        candidate->value = off / area;
    }
}

// The candidate farthest off the detail surface beyond the maximum error,
// the first in grid order on ties, or -1.
static int32_t Worst(const Builder* builder)
{
    int32_t worst = -1;
    for (int32_t s = 0; s < builder->candidateCount; ++s)
    {
        const Candidate* candidate = &builder->candidates[s];
        if (!candidate->added && candidate->value >= 0 &&
            (worst < 0 || candidate->value > builder->candidates[worst].value))
        {
            worst = s;
        }
    }
    return worst;
}

// Makes the outline's triangulation Delaunay, then adds the worst sample
// at a time until every sample is within the maximum error or the part is
// full. A sample is measured again only when its triangle changed.
static void AddInterior(Builder* builder)
{
    Part* part = builder->part;
    mnavTriangulation tr = {
        part->vertices, part->triangles, part->triangleCount,
        PART_TRIANGLES, part->stack,     (int32_t)(sizeof(part->stack) / sizeof(part->stack[0])),
        part->changed};
    mnavMakeDelaunay(&tr);
    for (int32_t s = 0; s < builder->candidateCount; ++s)
    {
        Measure(builder, &tr, &builder->candidates[s]);
    }
    for (int32_t s = Worst(builder); s >= 0; s = Worst(builder))
    {
        if (part->vertexCount >= MNAV_DETAIL_VERTICES)
        {
            builder->detail->cappedPolygons += 1;
            break;
        }
        Candidate* chosen = &builder->candidates[s];
        chosen->added = true;
        memset(part->changed, 0, sizeof(part->changed));
        part->vertices[part->vertexCount] = chosen->point;
        if (!mnavInsertVertex(&tr, part->vertexCount, chosen->triangle, chosen->edge))
        {
            continue;
        }
        part->vertexCount += 1;
        for (int32_t c = 0; c < builder->candidateCount; ++c)
        {
            Candidate* candidate = &builder->candidates[c];
            if (!candidate->added && candidate->triangle >= 0 &&
                part->changed[candidate->triangle] != 0)
            {
                Measure(builder, &tr, candidate);
            }
        }
    }
    part->triangleCount = tr.count;
}

// Appends the part to the detail mesh as polygon p's.
static mnavResult Store(Builder* builder, int32_t p)
{
    mnavDetailMesh* detail = builder->detail;
    const Part* part = builder->part;
    mnavResult result =
        mnavReserve(builder->memory, (void**)&detail->vertices, &detail->vertexCapacity,
                    detail->vertexCount, detail->vertexCount + part->vertexCount,
                    sizeof(mnavDetailVertex), alignof(mnavDetailVertex));
    if (result == mnav_success)
    {
        result = mnavReserve(builder->memory, (void**)&detail->triangles, &detail->triangleCapacity,
                             detail->triangleCount, detail->triangleCount + part->triangleCount,
                             sizeof(mnavDetailTriangle), alignof(mnavDetailTriangle));
    }
    if (result != mnav_success)
    {
        return result;
    }
    detail->parts[p] = (mnavDetailPart){detail->vertexCount, detail->triangleCount,
                                        (uint8_t)part->vertexCount, (uint8_t)part->triangleCount};
    memcpy(detail->vertices + detail->vertexCount, part->vertices,
           (size_t)part->vertexCount * sizeof(mnavDetailVertex));
    memcpy(detail->triangles + detail->triangleCount, part->triangles,
           (size_t)part->triangleCount * sizeof(mnavDetailTriangle));
    detail->vertexCount += part->vertexCount;
    detail->triangleCount += part->triangleCount;
    return mnav_success;
}

static mnavResult BuildPart(Builder* builder, int32_t p)
{
    mnavResult result = mnav_success;
    if (builder->settings.sample > 0)
    {
        result = mnavFillHeightPatch(builder->memory, builder->field, builder->regions,
                                     builder->settings.border, builder->mesh, p, &builder->patch);
    }
    if (result != mnav_success)
    {
        return result;
    }
    BuildOutline(builder, &builder->mesh->polygons[p]);
    TriangulateOutline(builder);
    result = Collect(builder, builder->mesh->polygons[p].count);
    if (result != mnav_success)
    {
        return result;
    }
    AddInterior(builder);
    MarkOutline(builder->part);
    return Store(builder, p);
}

mnavResult mnavBuildDetailMesh(mnavMemory* memory, const mnavCompactField* field,
                               const mnavRegionMap* regions, const mnavPolyMesh* mesh,
                               mnavDetailSettings settings, mnavDetailMesh* detail)
{
    *detail = (mnavDetailMesh){0};
    Builder builder = {memory, field, regions, mesh, settings, detail, {0}, nullptr, nullptr, 0, 0};
    size_t parts = (size_t)mesh->polygonCount;
    mnavResult result = mnavAllocate(memory, parts, sizeof(mnavDetailPart), alignof(mnavDetailPart),
                                     (void**)&detail->parts);
    if (result == mnav_success)
    {
        detail->partCount = mesh->polygonCount;
        result = mnavAllocate(memory, 1, sizeof(Part), alignof(Part), (void**)&builder.part);
    }
    for (int32_t p = 0; p < mesh->polygonCount && result == mnav_success; ++p)
    {
        result = BuildPart(&builder, p);
    }
    mnavRelease(memory, builder.candidates, (size_t)builder.candidateCapacity, sizeof(Candidate),
                alignof(Candidate));
    mnavRelease(memory, builder.part, 1, sizeof(Part), alignof(Part));
    mnavReleaseHeightPatch(memory, &builder.patch);
    if (result != mnav_success)
    {
        mnavReleaseDetailMesh(memory, detail);
    }
    return result;
}

void mnavReleaseDetailMesh(mnavMemory* memory, mnavDetailMesh* detail)
{
    mnavRelease(memory, detail->triangles, (size_t)detail->triangleCapacity,
                sizeof(mnavDetailTriangle), alignof(mnavDetailTriangle));
    mnavRelease(memory, detail->vertices, (size_t)detail->vertexCapacity, sizeof(mnavDetailVertex),
                alignof(mnavDetailVertex));
    mnavRelease(memory, detail->parts, (size_t)detail->partCount, sizeof(mnavDetailPart),
                alignof(mnavDetailPart));
    *detail = (mnavDetailMesh){0};
}
