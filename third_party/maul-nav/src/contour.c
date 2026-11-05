// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Region contours: tracing and simplifying.

#include "contour.h"

#include "allocator.h"
#include "compact.h"
#include "invariant.h"
#include "region.h"

#include "maul-nav/bake.h"

#include <stdint.h>
#include <string.h>

// The edge from a raw vertex to the next separates two areas.
#define EDGE_AREA_BORDER 2u

// A raw vertex, in cells of the field with its border (x, z) and cell
// heights (y), the region across the edge from it to the next vertex (0
// for a wall, a region, or a border region), and its flags: the
// vertex's MNAV_VERTEX_TILE_BORDER and the edge's EDGE_AREA_BORDER.
// Tracing and simplification read the region and the area border; a
// stored vertex keeps only the tile-border flag, which polygons read.
typedef struct RawVertex
{
    int32_t x;
    int32_t y;
    int32_t z;
    uint32_t neighbor;
    uint8_t flags;
} RawVertex;

// The working buffers one contour needs: its raw vertices, one per
// boundary edge, and the raw indices its simplified form keeps.
typedef struct Work
{
    RawVertex* raw;
    int32_t rawCount;
    int32_t rawCapacity;
    int32_t* kept;
    int32_t keptCount;
    int32_t keptCapacity;
    // The region traced, and, in the second pass, what the first found:
    // the regions one of whose contours kept fewer than three vertices,
    // by id, which keep their cells along every stretch, as the regions
    // across those stretches do, so that both sides of a stretch keep the
    // same vertices; and each region's outline bounds on the ground, four
    // per id (x0, z0, x1, z1), to find what a hole of walls holds.
    uint32_t region;
    const uint8_t* fell;
    const int32_t* bounds;
    // The regions that fell back, by id.
    const uint32_t* fallen;
    uint32_t fallenCount;
} Work;

typedef struct Tracer
{
    const mnavCompactField* field;
    const uint32_t* ids;
    uint32_t regionCount;
    uint8_t* edges;
    int32_t border;
} Tracer;

MNAV_NODISCARD static bool IsRegion(uint32_t id)
{
    return id != 0 && (id & MNAV_BORDER_REGION) == 0;
}

// The span linked from span i of column (x, z) in a direction, or -1.
static int64_t Linked(const mnavCompactField* field, int32_t x, int32_t z, uint32_t i,
                      int32_t direction)
{
    if (field->spans[i].links[direction] == MNAV_NO_LINK)
    {
        return -1;
    }
    return mnavLinkedSpan(field, x, z, i, direction);
}

// Marks, per span of a region, the directions whose edge is a boundary:
// no link, or a linked span of another region.
static void MarkEdges(const Tracer* tracer)
{
    const mnavCompactField* field = tracer->field;
    int32_t width = field->frame.width;
    for (int32_t c = 0; c < width * width; ++c)
    {
        for (uint32_t i = field->columns[c]; i < field->columns[c + 1]; ++i)
        {
            uint8_t edges = 0;
            for (int32_t direction = 0; IsRegion(tracer->ids[i]) && direction < 4; ++direction)
            {
                int64_t n = Linked(field, c % width, c / width, i, direction);
                if (n < 0 || tracer->ids[n] != tracer->ids[i])
                {
                    edges |= (uint8_t)(1u << direction);
                }
            }
            tracer->edges[i] = edges;
        }
    }
}

// A region and area as one key, so that cells compare by both.
static uint64_t Key(const Tracer* tracer, int64_t i)
{
    if (i < 0)
    {
        return 0;
    }
    return ((uint64_t)tracer->field->areas[i] << 32) | tracer->ids[i];
}

bool mnavIsTileBorderCorner(const uint64_t keys[4])
{
    for (int32_t j = 0; j < 4; ++j)
    {
        uint64_t a = keys[j];
        uint64_t b = keys[(j + 1) & 3];
        uint64_t c = keys[(j + 2) & 3];
        uint64_t d = keys[(j + 3) & 3];
        bool twoBorders = (a & MNAV_BORDER_REGION) != 0 && a == b;
        bool twoInside = ((c | d) & MNAV_BORDER_REGION) == 0 && (c >> 32) == (d >> 32);
        if (twoBorders && twoInside && a != 0 && b != 0 && c != 0 && d != 0)
        {
            return true;
        }
    }
    return false;
}

