// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Flow fields over the navmesh (mnav-0013): Dijkstra's search backward from
// the goals over polygons, as Detour's findPolysAroundCircle searches
// forward from one. Polygons are numbered slot by slot, so the heap's order
// by cost, then number, is the order by cost, then slot and polygon. The
// off-mesh links are sorted once per build by the polygon they land on, so
// the search can step back across them.

#include "maul-nav/navflow.h"

#include "allocator.h"
#include "draw.h"
#include "navmesh.h"
#include "offmesh.h"
#include "query_filter.h"
#include "sort.h"

#include "maul-nav/base.h"
#include "maul-nav/navmesh.h"
#include "maul-nav/query.h"

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>

// A polygon's place in the heap when it is not in it: never reached, or
// done.
#define NOT_OPEN (-1)
#define DONE     (-2)

// What a polygon holds: its cost, place, next polygon, the portal to it and
// the link slot taken or -1.
typedef struct Way
{
    double cost;
    mnavPos3 place;
    mnavPos3 left;
    mnavPos3 right;
    int32_t next;
    int32_t link;
} Way;

struct mnavNavFlow
{
    mnavMemory memory;
    mnavNavFlowDef def;
    Way* ways;
    int32_t* places;
    int32_t* heap;
    int32_t heapCount;
    // Each slot's first polygon number, tiles + 1 of them.
    int32_t* firsts;
    // The attachments by landing polygon: its number above 32 bits, the
    // attachment's index below; 2 per link, and as many again for the
    // sort's scratch.
    uint64_t* landings;
    int32_t landingCount;
    // The navmesh built on and its commits then; NULL with nothing built.
    const mnavNavmesh* navmesh;
    uint64_t commits;
    int32_t polygonCount;
    mnavQueryFilter filter;
    // The tiles searched, and whether the work has ended.
    mnavNavFlowRegion region;
    bool ended;
};

static bool GoodLimits(const mnavNavFlowLimits* l)
{
    return l->polygons >= 1 && l->polygons <= MNAV_MAX_NAVFLOW_POLYGONS && l->tiles >= 1 &&
           l->tiles <= MNAV_MAX_NAVFLOW_TILES && l->links >= 1 &&
           l->links <= MNAV_MAX_NAVFLOW_LINKS;
}

// The landing array's length: two attachments a link, and scratch.
static size_t Landings(const mnavNavFlowLimits* l)
{
    return 4 * (size_t)l->links;
}

mnavResult mnavCreateNavFlow(const mnavNavFlowDef* def, mnavNavFlow** fieldOut)
{
    if (fieldOut == nullptr)
    {
        return mnav_errorInvalid;
    }
    *fieldOut = nullptr;
    if (def == nullptr || def->cookie != MNAV_NAV_FLOW_DEF_COOKIE)
    {
        return mnav_errorInvalid;
    }
    if (def->version != MNAV_ABI_VERSION)
    {
        return mnav_errorVersion;
    }
    if ((def->allocator.alloc == nullptr) != (def->allocator.free == nullptr))
    {
        return mnav_errorInvalid;
    }
    if (!GoodLimits(&def->limits))
    {
        return mnav_errorRange;
    }
    mnavMemory memory = mnavMakeMemory(def->allocator, UINT64_MAX);
    mnavNavFlow* f = nullptr;
    mnavResult result =
        mnavAllocate(&memory, 1, sizeof(mnavNavFlow), alignof(mnavNavFlow), (void**)&f);
    if (result != mnav_success)
    {
        return result;
    }
    *f = (mnavNavFlow){0};
    f->memory = memory;
    f->def = *def;
    size_t n = (size_t)def->limits.polygons;
    result = mnavAllocate(&f->memory, n, sizeof(Way), alignof(Way), (void**)&f->ways);
    if (result == mnav_success)
    {
        result = mnavAllocate(&f->memory, n, sizeof(int32_t), alignof(int32_t), (void**)&f->places);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(&f->memory, n, sizeof(int32_t), alignof(int32_t), (void**)&f->heap);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(&f->memory, (size_t)def->limits.tiles + 1, sizeof(int32_t),
                              alignof(int32_t), (void**)&f->firsts);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(&f->memory, Landings(&def->limits), sizeof(uint64_t),
                              alignof(uint64_t), (void**)&f->landings);
    }
    if (result != mnav_success)
    {
        mnavDestroyNavFlow(f);
        return result;
    }
    *fieldOut = f;
    return mnav_success;
}

