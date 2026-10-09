// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Spatial queries (mnav-0005): heights, the nearest wall within a
// radius, and random points from a seed. The walls search is Detour's
// findDistanceToWall: Dijkstra over polygons from the center's, through
// the edges within the radius; the random point near a center samples the
// polygons it reaches by the area they share with the circle, an
// inscribed 32-gon, so that, unlike findRandomPointAroundCircle, the point
// lies in the circle.
// Random numbers come from SplitMix64 (Vigna).

#include "navmesh.h"
#include "nearest.h"
#include "query.h"
#include "query_filter.h"

#include "maul-nav/base.h"
#include "maul-nav/navmesh.h"
#include "maul-nav/query.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// 2^-53, turning 53 random bits into a number in [0, 1).
#define UNIT 0x1.0p-53

// The corners of a 32-gon inscribed in the unit circle, counterclockwise
// from +X toward +Z, written out so that every platform has the same.
static const double s_ring[32][2] = {
    {1.0, 0.0},
    {0.9807852804032304, 0.19509032201612825},
    {0.9238795325112867, 0.3826834323650898},
    {0.8314696123025452, 0.5555702330196022},
    {0.7071067811865476, 0.7071067811865475},
    {0.5555702330196023, 0.8314696123025452},
    {0.38268343236508984, 0.9238795325112867},
    {0.19509032201612833, 0.9807852804032304},
    {0.0, 1.0},
    {-0.1950903220161282, 0.9807852804032304},
    {-0.3826834323650897, 0.9238795325112867},
    {-0.555570233019602, 0.8314696123025455},
    {-0.7071067811865475, 0.7071067811865476},
    {-0.8314696123025453, 0.5555702330196022},
    {-0.9238795325112867, 0.3826834323650899},
    {-0.9807852804032304, 0.1950903220161286},
    {-1.0, 0.0},
    {-0.9807852804032304, -0.19509032201612836},
    {-0.9238795325112868, -0.38268343236508967},
    {-0.8314696123025455, -0.555570233019602},
    {-0.7071067811865477, -0.7071067811865475},
    {-0.5555702330196022, -0.8314696123025452},
    {-0.38268343236509034, -0.9238795325112865},
    {-0.19509032201612866, -0.9807852804032303},
    {0.0, -1.0},
    {0.1950903220161283, -0.9807852804032304},
    {0.38268343236509, -0.9238795325112866},
    {0.5555702330196018, -0.8314696123025455},
    {0.7071067811865474, -0.7071067811865477},
    {0.8314696123025452, -0.5555702330196022},
    {0.9238795325112865, -0.3826834323650904},
    {0.9807852804032303, -0.19509032201612872},
};

// The most corners a polygon clipped to the 32-gon has.
#define CLIPPED (MNAV_POLYGON_VERTICES + 32)

typedef struct Random
{
    uint64_t state;
} Random;

static double Next(Random* r)
{
    r->state += 0x9E3779B97F4A7C15ull;
    uint64_t z = r->state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return (double)(z >> 11) * UNIT;
}

// A polygon's corners in world meters.
static int32_t Corners(const mnavNavmesh* navmesh, int32_t slot, int32_t polygon,
                       mnavPos3 corners[MNAV_POLYGON_VERTICES])
{
    const mnavSlot* s = &navmesh->slots[slot];
    const mnavPolygon* p = &s->tile->mesh.polygons[polygon];
    mnavFrame f = mnavFrameOf(navmesh, s->x, s->z);
    for (int32_t k = 0; k < p->count; ++k)
    {
        corners[k] = mnavVertexWorld(&f, &s->tile->mesh.vertices[p->vertices[k]]);
    }
    return p->count;
}

// Twice the ground area of triangle abc, positive counterclockwise seen
// from above with X right and Z down the page.
static double Twice(mnavPos3 a, mnavPos3 b, mnavPos3 c)
{
    return (b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z);
}

static double Area(const mnavPos3* corners, int32_t count)
{
    double area = 0.0;
    for (int32_t k = 1; k + 1 < count; ++k)
    {
        area += fabs(Twice(corners[0], corners[k], corners[k + 1]));
    }
    return area * 0.5;
}