// The height of the corner at the end of span i's edge in a direction:
// the highest floor of the up to four cells around it.
static int32_t CornerHeight(const Tracer* tracer, int32_t x, int32_t z, uint32_t i,
                            int32_t direction, bool* tileBorder)
{
    const mnavCompactField* field = tracer->field;
    int32_t turn = (direction + 1) & 3;
    int32_t height = field->spans[i].floor;
    uint64_t keys[4] = {Key(tracer, i), 0, 0, 0};
    int64_t a = Linked(field, x, z, i, direction);
    if (a >= 0)
    {
        int32_t ax = x + mnavDirectionX(direction);
        int32_t az = z + mnavDirectionZ(direction);
        height = height > field->spans[a].floor ? height : field->spans[a].floor;
        keys[1] = Key(tracer, a);
        int64_t diagonal = Linked(field, ax, az, (uint32_t)a, turn);
        if (diagonal >= 0)
        {
            height = height > field->spans[diagonal].floor ? height : field->spans[diagonal].floor;
            keys[2] = Key(tracer, diagonal);
        }
    }
    int64_t b = Linked(field, x, z, i, turn);
    if (b >= 0)
    {
        int32_t bx = x + mnavDirectionX(turn);
        int32_t bz = z + mnavDirectionZ(turn);
        height = height > field->spans[b].floor ? height : field->spans[b].floor;
        keys[3] = Key(tracer, b);
        int64_t diagonal = Linked(field, bx, bz, (uint32_t)b, direction);
        if (diagonal >= 0)
        {
            height = height > field->spans[diagonal].floor ? height : field->spans[diagonal].floor;
            keys[2] = Key(tracer, diagonal);
        }
    }
    *tileBorder = mnavIsTileBorderCorner(keys);
    return height;
}

static mnavResult Emit(mnavMemory* memory, const Tracer* tracer, Work* work, int32_t x, int32_t z,
                       uint32_t i, int32_t direction)
{
    mnavResult result = mnavReserve(memory, (void**)&work->raw, &work->rawCapacity, work->rawCount,
                                    work->rawCount + 1, sizeof(RawVertex), alignof(RawVertex));
    if (result != mnav_success)
    {
        return result;
    }
    bool tileBorder = false;
    int32_t y = CornerHeight(tracer, x, z, i, direction, &tileBorder);
    // The corner at the end of the edge, walking with the region on the
    // right: -X edges end at (x, z + 1), +Z at (x + 1, z + 1), +X at
    // (x + 1, z) and -Z at (x, z).
    int32_t cx = x + (direction == 1 || direction == 2 ? 1 : 0);
    int32_t cz = z + (direction == 0 || direction == 1 ? 1 : 0);
    int64_t n = Linked(tracer->field, x, z, i, direction);
    uint32_t neighbor = n >= 0 ? tracer->ids[n] : 0;
    uint8_t flags = tileBorder ? MNAV_VERTEX_TILE_BORDER : 0;
    if (n >= 0 && tracer->field->areas[n] != tracer->field->areas[i])
    {
        flags |= EDGE_AREA_BORDER;
    }
    work->raw[work->rawCount++] = (RawVertex){cx, y, cz, neighbor, flags};
    return mnav_success;
}