void mnavDestroyNavFlow(mnavNavFlow* field)
{
    if (field == nullptr)
    {
        return;
    }
    mnavMemory memory = field->memory;
    size_t n = (size_t)field->def.limits.polygons;
    mnavRelease(&memory, field->ways, n, sizeof(Way), alignof(Way));
    mnavRelease(&memory, field->places, n, sizeof(int32_t), alignof(int32_t));
    mnavRelease(&memory, field->heap, n, sizeof(int32_t), alignof(int32_t));
    mnavRelease(&memory, field->firsts, (size_t)field->def.limits.tiles + 1, sizeof(int32_t),
                alignof(int32_t));
    mnavRelease(&memory, field->landings, Landings(&field->def.limits), sizeof(uint64_t),
                alignof(uint64_t));
    mnavRelease(&memory, field, 1, sizeof(mnavNavFlow), alignof(mnavNavFlow));
}

// The heap, by cost, then by number.
static bool Sooner(const mnavNavFlow* f, int32_t a, int32_t b)
{
    return f->ways[a].cost != f->ways[b].cost ? f->ways[a].cost < f->ways[b].cost : a < b;
}

static void Place(mnavNavFlow* f, int32_t at, int32_t polygon)
{
    f->heap[at] = polygon;
    f->places[polygon] = at;
}

static void SiftUp(mnavNavFlow* f, int32_t at)
{
    int32_t polygon = f->heap[at];
    while (at > 0)
    {
        int32_t up = (at - 1) / 2;
        if (!Sooner(f, polygon, f->heap[up]))
        {
            break;
        }
        Place(f, at, f->heap[up]);
        at = up;
    }
    Place(f, at, polygon);
}

static int32_t Pop(mnavNavFlow* f)
{
    int32_t top = f->heap[0];
    int32_t last = f->heap[--f->heapCount];
    int32_t at = 0;
    while (f->heapCount > 0)
    {
        int32_t child = 2 * at + 1;
        if (child >= f->heapCount)
        {
            break;
        }
        if (child + 1 < f->heapCount && Sooner(f, f->heap[child + 1], f->heap[child]))
        {
            child += 1;
        }
        if (!Sooner(f, f->heap[child], last))
        {
            break;
        }
        Place(f, at, f->heap[child]);
        at = child;
    }
    if (f->heapCount > 0)
    {
        Place(f, at, last);
    }
    f->places[top] = DONE;
    return top;
}

// Gives polygon p a way through polygon q at a cost, standing at place.
static void Lower(mnavNavFlow* f, int32_t p, double cost, const Way* way)
{
    f->ways[p] = *way;
    f->ways[p].cost = cost;
    if (f->places[p] == NOT_OPEN)
    {
        Place(f, f->heapCount++, p);
    }
    SiftUp(f, f->places[p]);
}

static double Distance(mnavPos3 a, mnavPos3 b)
{
    double dx = b.x - a.x;
    double dy = b.y - a.y;
    double dz = b.z - a.z;
    return sqrt(dx * dx + dy * dy + dz * dz);
}

// Offers polygon p, not yet done, a way into polygon q through the portal
// from left to right: it stands at the portal's midpoint.
static void Offer(mnavNavFlow* f, int32_t p, int32_t q, mnavPos3 left, mnavPos3 right,
                  double areaCost)
{
    if (f->places[p] == DONE)
    {
        return;
    }
    mnavPos3 middle = {(left.x + right.x) * 0.5, (left.y + right.y) * 0.5,
                       (left.z + right.z) * 0.5};
    double cost = f->ways[q].cost + Distance(middle, f->ways[q].place) * areaCost;
    if (cost < f->ways[p].cost)
    {
        Way way = {cost, middle, left, right, q, -1};
        Lower(f, p, cost, &way);
    }
}