mnavResult mnavGetHeight(const mnavNavmesh* navmesh, mnavPolygonId polygon, double x, double z,
                         double* heightOut)
{
    if (navmesh == nullptr || heightOut == nullptr || !isfinite(x) || !isfinite(z))
    {
        return mnav_errorInvalid;
    }
    mnavResult result = mnavCheckPolygon(navmesh, polygon);
    if (result != mnav_success)
    {
        return result;
    }
    mnavPos3 corners[MNAV_POLYGON_VERTICES];
    int32_t slot = (int32_t)polygon.slot - 1;
    int32_t count = Corners(navmesh, slot, (int32_t)polygon.polygon, corners);
    // Inside a convex polygon the point lies on one side of every edge.
    mnavPos3 p = {x, 0.0, z};
    bool below = false;
    bool above = false;
    for (int32_t k = 0; k < count; ++k)
    {
        double side = Twice(corners[k], corners[(k + 1) % count], p);
        below = below || side < 0.0;
        above = above || side > 0.0;
    }
    if (below && above)
    {
        return mnav_errorRange;
    }
    *heightOut = mnavSurfaceHeight(navmesh, slot, (int32_t)polygon.polygon, x, z);
    return mnav_success;
}

static mnavRandomPoint NoPoint(void)
{
    return (mnavRandomPoint){{0, 0, 0}, {0.0, 0.0, 0.0}};
}

// A point uniformly in a convex part of a polygon: a fan triangle by
// area, then a point in it; on the polygon's detail surface.
static mnavRandomPoint PointIn(const mnavNavmesh* navmesh, int32_t slot, int32_t polygon,
                               const mnavPos3* c, int32_t count, Random* r)
{
    // Callers pass parts with area, which have three corners or more.
    if (count < 3)
    {
        return NoPoint();
    }
    double pick = Next(r) * Area(c, count) * 2.0;
    int32_t k = 1;
    while (k + 2 < count && pick > fabs(Twice(c[0], c[k], c[k + 1])))
    {
        pick -= fabs(Twice(c[0], c[k], c[k + 1]));
        k += 1;
    }
    double s = Next(r);
    double t = Next(r);
    if (s + t > 1.0)
    {
        s = 1.0 - s;
        t = 1.0 - t;
    }
    double x = c[0].x + s * (c[k].x - c[0].x) + t * (c[k + 1].x - c[0].x);
    double z = c[0].z + s * (c[k].z - c[0].z) + t * (c[k + 1].z - c[0].z);
    const mnavSlot* home = &navmesh->slots[slot];
    return (mnavRandomPoint){{(uint32_t)slot + 1, home->generation, (uint32_t)polygon},
                             {x, mnavSurfaceHeight(navmesh, slot, polygon, x, z), z}};
}

// The included polygons' ground area, in place order; with pick at or
// past 0, the polygon where the running sum passes it.
static double Walk(const mnavNavmesh* navmesh, const mnavQueryFilter* filter, double pick,
                   int32_t* slotOut, int32_t* polygonOut)
{
    double sum = 0.0;
    for (int32_t i = 0; i < navmesh->placeCount; ++i)
    {
        int32_t slot = navmesh->places[i].slot;
        const mnavTile* tile = navmesh->slots[slot].tile;
        for (int32_t p = 0; p < tile->mesh.polygonCount; ++p)
        {
            if (!mnavIncludes(filter, tile->mesh.polygons[p].area))
            {
                continue;
            }
            mnavPos3 c[MNAV_POLYGON_VERTICES];
            double area = Area(c, Corners(navmesh, slot, p, c));
            *slotOut = slot;
            *polygonOut = p;
            if (pick >= 0.0 && pick < sum + area)
            {
                return sum;
            }
            sum += area;
        }
    }
    return sum;
}