// Walks the boundary of span i's region from its first boundary edge,
// turning clockwise at each boundary edge and counter-clockwise across
// each inner one, and emits a vertex per edge.
static mnavResult Walk(mnavMemory* memory, const Tracer* tracer, Work* work, int32_t x, int32_t z,
                       uint32_t i)
{
    int32_t direction = 0;
    while ((tracer->edges[i] & (1u << direction)) == 0)
    {
        ++direction;
    }
    uint32_t start = i;
    int32_t startDirection = direction;
    // Every step uses an edge or crosses one, and each edge is used once.
    int64_t bound = 4 * (int64_t)tracer->field->spanCount + 4;
    for (int64_t step = 0; step < bound; ++step)
    {
        if ((tracer->edges[i] & (1u << direction)) != 0)
        {
            mnavResult result = Emit(memory, tracer, work, x, z, i, direction);
            if (result != mnav_success)
            {
                return result;
            }
            tracer->edges[i] &= (uint8_t)~(1u << direction);
            direction = (direction + 1) & 3;
        }
        else
        {
            int64_t n = Linked(tracer->field, x, z, i, direction);
            MNAV_ASSERT(n >= 0);
            x += mnavDirectionX(direction);
            z += mnavDirectionZ(direction);
            i = (uint32_t)n;
            direction = (direction + 3) & 3;
        }
        if (i == start && direction == startDirection)
        {
            return mnav_success;
        }
    }
    MNAV_ASSERT(false);
    return mnav_success;
}

// Inserts raw index value into the kept list after position at.
static mnavResult Keep(mnavMemory* memory, Work* work, int32_t at, int32_t value)
{
    mnavResult result =
        mnavReserve(memory, (void**)&work->kept, &work->keptCapacity, work->keptCount,
                    work->keptCount + 1, sizeof(int32_t), alignof(int32_t));
    if (result != mnav_success)
    {
        return result;
    }
    for (int32_t j = work->keptCount; j > at + 1; --j)
    {
        work->kept[j] = work->kept[j - 1];
    }
    work->kept[at + 1] = value;
    work->keptCount += 1;
    return mnav_success;
}

// The squared distance from point (x, z) to the segment from (ax, az) to
// (bx, bz).
static float SegmentDistance(int32_t x, int32_t z, int32_t ax, int32_t az, int32_t bx, int32_t bz)
{
    float sx = (float)(bx - ax);
    float sz = (float)(bz - az);
    float dx = (float)(x - ax);
    float dz = (float)(z - az);
    float length = sx * sx + sz * sz;
    float t = sx * dx + sz * dz;
    t = length > 0.0f ? t / length : t;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    float px = (float)ax + t * sx - (float)x;
    float pz = (float)az + t * sz - (float)z;
    return px * px + pz * pz;
}

MNAV_NODISCARD static bool LexicallyAfter(const RawVertex* b, const RawVertex* a)
{
    return b->x > a->x || (b->x == a->x && b->z > a->z);
}

// The first kept vertices: every vertex where the region across or the
// area border changes, or, with none, the lowest-left and upper-right.
static mnavResult Seed(mnavMemory* memory, Work* work)
{
    int32_t n = work->rawCount;
    for (int32_t i = 0; i < n; ++i)
    {
        const RawVertex* a = &work->raw[i];
        const RawVertex* b = &work->raw[(i + 1) % n];
        bool changes =
            a->neighbor != b->neighbor || ((a->flags ^ b->flags) & EDGE_AREA_BORDER) != 0;
        mnavResult result = changes ? Keep(memory, work, work->keptCount - 1, i) : mnav_success;
        if (result != mnav_success)
        {
            return result;
        }
    }
    if (work->keptCount > 0)
    {
        return mnav_success;
    }
    int32_t low = 0;
    int32_t high = 0;
    for (int32_t i = 1; i < n; ++i)
    {
        low = LexicallyAfter(&work->raw[low], &work->raw[i]) ? i : low;
        high = LexicallyAfter(&work->raw[i], &work->raw[high]) ? i : high;
    }
    mnavResult result = Keep(memory, work, -1, low);
    return result == mnav_success ? Keep(memory, work, 0, high) : result;
}