// The polygon a number names: its slot and index.
static void Polygon(const mnavNavFlow* f, int32_t number, int32_t* slot, int32_t* polygon)
{
    int32_t lo = 0;
    int32_t hi = f->navmesh->slotCount;
    while (hi - lo > 1)
    {
        int32_t middle = lo + (hi - lo) / 2;
        if (f->firsts[middle] <= number)
        {
            lo = middle;
        }
        else
        {
            hi = middle;
        }
    }
    *slot = lo;
    *polygon = number - f->firsts[lo];
}

// Whether a slot's tile lies in the region.
static bool InRegion(const mnavNavFlow* f, const mnavNavmesh* navmesh, int32_t slot)
{
    const mnavSlot* s = &navmesh->slots[slot];
    const mnavNavFlowRegion* r = &f->region;
    return s->tile != nullptr && s->x >= r->x0 && s->x <= r->x1 && s->z >= r->z0 && s->z <= r->z1;
}

// Whether a polygon may be walked: in the region, of an area included.
static bool Usable(const mnavNavFlow* f, int32_t slot, int32_t polygon)
{
    return InRegion(f, f->navmesh, slot) &&
           mnavIncludes(&f->filter, f->navmesh->slots[slot].tile->mesh.polygons[polygon].area);
}

// Steps back from polygon q over its inner edges and tile links.
static void ExpandEdges(mnavNavFlow* f, int32_t q, int32_t slot, int32_t polygon)
{
    const mnavNavmesh* navmesh = f->navmesh;
    const mnavSlot* s = &navmesh->slots[slot];
    const mnavTile* tile = s->tile;
    const mnavPolygon* poly = &tile->mesh.polygons[polygon];
    // Every polygon has three corners or more; none has no edges to cross.
    if (poly->count == 0)
    {
        return;
    }
    double areaCost = (double)f->filter.costs[poly->area];
    mnavFrame frame = mnavFrameOf(navmesh, s->x, s->z);
    for (int32_t j = 0; j < poly->count; ++j)
    {
        int32_t next = poly->neighbors[j];
        if (next == MNAV_NO_INDEX || !Usable(f, slot, next))
        {
            continue;
        }
        // The portal as the agent crossing from the neighbour sees it.
        mnavPos3 a = mnavVertexWorld(&frame, &tile->mesh.vertices[poly->vertices[j]]);
        mnavPos3 b =
            mnavVertexWorld(&frame, &tile->mesh.vertices[poly->vertices[(j + 1) % poly->count]]);
        Offer(f, f->firsts[slot] + next, q, b, a, areaCost);
    }
    for (int32_t l = tile->firstLink[polygon]; l < tile->firstLink[polygon + 1]; ++l)
    {
        const mnavLink* link = &tile->links[l];
        if (!Usable(f, (int32_t)link->target.slot - 1, (int32_t)link->target.polygon))
        {
            continue;
        }
        const mnavMeshVertex* va = &tile->mesh.vertices[poly->vertices[link->edge]];
        const mnavMeshVertex* vb =
            &tile->mesh.vertices[poly->vertices[(link->edge + 1) % poly->count]];
        bool alongZ = link->side == 1 || link->side == 3;
        double au = alongZ ? va->z : va->x;
        double bu = alongZ ? vb->z : vb->x;
        // A tile-side edge runs along the side; one that does not has no
        // portal to give.
        if (bu == au)
        {
            continue;
        }
        mnavPos3 a = mnavVertexWorld(&frame, va);
        mnavPos3 b = mnavVertexWorld(&frame, vb);
        double tLow = ((double)link->low - au) / (bu - au);
        double tHigh = ((double)link->high - au) / (bu - au);
        mnavPos3 first = {a.x + tLow * (b.x - a.x), a.y + tLow * (b.y - a.y),
                          a.z + tLow * (b.z - a.z)};
        mnavPos3 second = {a.x + tHigh * (b.x - a.x), a.y + tHigh * (b.y - a.y),
                           a.z + tHigh * (b.z - a.z)};
        bool rising = bu > au;
        int32_t p = f->firsts[link->target.slot - 1] + (int32_t)link->target.polygon;
        Offer(f, p, q, rising ? second : first, rising ? first : second, areaCost);
    }
}