mnavResult mnavFindRandomPoint(const mnavNavmesh* navmesh, const mnavQueryFilter* filter,
                               uint64_t seed, mnavRandomPoint* pointOut)
{
    if (navmesh == nullptr || pointOut == nullptr)
    {
        return mnav_errorInvalid;
    }
    const mnavQueryFilter* usable = nullptr;
    mnavResult result = mnavCheckFilter(filter, &usable);
    if (result != mnav_success)
    {
        return result;
    }
    *pointOut = NoPoint();
    int32_t slot = -1;
    int32_t polygon = -1;
    double total = Walk(navmesh, usable, -1.0, &slot, &polygon);
    if (slot < 0)
    {
        return mnav_success;
    }
    Random r = {seed};
    // The last polygon takes a pick that rounding left past the sum.
    Walk(navmesh, usable, Next(&r) * total, &slot, &polygon);
    mnavPos3 c[MNAV_POLYGON_VERTICES];
    int32_t count = Corners(navmesh, slot, polygon, c);
    *pointOut = PointIn(navmesh, slot, polygon, c, count, &r);
    return mnav_success;
}

// A search through the polygons within a radius of a center.
typedef struct Ring
{
    mnavQuery* query;
    const mnavNavmesh* navmesh;
    const mnavQueryFilter* filter;
    mnavPos3 center;
    double radius;
    bool limited;
    mnavWall wall;
} Ring;

// The open list, by cost, then by node index.
static bool Sooner(const mnavQuery* q, int32_t a, int32_t b)
{
    double ca = q->nodes[a].cost;
    double cb = q->nodes[b].cost;
    return ca != cb ? ca < cb : a < b;
}

static void Place(mnavQuery* q, int32_t at, int32_t n)
{
    q->indexHeap[at] = n;
    q->nodes[n].heap = at;
}

static void SiftUp(mnavQuery* q, int32_t at)
{
    int32_t n = q->indexHeap[at];
    while (at > 0 && Sooner(q, n, q->indexHeap[(at - 1) / 2]))
    {
        Place(q, at, q->indexHeap[(at - 1) / 2]);
        at = (at - 1) / 2;
    }
    Place(q, at, n);
}

static int32_t Pop(mnavQuery* q)
{
    int32_t top = q->indexHeap[0];
    int32_t last = q->indexHeap[--q->heapCount];
    int32_t at = 0;
    for (int32_t child = 1; child < q->heapCount; child = 2 * at + 1)
    {
        if (child + 1 < q->heapCount && Sooner(q, q->indexHeap[child + 1], q->indexHeap[child]))
        {
            child += 1;
        }
        if (!Sooner(q, q->indexHeap[child], last))
        {
            break;
        }
        Place(q, at, q->indexHeap[child]);
        at = child;
    }
    if (q->heapCount > 0)
    {
        Place(q, at, last);
    }
    q->nodes[top].heap = MNAV_NO_NODE;
    return top;
}

// The squared ground distance from p to segment ab, and the nearest point.
static double SegmentDistance(mnavPos3 p, mnavPos3 a, mnavPos3 b, mnavPos3* nearest)
{
    double ex = b.x - a.x;
    double ez = b.z - a.z;
    double length = ex * ex + ez * ez;
    double t = length > 0.0 ? ((p.x - a.x) * ex + (p.z - a.z) * ez) / length : 0.0;
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    *nearest = (mnavPos3){a.x + t * ex, a.y + t * (b.y - a.y), a.z + t * ez};
    double dx = p.x - nearest->x;
    double dz = p.z - nearest->z;
    return dx * dx + dz * dz;
}

// Notes wall ab when nearer than the nearest so far; ties keep the first.
static void NoteWall(Ring* r, mnavPos3 a, mnavPos3 b)
{
    mnavPos3 nearest;
    double distance = sqrt(SegmentDistance(r->center, a, b, &nearest));
    if (distance > r->radius || (r->wall.found && distance >= r->wall.distance))
    {
        return;
    }
    double dx = r->center.x - nearest.x;
    double dz = r->center.z - nearest.z;
    double length = sqrt(dx * dx + dz * dz);
    mnavPos3 normal =
        length > 0.0 ? (mnavPos3){dx / length, 0.0, dz / length} : (mnavPos3){0.0, 0.0, 0.0};
    r->wall = (mnavWall){true, r->wall.limited, distance, nearest, normal};
}