// Every raw vertex from at to end, stepping by step, has the neighbor
// first has and lies on the segment from from to to.
static void CheckStraight(const Work* work, int32_t at, int32_t end, int32_t step,
                          const RawVertex* from, const RawVertex* to)
{
    int32_t n = work->rawCount;
    uint32_t neighbor = work->raw[at].neighbor;
    for (; at != end; at = (at + step) % n)
    {
        const RawVertex* v = &work->raw[at];
        int64_t cross = (int64_t)(to->x - from->x) * (v->z - from->z) -
                        (int64_t)(to->z - from->z) * (v->x - from->x);
        MNAV_ASSERT(cross == 0 && v->neighbor == neighbor);
        (void)cross;
    }
    (void)neighbor;
}

// The first raw vertex past the kept segment at position i's start, in
// its lexical direction: every raw vertex of the segment has its
// neighbor, since the seeds are where the neighbor changes.
static int32_t FirstOf(const Work* work, int32_t i)
{
    int32_t n = work->rawCount;
    int32_t ai = work->kept[i];
    int32_t bi = work->kept[(i + 1) % work->keptCount];
    bool forward = LexicallyAfter(&work->raw[bi], &work->raw[ai]);
    return forward ? (ai + 1) % n : (bi + n - 1) % n;
}

// Whether a region fell back in the first pass.
MNAV_NODISCARD static bool Fell(const Work* work, uint32_t id)
{
    return work->fell != nullptr && IsRegion(id) && work->fell[id] != 0;
}

// Whether the raw vertices of the kept segment at position i, its ends
// included, reach within the outline bounds of a region that fell back
// other than its own: a wall simplified there could cut across the
// region's cells, which it keeps.
MNAV_NODISCARD static bool NearFallen(const Work* work, int32_t i)
{
    if (work->fallenCount == 0)
    {
        return false;
    }
    int32_t n = work->rawCount;
    int32_t ai = work->kept[i];
    int32_t bi = work->kept[(i + 1) % work->keptCount];
    int32_t x0 = INT32_MAX;
    int32_t z0 = INT32_MAX;
    int32_t x1 = INT32_MIN;
    int32_t z1 = INT32_MIN;
    for (int32_t at = ai;; at = (at + 1) % n)
    {
        const RawVertex* v = &work->raw[at];
        x0 = v->x < x0 ? v->x : x0;
        z0 = v->z < z0 ? v->z : z0;
        x1 = v->x > x1 ? v->x : x1;
        z1 = v->z > z1 ? v->z : z1;
        if (at == bi)
        {
            break;
        }
    }
    for (uint32_t k = 0; k < work->fallenCount; ++k)
    {
        uint32_t id = work->fallen[k];
        const int32_t* b = &work->bounds[4 * id];
        if (id != work->region && b[0] <= x1 && b[2] >= x0 && b[1] <= z1 && b[3] >= z0)
        {
            return true;
        }
    }
    return false;
}

// The raw vertex farthest from the kept segment at position i, or -1,
// for walls and area borders; the segment is measured in its lexical
// direction so the regions on both sides of an edge agree. Every other
// edge has a region of the same area or a border region across, and is
// straight: a region is a whole layer of row sweeps (region.c), and
// spans linked along x always share a sweep, so two regions of one area
// meet only across a row of cells, and a border region only along the
// tile's side. Simplifying such an edge would keep nothing more.
static int32_t Farthest(const Work* work, int32_t i, float* distanceOut)
{
    int32_t n = work->rawCount;
    int32_t ai = work->kept[i];
    int32_t bi = work->kept[(i + 1) % work->keptCount];
    const RawVertex* a = &work->raw[ai];
    const RawVertex* b = &work->raw[bi];
    bool forward = LexicallyAfter(b, a);
    int32_t step = forward ? 1 : n - 1;
    int32_t at = FirstOf(work, i);
    int32_t end = forward ? bi : ai;
    const RawVertex* from = forward ? a : b;
    const RawVertex* to = forward ? b : a;
    const RawVertex* first = &work->raw[at];
    int32_t farthest = -1;
    float distance = 0.0f;
    if (first->neighbor == 0 || (first->flags & EDGE_AREA_BORDER) != 0)
    {
        for (; at != end; at = (at + step) % n)
        {
            const RawVertex* v = &work->raw[at];
            float d = SegmentDistance(v->x, v->z, from->x, from->z, to->x, to->z);
            if (d > distance)
            {
                distance = d;
                farthest = at;
            }
        }
    }
    else if (MNAV_INVARIANTS)
    {
        CheckStraight(work, at, end, step, from, to);
    }
    *distanceOut = distance;
    return farthest;
}