// Steps back from polygon q over the off-mesh links landing on it.
static void ExpandLinks(mnavNavFlow* f, int32_t q, double areaCost)
{
    const mnavNavmesh* navmesh = f->navmesh;
    // The first landing on q, by a binary search of the sorted keys.
    uint64_t low = (uint64_t)(uint32_t)q << 32;
    int32_t lo = 0;
    int32_t hi = f->landingCount;
    while (lo < hi)
    {
        int32_t middle = lo + (hi - lo) / 2;
        lo = f->landings[middle] < low ? middle + 1 : lo;
        hi = f->landings[middle] < low ? hi : middle;
    }
    for (int32_t i = lo; i < f->landingCount && (f->landings[i] >> 32) == (uint32_t)q; ++i)
    {
        int32_t index = (int32_t)(uint32_t)f->landings[i];
        mnavAttachment a = mnavAttachmentOf(navmesh->attachments[index]);
        const mnavOffLink* link = &navmesh->links[a.link];
        int32_t p = f->firsts[a.slot] + a.polygon;
        if (!mnavCrosses(&f->filter, link->def.kind) || !Usable(f, a.slot, a.polygon) ||
            f->places[p] == DONE)
        {
            continue;
        }
        mnavPos3 takeoff = a.reverse ? link->state.end : link->state.start;
        mnavPos3 landing = a.reverse ? link->state.start : link->state.end;
        double cost = f->ways[q].cost + Distance(landing, f->ways[q].place) * areaCost +
                      (double)link->def.cost;
        if (cost < f->ways[p].cost)
        {
            Way way = {cost, takeoff, takeoff, takeoff, q, a.link};
            Lower(f, p, cost, &way);
        }
    }
}

// Numbers the polygons slot by slot and sorts the attachments by the
// polygon they land on.
static mnavResult Index(mnavNavFlow* f, const mnavNavmesh* navmesh)
{
    // Two attachments a link at most: its two ends when two-way.
    int32_t count = navmesh->attachmentCount;
    mnavResult result = navmesh->slotCount > f->def.limits.tiles ||
                                (int64_t)count > 2 * (int64_t)f->def.limits.links
                            ? mnav_errorLimit
                            : mnav_success;
    int64_t total = 0;
    for (int32_t s = 0; result == mnav_success && s < navmesh->slotCount; ++s)
    {
        f->firsts[s] = (int32_t)total;
        total += InRegion(f, navmesh, s) ? navmesh->slots[s].tile->mesh.polygonCount : 0;
        if (total > f->def.limits.polygons)
        {
            result = mnav_errorLimit;
        }
    }
    if (result != mnav_success)
    {
        return result;
    }
    f->firsts[navmesh->slotCount] = (int32_t)total;
    f->polygonCount = (int32_t)total;
    // Only links landing in the region.
    int32_t kept = 0;
    for (int32_t i = 0; i < count; ++i)
    {
        mnavAttachment a = mnavAttachmentOf(navmesh->attachments[i]);
        const mnavLinkState* state = &navmesh->links[a.link].state;
        mnavPolygonId landing = a.reverse ? state->startPolygon : state->endPolygon;
        if (InRegion(f, navmesh, (int32_t)landing.slot - 1))
        {
            uint32_t p = (uint32_t)(f->firsts[landing.slot - 1] + (int32_t)landing.polygon);
            f->landings[kept++] = (uint64_t)p << 32 | (uint32_t)i;
        }
    }
    f->landingCount =
        kept > 0 ? (int32_t)mnavSortUnique(f->landings, f->landings + count, (size_t)kept) : 0;
    return mnav_success;
}

