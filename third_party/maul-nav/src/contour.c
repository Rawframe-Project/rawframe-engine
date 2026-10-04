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

// The working buffers one contour needs: its raw vertices, one per
// boundary edge, and the raw indices its simplified form keeps.
typedef struct Work
{
    mnavContourVertex* raw;
    int32_t rawCount;
    int32_t rawCapacity;
    int32_t* kept;
    int32_t keptCount;
    int32_t keptCapacity;
} Work;

typedef struct Tracer
{
    const mnavCompactField* field;
    const uint32_t* ids;
    uint8_t* edges;
    int32_t border;
} Tracer;

static bool IsRegion(uint32_t id)
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
    mnavResult result =
        mnavReserve(memory, (void**)&work->raw, &work->rawCapacity, work->rawCount,
                    work->rawCount + 1, sizeof(mnavContourVertex), alignof(mnavContourVertex));
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
        flags |= MNAV_EDGE_AREA_BORDER;
    }
    work->raw[work->rawCount++] = (mnavContourVertex){cx, y, cz, neighbor, flags};
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

static bool LexicallyAfter(const mnavContourVertex* b, const mnavContourVertex* a)
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
        const mnavContourVertex* a = &work->raw[i];
        const mnavContourVertex* b = &work->raw[(i + 1) % n];
        bool changes =
            a->neighbor != b->neighbor || ((a->flags ^ b->flags) & MNAV_EDGE_AREA_BORDER) != 0;
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

// The raw vertex farthest from the kept segment at position i, or -1,
// for walls and area borders; the segment is measured in its lexical
// direction so the regions on both sides of an edge agree.
static int32_t Farthest(const Work* work, int32_t i, float* distanceOut)
{
    int32_t n = work->rawCount;
    int32_t ai = work->kept[i];
    int32_t bi = work->kept[(i + 1) % work->keptCount];
    const mnavContourVertex* a = &work->raw[ai];
    const mnavContourVertex* b = &work->raw[bi];
    bool forward = LexicallyAfter(b, a);
    int32_t step = forward ? 1 : n - 1;
    int32_t at = forward ? (ai + 1) % n : (bi + n - 1) % n;
    int32_t end = forward ? bi : ai;
    const mnavContourVertex* from = forward ? a : b;
    const mnavContourVertex* to = forward ? b : a;
    const mnavContourVertex* first = &work->raw[at];
    int32_t farthest = -1;
    float distance = 0.0f;
    if (first->neighbor == 0 || (first->flags & MNAV_EDGE_AREA_BORDER) != 0)
    {
        for (; at != end; at = (at + step) % n)
        {
            const mnavContourVertex* v = &work->raw[at];
            float d = SegmentDistance(v->x, v->z, from->x, from->z, to->x, to->z);
            if (d > distance)
            {
                distance = d;
                farthest = at;
            }
        }
    }
    *distanceOut = distance;
    return farthest;
}

static mnavResult Simplify(mnavMemory* memory, Work* work, float edgeError)
{
    mnavResult result = Seed(memory, work);
    for (int32_t i = 0; result == mnav_success && i < work->keptCount;)
    {
        float distance = 0.0f;
        int32_t farthest = Farthest(work, i, &distance);
        if (farthest >= 0 && distance > edgeError * edgeError)
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
        const mnavContourVertex* a = &work->raw[ai];
        const mnavContourVertex* b = &work->raw[bi];
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

// Appends the kept vertices to the set as one contour: each takes the
// region across and the area border of the edge that starts at it (the
// next raw vertex's) and its own tile-border flag, moves out of the
// border, and loses any repeat of the next vertex's ground position.
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
        const mnavContourVertex* v = &work->raw[index];
        const mnavContourVertex* next = &work->raw[(index + 1) % work->rawCount];
        out[count++] = (mnavContourVertex){
            v->x - border,
            v->y,
            v->z - border,
            next->neighbor,
            (uint8_t)((next->flags & MNAV_EDGE_AREA_BORDER) | (v->flags & MNAV_VERTEX_TILE_BORDER)),
        };
    }
    // Repeats on the ground confuse triangulation; drop the earlier of
    // each pair until none is left, the last against the first included.
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

static mnavResult TraceAll(mnavMemory* memory, const Tracer* tracer, Work* work, float edgeError,
                           int32_t edgeLength, mnavContourSet* set)
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
            mnavResult result = Walk(memory, tracer, work, c % width, c / width, i);
            if (result == mnav_success)
            {
                result = Simplify(memory, work, edgeError);
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

mnavResult mnavBuildContours(mnavMemory* memory, const mnavCompactField* field,
                             const mnavRegionMap* regions, int32_t border, float edgeError,
                             int32_t edgeLength, mnavContourSet* set)
{
    *set = (mnavContourSet){0};
    Tracer tracer = {field, regions->ids, nullptr, border};
    mnavResult result = mnavAllocate(memory, (size_t)field->spanCount, sizeof(uint8_t),
                                     alignof(uint8_t), (void**)&tracer.edges);
    if (result != mnav_success)
    {
        return result;
    }
    MarkEdges(&tracer);
    Work work = {0};
    result = TraceAll(memory, &tracer, &work, edgeError, edgeLength, set);
    mnavRelease(memory, work.kept, (size_t)work.keptCapacity, sizeof(int32_t), alignof(int32_t));
    mnavRelease(memory, work.raw, (size_t)work.rawCapacity, sizeof(mnavContourVertex),
                alignof(mnavContourVertex));
    mnavRelease(memory, tracer.edges, (size_t)field->spanCount, sizeof(uint8_t), alignof(uint8_t));
    if (result != mnav_success)
    {
        mnavReleaseContours(memory, set);
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