// Twice the signed area of the raw contour on the ground: positive for a
// region's outline, negative for a hole.
static int64_t RawArea(const Work* work)
{
    int64_t area = 0;
    for (int32_t i = 0, j = work->rawCount - 1; i < work->rawCount; j = i++)
    {
        const RawVertex* a = &work->raw[i];
        const RawVertex* b = &work->raw[j];
        area += (int64_t)a->x * b->z - (int64_t)b->x * a->z;
    }
    return area;
}

// Whether a contour may go when it simplifies to fewer than three vertices:
// a hole all of whose edges are walls, which its region then covers,
// when it holds no other region (HoldsAnother).
MNAV_NODISCARD static bool MayGo(const Work* work)
{
    if (RawArea(work) > 0)
    {
        return false;
    }
    for (int32_t i = 0; i < work->rawCount; ++i)
    {
        if (work->raw[i].neighbor != 0)
        {
            return false;
        }
    }
    return true;
}

// Whether a hole of walls holds another region: one whose outline's bounds
// lie inside the hole's, which it would cover if it went. Layers above or
// below count too; a hole kept for one is only more exact.
MNAV_NODISCARD static bool HoldsAnother(const Work* work, uint32_t count)
{
    int32_t x0 = INT32_MAX;
    int32_t z0 = INT32_MAX;
    int32_t x1 = INT32_MIN;
    int32_t z1 = INT32_MIN;
    for (int32_t i = 0; i < work->rawCount; ++i)
    {
        const RawVertex* v = &work->raw[i];
        x0 = v->x < x0 ? v->x : x0;
        z0 = v->z < z0 ? v->z : z0;
        x1 = v->x > x1 ? v->x : x1;
        z1 = v->z > z1 ? v->z : z1;
    }
    for (uint32_t id = 1; work->bounds != nullptr && id <= count; ++id)
    {
        const int32_t* b = &work->bounds[4 * id];
        if (id != work->region && b[0] <= b[2] && b[0] > x0 && b[1] > z0 && b[2] < x1 && b[3] < z1)
        {
            return true;
        }
    }
    return false;
}

// Keeps the raw vertices the walls need to stay within edgeError of their
// cells: within 0 a stretch of a region that fell back, across from one,
// or reaching within one's bounds (Work), so that no wall simplified past
// a cell cuts into a region kept to its cells.
static mnavResult Simplify(mnavMemory* memory, Work* work, float edgeError)
{
    mnavResult result = Seed(memory, work);
    bool fell = Fell(work, work->region);
    for (int32_t i = 0; result == mnav_success && i < work->keptCount;)
    {
        float distance = 0.0f;
        int32_t farthest = Farthest(work, i, &distance);
        float error =
            fell || Fell(work, work->raw[FirstOf(work, i)].neighbor) || NearFallen(work, i)
                ? 0.0f
                : edgeError;
        if (farthest >= 0 && distance > error * error)
        {
            result = Keep(memory, work, i, farthest);
        }
        else
        {
            ++i;
        }
    }
    return result;
}

// Splits walls longer than edgeLength cells at their raw midpoint,
// rounded the same way from both directions.
static mnavResult SplitLongWalls(mnavMemory* memory, Work* work, int32_t edgeLength)
{
    int32_t n = work->rawCount;
    mnavResult result = mnav_success;
    for (int32_t i = 0; edgeLength > 0 && result == mnav_success && i < work->keptCount;)
    {
        int32_t ai = work->kept[i];
        int32_t bi = work->kept[(i + 1) % work->keptCount];
        const RawVertex* a = &work->raw[ai];
        const RawVertex* b = &work->raw[bi];
        int32_t dx = b->x - a->x;
        int32_t dz = b->z - a->z;
        int32_t between = bi < ai ? bi + n - ai : bi - ai;
        bool wall = work->raw[(ai + 1) % n].neighbor == 0;
        if (wall && dx * dx + dz * dz > edgeLength * edgeLength && between > 1)
        {
            int32_t half = LexicallyAfter(b, a) ? between / 2 : (between + 1) / 2;
            result = Keep(memory, work, i, (ai + half) % n);
        }
        else
        {
            ++i;
        }
    }
    return result;
}