// Opens every goal on a polygon the filter includes.
static mnavResult Seed(mnavNavFlow* f, const mnavNavFlowGoal* goals, int32_t goalCount)
{
    for (int32_t i = 0; i < goalCount; ++i)
    {
        const mnavPos3 p = goals[i].point;
        if (!isfinite(p.x) || !isfinite(p.y) || !isfinite(p.z))
        {
            return mnav_errorInvalid;
        }
        mnavResult result = mnavCheckPolygon(f->navmesh, goals[i].polygon);
        if (result != mnav_success)
        {
            return result;
        }
        int32_t slot = (int32_t)goals[i].polygon.slot - 1;
        int32_t polygon = (int32_t)goals[i].polygon.polygon;
        int32_t number = f->firsts[slot] + polygon;
        if (Usable(f, slot, polygon) && f->ways[number].cost != 0.0)
        {
            Way way = {0.0, p, p, p, -1, -1};
            Lower(f, number, 0.0, &way);
        }
    }
    return mnav_success;
}

mnavResult mnavBeginNavFlow(mnavNavFlow* field, const mnavNavmesh* navmesh,
                            const mnavQueryFilter* filter, const mnavNavFlowRegion* region,
                            const mnavNavFlowGoal* goals, int32_t goalCount)
{
    if (field == nullptr)
    {
        return mnav_errorInvalid;
    }
    field->navmesh = nullptr;
    const mnavQueryFilter* usable = nullptr;
    if (navmesh == nullptr || goalCount < 0 || (goalCount > 0 && goals == nullptr) ||
        (region != nullptr && (region->x0 > region->x1 || region->z0 > region->z1)))
    {
        return mnav_errorInvalid;
    }
    field->region = region != nullptr
                        ? *region
                        : (mnavNavFlowRegion){INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX};
    mnavResult result = mnavCheckFilter(filter, &usable);
    result = result == mnav_success ? Index(field, navmesh) : result;
    if (result != mnav_success)
    {
        return result;
    }
    field->filter = *usable;
    field->navmesh = navmesh;
    field->commits = navmesh->commits;
    for (int32_t p = 0; p < field->polygonCount; ++p)
    {
        field->ways[p] = (Way){(double)INFINITY, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, -1, -1};
        field->places[p] = NOT_OPEN;
    }
    field->heapCount = 0;
    field->ended = false;
    result = Seed(field, goals, goalCount);
    field->navmesh = result == mnav_success ? navmesh : nullptr;
    return result;
}

mnavResult mnavContinueNavFlow(mnavNavFlow* field, const mnavNavmesh* navmesh, int32_t polygons,
                               bool* endedOut)
{
    if (field == nullptr || navmesh == nullptr || field->navmesh == nullptr || polygons < 1)
    {
        return mnav_errorInvalid;
    }
    if (navmesh != field->navmesh || navmesh->commits != field->commits)
    {
        return mnav_errorStale;
    }
    for (int32_t n = 0; n < polygons && field->heapCount > 0; ++n)
    {
        int32_t q = Pop(field);
        int32_t slot = 0;
        int32_t polygon = 0;
        Polygon(field, q, &slot, &polygon);
        const mnavTile* tile = navmesh->slots[slot].tile;
        ExpandEdges(field, q, slot, polygon);
        ExpandLinks(field, q, (double)field->filter.costs[tile->mesh.polygons[polygon].area]);
    }
    field->ended = field->heapCount == 0;
    if (endedOut != nullptr)
    {
        *endedOut = field->ended;
    }
    return mnav_success;
}

mnavResult mnavBuildNavFlow(mnavNavFlow* field, const mnavNavmesh* navmesh,
                            const mnavQueryFilter* filter, const mnavNavFlowGoal* goals,
                            int32_t goalCount)
{
    mnavResult result = mnavBeginNavFlow(field, navmesh, filter, nullptr, goals, goalCount);
    return result == mnav_success ? mnavContinueNavFlow(field, navmesh, INT32_MAX, nullptr)
                                  : result;
}