// Opens or lowers the node of polygon (slot, polygon) entered through
// portal ab from node from, when the portal lies within the radius.
static void Enter(Ring* r, int32_t from, int32_t slot, int32_t polygon, mnavPos3 a, mnavPos3 b)
{
    mnavQuery* q = r->query;
    mnavPos3 nearest;
    if (SegmentDistance(r->center, a, b, &nearest) > r->radius * r->radius)
    {
        return;
    }
    const mnavSearchNode* parent = &q->nodes[from];
    const mnavTile* tile = r->navmesh->slots[parent->slot].tile;
    mnavPos3 mid = {(a.x + b.x) * 0.5, (a.y + b.y) * 0.5, (a.z + b.z) * 0.5};
    double dx = mid.x - parent->at.x;
    double dy = mid.y - parent->at.y;
    double dz = mid.z - parent->at.z;
    double cost =
        parent->cost + sqrt(dx * dx + dy * dy + dz * dz) *
                           (double)r->filter->costs[tile->mesh.polygons[parent->polygon].area];
    uint32_t cell = mnavFindNode(q, slot, polygon, 0, 0);
    int32_t n = q->table[cell];
    if (n != MNAV_NO_NODE)
    {
        if (q->nodes[n].heap != MNAV_NO_NODE && cost < q->nodes[n].cost)
        {
            q->nodes[n].cost = cost;
            q->nodes[n].at = mid;
            q->nodes[n].parent = from;
            SiftUp(q, q->nodes[n].heap);
        }
        return;
    }
    if (q->nodeCount == q->limits.nodes)
    {
        r->limited = true;
        return;
    }
    n = q->nodeCount++;
    q->nodes[n] = (mnavSearchNode){a, b, mid, cost, 0.0, 0.0, slot, polygon, 0, 0, from, 0};
    q->table[cell] = n;
    Place(q, q->heapCount++, n);
    SiftUp(q, q->nodes[n].heap);
}

// The point of edge ab whose coordinate along a tile side is u.
static mnavPos3 AlongSide(mnavPos3 a, mnavPos3 b, double au, double bu, double u)
{
    double t = (u - au) / (bu - au);
    return (mnavPos3){a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.z + t * (b.z - a.z)};
}

// Crosses side edge j of node n's polygon through the tile links the
// filter allows; whether any did.
static bool CrossSide(Ring* r, int32_t n, int32_t j, mnavPos3 a, mnavPos3 b)
{
    const mnavSearchNode* node = &r->query->nodes[n];
    const mnavSlot* slot = &r->navmesh->slots[node->slot];
    const mnavTile* tile = slot->tile;
    const mnavPolygon* polygon = &tile->mesh.polygons[node->polygon];
    const mnavMeshVertex* va = &tile->mesh.vertices[polygon->vertices[j]];
    const mnavMeshVertex* vb = &tile->mesh.vertices[polygon->vertices[(j + 1) % polygon->count]];
    bool alongZ = polygon->sides[j] == 1 || polygon->sides[j] == 3;
    double au = alongZ ? va->z : va->x;
    double bu = alongZ ? vb->z : vb->x;
    bool crossed = false;
    for (int32_t l = tile->firstLink[node->polygon]; l < tile->firstLink[node->polygon + 1]; ++l)
    {
        const mnavLink* link = &tile->links[l];
        const mnavTile* target = r->navmesh->slots[link->target.slot - 1].tile;
        if (link->edge != j ||
            !mnavIncludes(r->filter, target->mesh.polygons[link->target.polygon].area))
        {
            continue;
        }
        crossed = true;
        Enter(r, n, (int32_t)link->target.slot - 1, (int32_t)link->target.polygon,
              AlongSide(a, b, au, bu, link->low), AlongSide(a, b, au, bu, link->high));
    }
    return crossed;
}