// Twice the signed area of a contour's vertices on the ground; negative
// for a hole.
static int64_t SignedArea(const mnavContourVertex* vertices, int32_t count)
{
    int64_t area = 0;
    for (int32_t i = 0, j = count - 1; i < count; j = i++)
    {
        area += (int64_t)vertices[i].x * vertices[j].z - (int64_t)vertices[j].x * vertices[i].z;
    }
    return area;
}

// Appends the kept vertices to the set as one contour: each keeps only
// its place and its tile-border flag, moves out of the border, and loses
// any repeat of the next vertex's ground position.
static mnavResult Store(mnavMemory* memory, const Work* work, uint32_t region, mnavAreaType area,
                        int32_t border, mnavContourSet* set)
{
    int32_t first = set->vertexCount;
    mnavResult result = mnavReserve(memory, (void**)&set->vertices, &set->vertexCapacity,
                                    set->vertexCount, set->vertexCount + work->keptCount,
                                    sizeof(mnavContourVertex), alignof(mnavContourVertex));
    if (result != mnav_success)
    {
        return result;
    }
    mnavContourVertex* out = set->vertices + first;
    int32_t count = 0;
    for (int32_t k = 0; k < work->keptCount; ++k)
    {
        int32_t index = work->kept[k];
        const RawVertex* v = &work->raw[index];
        out[count++] = (mnavContourVertex){v->x - border, v->y, v->z - border,
                                           (uint8_t)(v->flags & MNAV_VERTEX_TILE_BORDER)};
    }
    // Repeats on the ground confuse triangulation; drop the earlier of
    // each pair until none is left, the last against the first included.
    // One arises only when both passes of a corner the outline touches
    // twice are kept and everything between them is simplified away,
    // which no input has yet made.
    for (int32_t i = 0; count > 1 && i < count;)
    {
        const mnavContourVertex* a = &out[i];
        const mnavContourVertex* b = &out[(i + 1) % count];
        if (a->x == b->x && a->z == b->z)
        {
            memmove(&out[i], &out[i + 1], (size_t)(count - i - 1) * sizeof(mnavContourVertex));
            count -= 1;
        }
        else
        {
            ++i;
        }
    }
    if (count < 3)
    {
        return mnav_success;
    }
    result = mnavReserve(memory, (void**)&set->contours, &set->capacity, set->count, set->count + 1,
                         sizeof(mnavContour), alignof(mnavContour));
    if (result != mnav_success)
    {
        return result;
    }
    set->contours[set->count++] =
        (mnavContour){first, count, region, area, SignedArea(out, count) < 0};
    set->vertexCount += count;
    return mnav_success;
}

// What the first pass finds for the second (Work), and whether a second
// pass is needed: a region fell back, or a hole of walls wide enough to
// hold another region went.
typedef struct Findings
{
    uint8_t* fell;
    int32_t* bounds;
    uint32_t* fallen;
    uint32_t fallenCount;
    uint32_t count;
    bool again;
} Findings;