mnavResult mnavNavFlowAt(const mnavNavFlow* field, const mnavNavmesh* navmesh,
                         mnavPolygonId polygon, mnavPolygonFlow* flowOut)
{
    if (field == nullptr || navmesh == nullptr || flowOut == nullptr || field->navmesh == nullptr)
    {
        return mnav_errorInvalid;
    }
    if (navmesh != field->navmesh || navmesh->commits != field->commits)
    {
        return mnav_errorStale;
    }
    mnavResult result = mnavCheckPolygon(navmesh, polygon);
    if (result != mnav_success)
    {
        return result;
    }
    if (!InRegion(field, navmesh, (int32_t)polygon.slot - 1))
    {
        return mnav_errorInvalid;
    }
    if (!field->ended)
    {
        return mnav_errorStale;
    }
    const Way* way = &field->ways[field->firsts[polygon.slot - 1] + (int32_t)polygon.polygon];
    *flowOut = (mnavPolygonFlow){way->cost, {0, 0, 0}, way->left, way->right, {0, 0}};
    if (way->next >= 0)
    {
        int32_t slot = 0;
        int32_t index = 0;
        Polygon(field, way->next, &slot, &index);
        flowOut->next =
            (mnavPolygonId){(uint32_t)slot + 1, navmesh->slots[slot].generation, (uint32_t)index};
    }
    if (way->link >= 0)
    {
        int32_t parent = navmesh->links[way->link].parent;
        flowOut->link = (mnavLinkId){(uint32_t)parent + 1, navmesh->links[parent].generation};
    }
    return mnav_success;
}

// Appends an arrow from tail to head with two barbs on the ground plane.
static void Arrow(mnavDebugBuffer* buffer, mnavPos3 tail, mnavPos3 head)
{
    double dx = head.x - tail.x;
    double dz = head.z - tail.z;
    double ground = sqrt(dx * dx + dz * dz);
    mnavDrawLine(buffer, tail, head, mnav_debugFlow, 0);
    if (ground == 0.0)
    {
        return;
    }
    double barb = 0.25 * Distance(tail, head);
    barb = barb < 0.5 ? barb : 0.5;
    double ux = dx / ground * barb;
    double uz = dz / ground * barb;
    // Back from the head, an eighth turned either way.
    mnavDrawLine(buffer, head, (mnavPos3){head.x - ux + uz, head.y, head.z - uz - ux},
                 mnav_debugFlow, 0);
    mnavDrawLine(buffer, head, (mnavPos3){head.x - ux - uz, head.y, head.z - uz + ux},
                 mnav_debugFlow, 0);
}

// The mean of a polygon's corners.
static mnavPos3 Center(const mnavFrame* frame, const mnavPolyMesh* mesh, const mnavPolygon* poly)
{
    mnavPos3 sum = {0.0, 0.0, 0.0};
    for (int32_t k = 0; k < poly->count; ++k)
    {
        mnavPos3 v = mnavVertexWorld(frame, &mesh->vertices[poly->vertices[k]]);
        sum = (mnavPos3){sum.x + v.x, sum.y + v.y, sum.z + v.z};
    }
    double n = poly->count > 0 ? (double)poly->count : 1.0;
    return (mnavPos3){sum.x / n, sum.y / n, sum.z / n};
}

mnavResult mnavDebugNavFlow(const mnavNavFlow* field, const mnavNavmesh* navmesh,
                            mnavDebugBuffer* buffer)
{
    if (field == nullptr || navmesh == nullptr || !mnavGoodBuffer(buffer))
    {
        return mnav_errorInvalid;
    }
    if (field->navmesh == nullptr)
    {
        return mnav_success;
    }
    if (navmesh != field->navmesh || navmesh->commits != field->commits || !field->ended)
    {
        return mnav_errorStale;
    }
    for (int32_t s = 0; s < navmesh->slotCount; ++s)
    {
        if (!InRegion(field, navmesh, s))
        {
            continue;
        }
        const mnavSlot* slot = &navmesh->slots[s];
        const mnavPolyMesh* mesh = &slot->tile->mesh;
        mnavFrame frame = mnavFrameOf(navmesh, slot->x, slot->z);
        for (int32_t p = 0; p < mesh->polygonCount; ++p)
        {
            const Way* way = &field->ways[field->firsts[s] + p];
            if (way->next < 0)
            {
                continue;
            }
            mnavPos3 head = {(way->left.x + way->right.x) * 0.5, (way->left.y + way->right.y) * 0.5,
                             (way->left.z + way->right.z) * 0.5};
            Arrow(buffer, Center(&frame, mesh, &mesh->polygons[p]), head);
        }
    }
    return mnavDrawResult(buffer);
}