// Visits node n's polygon: its walls noted, the polygons across its other
// edges within the radius entered.
static void Visit(Ring* r, int32_t n)
{
    const mnavSearchNode* node = &r->query->nodes[n];
    int32_t slot = node->slot;
    const mnavTile* tile = r->navmesh->slots[slot].tile;
    const mnavPolygon* polygon = &tile->mesh.polygons[node->polygon];
    mnavPos3 c[MNAV_POLYGON_VERTICES];
    int32_t count = Corners(r->navmesh, slot, node->polygon, c);
    for (int32_t j = 0; j < count; ++j)
    {
        mnavPos3 a = c[j];
        mnavPos3 b = c[(j + 1) % count];
        int32_t next = polygon->neighbors[j];
        bool open = false;
        if (next != MNAV_NO_INDEX)
        {
            open = mnavIncludes(r->filter, tile->mesh.polygons[next].area);
            if (open)
            {
                Enter(r, n, slot, next, a, b);
            }
        }
        else if (polygon->sides[j] != 0)
        {
            open = CrossSide(r, n, j, a, b);
        }
        if (!open)
        {
            NoteWall(r, a, b);
        }
    }
}

static mnavResult BeginRing(Ring* r, mnavPolygonId polygon)
{
    const mnavQueryFilter* usable = nullptr;
    mnavResult result = mnavCheckPolygon(r->navmesh, polygon);
    result = result == mnav_success ? mnavCheckFilter(r->filter, &usable) : result;
    if (result != mnav_success)
    {
        return result;
    }
    mnavQuery* q = r->query;
    q->filter = *usable;
    r->filter = &q->filter;
    q->search.active = false;
    memset(q->table, 0xFF, ((size_t)q->tableMask + 1) * sizeof(int32_t));
    int32_t slot = (int32_t)polygon.slot - 1;
    q->nodes[0] = (mnavSearchNode){r->center, r->center, r->center,    0.0,
                                   0.0,       0.0,       slot,         (int32_t)polygon.polygon,
                                   0,         0,         MNAV_NO_NODE, 0};
    q->table[mnavFindNode(q, slot, (int32_t)polygon.polygon, 0, 0)] = 0;
    q->nodeCount = 1;
    q->indexHeap[0] = 0;
    q->heapCount = 1;
    r->wall = (mnavWall){false, false, r->radius, r->center, {0.0, 0.0, 0.0}};
    return mnav_success;
}

static bool GoodRing(const mnavQuery* query, const mnavNavmesh* navmesh, mnavPos3 center,
                     double radius)
{
    return query != nullptr && navmesh != nullptr && isfinite(center.x) && isfinite(center.y) &&
           isfinite(center.z) && isfinite(radius) && radius >= 0.0;
}

mnavResult mnavFindWallDistance(mnavQuery* query, const mnavNavmesh* navmesh,
                                const mnavQueryFilter* filter, mnavPolygonId polygon,
                                mnavPos3 center, double radius, mnavWall* wallOut)
{
    if (wallOut == nullptr || !GoodRing(query, navmesh, center, radius))
    {
        return mnav_errorInvalid;
    }
    Ring r = {query, navmesh, filter, center, radius, false, {0}};
    mnavResult result = BeginRing(&r, polygon);
    if (result != mnav_success)
    {
        return result;
    }
    while (query->heapCount > 0)
    {
        Visit(&r, Pop(query));
    }
    r.wall.limited = r.limited;
    *wallOut = r.wall;
    return mnav_success;
}