// Notes a contour's outline bounds and, when it kept fewer than three
// vertices, whether it falls back, in the first pass.
static void Note(const Work* work, bool goes, Findings* f)
{
    int32_t* b = &f->bounds[4 * work->region];
    bool outline = RawArea(work) > 0;
    int32_t x0 = INT32_MAX;
    int32_t z0 = INT32_MAX;
    int32_t x1 = INT32_MIN;
    int32_t z1 = INT32_MIN;
    for (int32_t i = 0; i < work->rawCount; ++i)
    {
        const RawVertex* v = &work->raw[i];
        x0 = v->x < x0 ? v->x : x0;
        z0 = v->z < z0 ? v->z : z0;
        x1 = v->x > x1 ? v->x : x1;
        z1 = v->z > z1 ? v->z : z1;
    }
    if (outline)
    {
        b[0] = x0 < b[0] ? x0 : b[0];
        b[1] = z0 < b[1] ? z0 : b[1];
        b[2] = x1 > b[2] ? x1 : b[2];
        b[3] = z1 > b[3] ? z1 : b[3];
    }
    if (work->keptCount >= 3)
    {
        return;
    }
    if (!goes)
    {
        if (f->fell[work->region] == 0)
        {
            f->fallen[f->fallenCount++] = work->region;
        }
        f->fell[work->region] = 1;
        f->again = true;
    }
    // A hole of walls holds another region only round at least a cell of
    // it and a cell of wall on each side.
    else if (x1 - x0 >= 3 && z1 - z0 >= 3)
    {
        f->again = true;
    }
}

// Simplifies the contour just walked. An outline so thin that no wall
// strays edgeError would keep two vertices and its region be lost, though
// it is walkable and a neighboring tile keeps the same cells in a wider
// region up to the shared side: it keeps its cells' outline, and in the
// second pass so do the stretches across from it. A hole of walls that
// thin goes, and its region covers it, unless it holds another region.
// The first pass, with findings to fill, simplifies each contour alone.
static mnavResult SimplifyOne(mnavMemory* memory, const Tracer* tracer, Work* work, float edgeError,
                              Findings* first)
{
    mnavResult result = Simplify(memory, work, edgeError);
    if (result != mnav_success || work->keptCount >= 3 || edgeError == 0.0f)
    {
        if (result == mnav_success && first != nullptr)
        {
            Note(work, true, first);
        }
        return result;
    }
    bool goes = MayGo(work) && (first != nullptr || !HoldsAnother(work, tracer->regionCount));
    if (first != nullptr)
    {
        Note(work, goes, first);
    }
    if (goes)
    {
        return mnav_success;
    }
    work->keptCount = 0;
    return Simplify(memory, work, 0.0f);
}

// Traces, simplifies and stores every contour. The first pass, with
// findings to fill, simplifies each contour alone; the second, with the
// first's, simplifies the stretches of regions that fell back, across
// from them or near them, within 0.
static mnavResult TraceAll(mnavMemory* memory, const Tracer* tracer, Work* work, float edgeError,
                           int32_t edgeLength, Findings* first, mnavContourSet* set)
{
    const mnavCompactField* field = tracer->field;
    int32_t width = field->frame.width;
    for (int32_t c = 0; c < width * width; ++c)
    {
        for (uint32_t i = field->columns[c]; i < field->columns[c + 1]; ++i)
        {
            if (tracer->edges[i] == 0)
            {
                continue;
            }
            work->rawCount = 0;
            work->keptCount = 0;
            work->region = tracer->ids[i];
            mnavResult result = Walk(memory, tracer, work, c % width, c / width, i);
            if (result == mnav_success)
            {
                result = SimplifyOne(memory, tracer, work, edgeError, first);
            }
            if (result == mnav_success)
            {
                result = SplitLongWalls(memory, work, edgeLength);
            }
            if (result == mnav_success)
            {
                result = Store(memory, work, tracer->ids[i], field->areas[i], tracer->border, set);
            }
            if (result != mnav_success)
            {
                return result;
            }
        }
    }
    return mnav_success;
}