// Clips a convex polygon to the 32-gon of a circle on the ground,
// Sutherland and Hodgman's way; returns the corners left.
static int32_t ClipToCircle(const mnavPos3* corners, int32_t count, mnavPos3 center, double radius,
                            mnavPos3 out[CLIPPED])
{
    mnavPos3 a[CLIPPED];
    mnavPos3 b[CLIPPED];
    memcpy(a, corners, (size_t)count * sizeof(mnavPos3));
    int32_t n = count;
    // The polygons' corners wind one way on the ground; the 32-gon's
    // edges are kept with the same turn.
    double turn = 0.0;
    for (int32_t k = 1; k + 1 < count; ++k)
    {
        turn += Twice(corners[0], corners[k], corners[k + 1]);
    }
    double sign = turn < 0.0 ? -1.0 : 1.0;
    for (int32_t e = 0; e < 32 && n > 0; ++e)
    {
        mnavPos3 p = {center.x + radius * s_ring[e][0], 0.0, center.z + radius * s_ring[e][1]};
        mnavPos3 q = {center.x + radius * s_ring[(e + 1) % 32][0], 0.0,
                      center.z + radius * s_ring[(e + 1) % 32][1]};
        // A circle too small for its corners to differ at the center's
        // place, as one of no radius is, holds no area: an edge of one
        // point would keep every corner.
        if (p.x == q.x && p.z == q.z)
        {
            return 0;
        }
        // The 32-gon turns counterclockwise in X and Z; flipped to the
        // polygon's way when it turns the other.
        if (sign < 0.0)
        {
            mnavPos3 swap = p;
            p = q;
            q = swap;
        }
        int32_t m = 0;
        for (int32_t k = 0; k < n; ++k)
        {
            mnavPos3 s = a[k];
            mnavPos3 t = a[(k + 1) % n];
            double ds = Twice(p, q, s) * sign;
            double dt = Twice(p, q, t) * sign;
            if (ds >= 0.0)
            {
                b[m++] = s;
            }
            if ((ds >= 0.0) != (dt >= 0.0))
            {
                double u = ds / (ds - dt);
                b[m++] =
                    (mnavPos3){s.x + u * (t.x - s.x), s.y + u * (t.y - s.y), s.z + u * (t.z - s.z)};
            }
        }
        memcpy(a, b, (size_t)m * sizeof(mnavPos3));
        n = m;
    }
    memcpy(out, a, (size_t)n * sizeof(mnavPos3));
    return n;
}

mnavResult mnavFindRandomPointAround(mnavQuery* query, const mnavNavmesh* navmesh,
                                     const mnavQueryFilter* filter, mnavPolygonId polygon,
                                     mnavPos3 center, double radius, uint64_t seed,
                                     mnavRandomPoint* pointOut)
{
    if (pointOut == nullptr || !GoodRing(query, navmesh, center, radius))
    {
        return mnav_errorInvalid;
    }
    Ring r = {query, navmesh, filter, center, radius, false, {0}};
    mnavResult result = BeginRing(&r, polygon);
    if (result != mnav_success)
    {
        return result;
    }
    Random random = {seed};
    double sum = 0.0;
    int32_t picked = 0;
    while (query->heapCount > 0)
    {
        int32_t n = Pop(query);
        const mnavSearchNode* node = &query->nodes[n];
        mnavPos3 c[MNAV_POLYGON_VERTICES];
        mnavPos3 clipped[CLIPPED];
        int32_t count = Corners(navmesh, node->slot, node->polygon, c);
        double area = Area(clipped, ClipToCircle(c, count, center, radius, clipped));
        // Reservoir sampling by the area each polygon shares with the
        // circle: each one reached takes the pick with its share so far.
        sum += area;
        if (area > 0.0 && Next(&random) * sum < area)
        {
            picked = n;
        }
        Visit(&r, n);
    }
    const mnavSearchNode* node = &query->nodes[picked];
    int32_t slot = node->slot;
    if (sum == 0.0)
    {
        // A circle of no area: its center.
        *pointOut = (mnavRandomPoint){
            polygon,
            {center.x, mnavSurfaceHeight(navmesh, slot, node->polygon, center.x, center.z),
             center.z}};
        return mnav_success;
    }
    mnavPos3 c[MNAV_POLYGON_VERTICES];
    mnavPos3 clipped[CLIPPED];
    int32_t count =
        ClipToCircle(c, Corners(navmesh, slot, node->polygon, c), center, radius, clipped);
    *pointOut = PointIn(navmesh, slot, node->polygon, clipped, count, &random);
    return mnav_success;
}