// The contours lie end to end over the vertices, each of at least three,
// with no two neighbors at one ground place, within the tile without its
// border, of one of the map's regions, a hole exactly when it winds
// backward.
static void CheckContours(const mnavContourSet* set, const mnavRegionMap* regions, int32_t side)
{
    int32_t next = 0;
    for (int32_t c = 0; c < set->count; ++c)
    {
        const mnavContour* contour = &set->contours[c];
        MNAV_ASSERT(contour->first == next && contour->count >= 3);
        MNAV_ASSERT(IsRegion(contour->region) && contour->region <= regions->count);
        const mnavContourVertex* v = &set->vertices[contour->first];
        for (int32_t k = 0; k < contour->count; ++k)
        {
            const mnavContourVertex* w = &v[(k + 1) % contour->count];
            MNAV_ASSERT(v[k].x >= 0 && v[k].x <= side && v[k].z >= 0 && v[k].z <= side);
            MNAV_ASSERT(v[k].x != w->x || v[k].z != w->z);
            (void)w;
        }
        MNAV_ASSERT(contour->hole == (SignedArea(v, contour->count) < 0));
        next += contour->count;
    }
    MNAV_ASSERT(next == set->vertexCount);
}

mnavResult mnavBuildContours(mnavMemory* memory, const mnavCompactField* field,
                             const mnavRegionMap* regions, int32_t border, float edgeError,
                             int32_t edgeLength, mnavContourSet* set)
{
    *set = (mnavContourSet){0};
    Tracer tracer = {field, regions->ids, regions->count, nullptr, border};
    size_t ids = (size_t)regions->count + 1;
    Findings findings = {nullptr, nullptr, nullptr, 0, regions->count, false};
    mnavResult result = mnavAllocate(memory, (size_t)field->spanCount, sizeof(uint8_t),
                                     alignof(uint8_t), (void**)&tracer.edges);
    result = result == mnav_success ? mnavAllocate(memory, ids, sizeof(uint8_t), alignof(uint8_t),
                                                   (void**)&findings.fell)
                                    : result;
    result = result == mnav_success ? mnavAllocate(memory, 4 * ids, sizeof(int32_t),
                                                   alignof(int32_t), (void**)&findings.bounds)
                                    : result;
    result = result == mnav_success ? mnavAllocate(memory, ids, sizeof(uint32_t), alignof(uint32_t),
                                                   (void**)&findings.fallen)
                                    : result;
    Work work = {0};
    if (result == mnav_success)
    {
        memset(findings.fell, 0, ids);
        for (size_t id = 0; id < ids; ++id)
        {
            int32_t* b = &findings.bounds[4 * id];
            b[0] = INT32_MAX;
            b[1] = INT32_MAX;
            b[2] = INT32_MIN;
            b[3] = INT32_MIN;
        }
        MarkEdges(&tracer);
        result = TraceAll(memory, &tracer, &work, edgeError, edgeLength, &findings, set);
    }
    if (result == mnav_success && findings.again)
    {
        set->count = 0;
        set->vertexCount = 0;
        work.fell = findings.fell;
        work.bounds = findings.bounds;
        work.fallen = findings.fallen;
        work.fallenCount = findings.fallenCount;
        MarkEdges(&tracer);
        result = TraceAll(memory, &tracer, &work, edgeError, edgeLength, nullptr, set);
    }
    mnavRelease(memory, findings.fallen, ids, sizeof(uint32_t), alignof(uint32_t));
    mnavRelease(memory, findings.bounds, 4 * ids, sizeof(int32_t), alignof(int32_t));
    mnavRelease(memory, findings.fell, ids, sizeof(uint8_t), alignof(uint8_t));
    mnavRelease(memory, work.kept, (size_t)work.keptCapacity, sizeof(int32_t), alignof(int32_t));
    mnavRelease(memory, work.raw, (size_t)work.rawCapacity, sizeof(RawVertex), alignof(RawVertex));
    mnavRelease(memory, tracer.edges, (size_t)field->spanCount, sizeof(uint8_t), alignof(uint8_t));
    if (result == mnav_success && MNAV_INVARIANTS)
    {
        CheckContours(set, regions, field->frame.width - 2 * border);
    }
    return result;
}

void mnavReleaseContours(mnavMemory* memory, mnavContourSet* set)
{
    mnavRelease(memory, set->vertices, (size_t)set->vertexCapacity, sizeof(mnavContourVertex),
                alignof(mnavContourVertex));
    mnavRelease(memory, set->contours, (size_t)set->capacity, sizeof(mnavContour),
                alignof(mnavContour));
    *set = (mnavContourSet){0};
}
