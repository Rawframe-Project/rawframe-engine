// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The path search: A* over portals, with named limits (mnav-0005).

#include "allocator.h"
#include "funnel.h"
#include "navmesh.h"
#include "offmesh.h"
#include "polymesh.h"
#include "query.h"
#include "query_filter.h"
#include "raster.h"

#include "maul-nav/base.h"
#include "maul-nav/navmesh.h"
#include "maul-nav/query.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

// Whether node a leaves the open list before node b: the lower total, then
// the one made first.
static bool Sooner(const mnavQuery* query, int32_t a, int32_t b)
{
    const mnavSearchNode* na = &query->nodes[a];
    const mnavSearchNode* nb = &query->nodes[b];
    double ta = na->cost + na->remaining;
    double tb = nb->cost + nb->remaining;
    return ta != tb ? ta < tb : a < b;
}

static void Place(mnavQuery* query, int32_t at, int32_t n)
{
    query->heap[at] = n;
    query->nodes[n].heap = at;
}

static void SiftUp(mnavQuery* query, int32_t at)
{
    int32_t n = query->heap[at];
    while (at > 0 && Sooner(query, n, query->heap[(at - 1) / 2]))
    {
        Place(query, at, query->heap[(at - 1) / 2]);
        at = (at - 1) / 2;
    }
    Place(query, at, n);
}

static int32_t Pop(mnavQuery* query)
{
    int32_t top = query->heap[0];
    query->nodes[top].heap = MNAV_NO_NODE;
    int32_t last = query->heap[--query->heapCount];
    int32_t at = 0;
    while (query->heapCount > 0)
    {
        int32_t child = 2 * at + 1;
        if (child >= query->heapCount)
        {
            break;
        }
        if (child + 1 < query->heapCount &&
            Sooner(query, query->heap[child + 1], query->heap[child]))
        {
            child += 1;
        }
        if (!Sooner(query, query->heap[child], last))
        {
            break;
        }
        Place(query, at, query->heap[child]);
        at = child;
    }
    if (query->heapCount > 0)
    {
        Place(query, at, last);
    }
    return top;
}

// One search: the end it heads for and what stopped ways on.

static double Distance(mnavPos3 a, mnavPos3 b)
{
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    double dz = a.z - b.z;
    return sqrt(dx * dx + dy * dy + dz * dz);
}

static mnavPos3 Midpoint(mnavPos3 a, mnavPos3 b)
{
    return (mnavPos3){(a.x + b.x) * 0.5, (a.y + b.y) * 0.5, (a.z + b.z) * 0.5};
}

// Opens the node behind a portal from node from, or lowers its cost when
// this way is cheaper; closed nodes are final.
static void Open(mnavSearch* s, int32_t from, const mnavSearchNode* key, mnavPos3 a, mnavPos3 b,
                 double linkCost)
{
    mnavQuery* query = s->query;
    if (s->inside != nullptr && s->inside[key->slot] == 0 && !s->beyond)
    {
        return;
    }
    const mnavSearchNode* parent = &query->nodes[from];
    bool offMesh = key->tag == MNAV_TAG_OFFMESH;
    // Where the walk in the parent's polygon goes, and where the node
    // stands: a portal's midpoint, the end point, or an off-mesh link's
    // takeoff and landing points.
    mnavPos3 via = key->tag == MNAV_TAG_END ? s->end : (offMesh ? a : Midpoint(a, b));
    mnavPos3 at = offMesh ? b : via;
    // The walk lies in the parent's polygon, which is convex.
    const mnavPolygon* crossed =
        &s->navmesh->slots[parent->slot].tile->mesh.polygons[parent->polygon];
    double step = Distance(parent->at, via);
    double span = offMesh ? Distance(a, b) : 0.0;
    double cost = parent->cost + step * (double)s->filter->costs[crossed->area] + linkCost;
    double length = parent->length + step + span;
    double toEnd = key->tag == MNAV_TAG_END ? 0.0 : Distance(at, s->end);
    if (length + toEnd > s->limit)
    {
        s->tooLong = true;
        return;
    }
    uint32_t cell = mnavFindNode(query, key->slot, key->polygon, key->tag, key->low);
    int32_t n = query->table[cell];
    if (n != MNAV_NO_NODE)
    {
        mnavSearchNode* node = &query->nodes[n];
        if (node->heap != MNAV_NO_NODE && cost < node->cost)
        {
            node->cost = cost;
            node->length = length;
            node->parent = from;
            SiftUp(query, node->heap);
        }
        return;
    }
    if (query->nodeCount == query->limits.nodes)
    {
        s->outOfNodes = true;
        return;
    }
    n = query->nodeCount++;
    query->nodes[n] = (mnavSearchNode){
        a,        b,        at,   cost,        length, toEnd * s->cheapest, key->slot, key->polygon,
        key->tag, key->low, from, MNAV_NO_NODE};
    query->table[cell] = n;
    query->heap[query->heapCount] = n;
    SiftUp(query, query->heapCount++);
}

// Opens the polygon across an inner edge j of node n's polygon.
static void ExpandInner(mnavSearch* s, int32_t n, const mnavTile* tile, int32_t j)
{
    const mnavSearchNode* node = &s->query->nodes[n];
    const mnavPolygon* polygon = &tile->mesh.polygons[node->polygon];
    int32_t next = polygon->neighbors[j];
    uint16_t from = polygon->vertices[j];
    uint16_t to = polygon->vertices[(j + 1) % polygon->count];
    const mnavPolygon* other = &tile->mesh.polygons[next];
    if (!mnavIncludes(s->filter, other->area))
    {
        return;
    }
    for (int32_t i = 0; i < other->count; ++i)
    {
        if (other->vertices[i] == to && other->vertices[(i + 1) % other->count] == from)
        {
            mnavFrame f = mnavFrameOf(s->navmesh, s->navmesh->slots[node->slot].x,
                                      s->navmesh->slots[node->slot].z);
            mnavSearchNode key = {.slot = node->slot, .polygon = next, .tag = i, .low = 0};
            Open(s, n, &key, mnavVertexWorld(&f, &tile->mesh.vertices[from]),
                 mnavVertexWorld(&f, &tile->mesh.vertices[to]), 0.0);
            return;
        }
    }
}

// The point of edge ab whose coordinate along a tile side is u.
static mnavPos3 AlongSide(mnavPos3 a, mnavPos3 b, double au, double bu, double u)
{
    double t = (u - au) / (bu - au);
    return (mnavPos3){a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.z + t * (b.z - a.z)};
}

// Opens the polygons the tile links of edge j of node n's polygon reach,
// or notes that no tile is loaded across it.
static void ExpandSide(mnavSearch* s, int32_t n, const mnavTile* tile, int32_t j)
{
    const mnavSearchNode* node = &s->query->nodes[n];
    const mnavSlot* slot = &s->navmesh->slots[node->slot];
    const mnavPolygon* polygon = &tile->mesh.polygons[node->polygon];
    int32_t side = polygon->sides[j];
    int32_t x = slot->x;
    int32_t z = slot->z;
    int32_t facing = 0;
    int32_t across = -1;
    mnavAcross(side, &x, &z, &facing);
    if (mnavTileAt(s->navmesh, x, z, &across) == nullptr)
    {
        s->notLoaded = true;
        return;
    }
    mnavFrame f = mnavFrameOf(s->navmesh, slot->x, slot->z);
    const mnavMeshVertex* va = &tile->mesh.vertices[polygon->vertices[j]];
    const mnavMeshVertex* vb = &tile->mesh.vertices[polygon->vertices[(j + 1) % polygon->count]];
    bool alongZ = side == 1 || side == 3;
    double au = alongZ ? va->z : va->x;
    double bu = alongZ ? vb->z : vb->x;
    mnavPos3 a = mnavVertexWorld(&f, va);
    mnavPos3 b = mnavVertexWorld(&f, vb);
    for (int32_t l = tile->firstLink[node->polygon]; l < tile->firstLink[node->polygon + 1]; ++l)
    {
        const mnavLink* link = &tile->links[l];
        bool back = node->tag == MNAV_TAG_LINK + side && node->low == link->low;
        const mnavTile* beyond = s->navmesh->slots[link->target.slot - 1].tile;
        mnavAreaType area = beyond->mesh.polygons[link->target.polygon].area;
        if (link->edge != j || back || !mnavIncludes(s->filter, area))
        {
            continue;
        }
        // The overlap's ends in the edge's own direction.
        bool rising = bu > au;
        mnavPos3 first = AlongSide(a, b, au, bu, rising ? link->low : link->high);
        mnavPos3 second = AlongSide(a, b, au, bu, rising ? link->high : link->low);
        mnavSearchNode key = {.slot = (int32_t)link->target.slot - 1,
                              .polygon = (int32_t)link->target.polygon,
                              .tag = MNAV_TAG_LINK + facing,
                              .low = link->low};
        Open(s, n, &key, first, second, 0.0);
    }
}

// Opens the polygons the off-mesh links leaving node n's polygon land on,
// for the kinds and areas the filter includes.
static void ExpandOffMesh(mnavSearch* s, int32_t n)
{
    const mnavNavmesh* navmesh = s->navmesh;
    const mnavSearchNode* node = &s->query->nodes[n];
    int32_t first = 0;
    int32_t count = mnavAttachmentsFrom(navmesh, node->slot, node->polygon, &first);
    for (int32_t i = first; i < first + count; ++i)
    {
        mnavAttachment attachment = mnavAttachmentOf(navmesh->attachments[i]);
        const mnavOffLink* link = &navmesh->links[attachment.link];
        const mnavLinkState* state = &link->state;
        mnavPolygonId landing = attachment.reverse ? state->startPolygon : state->endPolygon;
        const mnavTile* tile = navmesh->slots[landing.slot - 1].tile;
        if (!mnavCrosses(s->filter, link->def.kind) ||
            !mnavIncludes(s->filter, tile->mesh.polygons[landing.polygon].area))
        {
            continue;
        }
        mnavSearchNode key = {.slot = (int32_t)landing.slot - 1,
                              .polygon = (int32_t)landing.polygon,
                              .tag = MNAV_TAG_OFFMESH,
                              .low = attachment.link * 2 + (attachment.reverse ? 1 : 0)};
        Open(s, n, &key, attachment.reverse ? state->end : state->start,
             attachment.reverse ? state->start : state->end, (double)link->def.cost);
    }
}

static void Expand(mnavSearch* s, int32_t n)
{
    const mnavSearchNode* node = &s->query->nodes[n];
    const mnavTile* tile = s->navmesh->slots[node->slot].tile;
    const mnavPolygon* polygon = &tile->mesh.polygons[node->polygon];
    if (node->slot == s->endSlot && node->polygon == s->endPolygon)
    {
        mnavSearchNode key = {
            .slot = node->slot, .polygon = node->polygon, .tag = MNAV_TAG_END, .low = 0};
        Open(s, n, &key, s->end, s->end, 0.0);
    }
    // mnavSearchNode pointers stay valid as nodes are added: the array never moves.
    // The edge the node came in through leads only back.
    for (int32_t j = 0; j < polygon->count; ++j)
    {
        bool inner = polygon->neighbors[j] != MNAV_NO_INDEX;
        if (inner && j != node->tag)
        {
            ExpandInner(s, n, tile, j);
        }
        if (!inner && polygon->sides[j] != 0)
        {
            ExpandSide(s, n, tile, j);
        }
    }
    ExpandOffMesh(s, n);
}

static bool FinitePoint(mnavPos3 p)
{
    return isfinite(p.x) && isfinite(p.y) && isfinite(p.z);
}

// Whether node a lies nearer the end than node b: less still to go, then
// the lower cost, then made first.
static bool Nearer(const mnavQuery* query, int32_t a, int32_t b)
{
    const mnavSearchNode* na = &query->nodes[a];
    const mnavSearchNode* nb = &query->nodes[b];
    if (na->remaining != nb->remaining)
    {
        return na->remaining < nb->remaining;
    }
    return na->cost != nb->cost ? na->cost < nb->cost : a < b;
}

// The heuristic's scale: the cheapest included area's cost, or less for
// a kind of link the filter crosses that costs less per meter (mnav-0005).
double mnavHeuristicScale(const mnavNavmesh* navmesh, const mnavQueryFilter* filter)
{
    double scale = mnavCheapest(filter);
    for (int32_t k = 0; k < MNAV_LINK_KINDS; ++k)
    {
        double perMeter = navmesh->costPerMeter[k];
        scale = mnavCrosses(filter, (mnavLinkKind)k) && perMeter < scale ? perMeter : scale;
    }
    return scale;
}

static mnavPathEnd EndOf(const mnavSearch* s)
{
    if (s->outOfNodes)
    {
        return mnav_pathOutOfNodes;
    }
    if (s->tooLong)
    {
        return mnav_pathTooLong;
    }
    return s->notLoaded ? mnav_pathNotLoaded : mnav_pathNone;
}

mnavResult mnavBeginPath(mnavQuery* query, const mnavNavmesh* navmesh,
                         const mnavQueryFilter* filter, mnavPolygonId startPolygon, mnavPos3 start,
                         mnavPolygonId endPolygon, mnavPos3 end)
{
    if (query == nullptr || navmesh == nullptr || !FinitePoint(start) || !FinitePoint(end))
    {
        return mnav_errorInvalid;
    }
    mnavResult result = mnavCheckPolygon(navmesh, startPolygon);
    result = result == mnav_success ? mnavCheckPolygon(navmesh, endPolygon) : result;
    const mnavQueryFilter* usable = nullptr;
    result = result == mnav_success ? mnavCheckFilter(filter, &usable) : result;
    if (result != mnav_success)
    {
        return result;
    }
    // The search keeps its own copy: the caller's filter may change.
    query->filter = *usable;
    memset(query->table, 0xFF, ((size_t)query->tableMask + 1) * sizeof(int32_t));
    mnavSearch* s = &query->search;
    *s = (mnavSearch){query,
                      navmesh,
                      navmesh->commits,
                      &query->filter,
                      mnavHeuristicScale(navmesh, &query->filter),
                      end,
                      (int32_t)endPolygon.slot - 1,
                      (int32_t)endPolygon.polygon,
                      (double)query->limits.pathLength,
                      false,
                      false,
                      false,
                      true,
                      0,
                      MNAV_NO_NODE,
                      nullptr,
                      false,
                      {-1, -1, -1, -1}};
    query->nodes[0] = (mnavSearchNode){start,
                                       start,
                                       start,
                                       0.0,
                                       0.0,
                                       Distance(start, end) * s->cheapest,
                                       (int32_t)startPolygon.slot - 1,
                                       (int32_t)startPolygon.polygon,
                                       MNAV_TAG_START,
                                       0,
                                       MNAV_NO_NODE,
                                       0};
    query->table[mnavFindNode(query, query->nodes[0].slot, query->nodes[0].polygon, MNAV_TAG_START,
                              0)] = 0;
    query->heap[0] = 0;
    query->nodeCount = 1;
    query->heapCount = 1;
    return mnav_success;
}

void mnavConfineSearch(mnavQuery* query, const uint8_t* inside, bool beyond, bool noEnd)
{
    mnavSearch* s = &query->search;
    s->inside = inside;
    s->beyond = beyond;
    if (noEnd)
    {
        s->endSlot = -1;
        s->cheapest = 0.0;
        query->nodes[0].remaining = 0.0;
    }
}

void mnavAimSearch(mnavQuery* query, mnavPos3 end, int32_t endSlot, int32_t endPolygon,
                   const int32_t goal[4])
{
    mnavSearch* s = &query->search;
    for (int32_t i = 0; i < 4; ++i)
    {
        s->goal[i] = goal[i];
    }
    // Aimed just after it began or started over, the search holds one open
    // node, whose order does not matter.
    s->end = end;
    s->endSlot = endSlot;
    s->endPolygon = endPolygon;
}

// The rank of node index v among the sorted indices.
static int32_t RankOf(const int32_t* sorted, int32_t count, int32_t v)
{
    int32_t low = 0;
    int32_t high = count - 1;
    while (low < high)
    {
        int32_t middle = low + (high - low) / 2;
        low = sorted[middle] < v ? middle + 1 : low;
        high = sorted[middle] < v ? high : middle;
    }
    return low;
}

void mnavRestartSearch(mnavQuery* query, int32_t n)
{
    // The way to n, its node indices sorted, in the heap's memory: a
    // lowered cost may give a node a parent made after it.
    int32_t* sorted = query->heap;
    int32_t count = 0;
    for (int32_t at = n; at != MNAV_NO_NODE; at = query->nodes[at].parent)
    {
        int32_t i = count++;
        while (i > 0 && sorted[i - 1] > at)
        {
            sorted[i] = sorted[i - 1];
            i -= 1;
        }
        sorted[i] = at;
    }
    // The k-th smallest index is at least k, so moving the nodes up in
    // that order never writes over one still to move.
    memset(query->table, 0xFF, ((size_t)query->tableMask + 1) * sizeof(int32_t));
    for (int32_t k = 0; k < count; ++k)
    {
        mnavSearchNode node = query->nodes[sorted[k]];
        node.parent =
            node.parent == MNAV_NO_NODE ? MNAV_NO_NODE : RankOf(sorted, count, node.parent);
        node.heap = MNAV_NO_NODE;
        query->nodes[k] = node;
        query->table[mnavFindNode(query, node.slot, node.polygon, node.tag, node.low)] = k;
    }
    int32_t last = RankOf(sorted, count, n);
    mnavSearch* s = &query->search;
    query->nodeCount = count;
    query->heapCount = 1;
    query->heap[0] = last;
    query->nodes[last].heap = 0;
    query->nodes[last].remaining = Distance(query->nodes[last].at, s->end) * s->cheapest;
    s->found = MNAV_NO_NODE;
    s->best = last;
}

// Whether a search began and may go on on this navmesh: no commit to it
// since.
static mnavResult CheckSearch(const mnavQuery* query, const mnavNavmesh* navmesh)
{
    if (query == nullptr || navmesh == nullptr || !query->search.active)
    {
        return mnav_errorInvalid;
    }
    const mnavSearch* s = &query->search;
    return s->navmesh == navmesh && s->commits == navmesh->commits ? mnav_success : mnav_errorStale;
}

static bool Ended(const mnavQuery* query)
{
    return query->search.found != MNAV_NO_NODE || query->heapCount == 0;
}

mnavResult mnavContinuePath(mnavQuery* query, const mnavNavmesh* navmesh, int32_t nodes,
                            bool* endedOut)
{
    if (endedOut == nullptr || nodes < 1)
    {
        return mnav_errorInvalid;
    }
    mnavResult result = CheckSearch(query, navmesh);
    if (result != mnav_success)
    {
        return result;
    }
    mnavSearch* s = &query->search;
    for (int32_t closed = 0; closed < nodes && !Ended(query); ++closed)
    {
        int32_t n = Pop(query);
        if (query->nodes[n].tag == MNAV_TAG_END)
        {
            s->found = n;
            continue;
        }
        const mnavSearchNode* node = &query->nodes[n];
        if (node->tag == s->goal[1] && node->slot == s->goal[0] && node->low >= s->goal[2] &&
            node->low <= s->goal[3])
        {
            s->found = n;
            continue;
        }
        s->best = Nearer(query, n, s->best) ? n : s->best;
        if (s->inside == nullptr || s->inside[node->slot] != 0)
        {
            Expand(s, n);
        }
    }
    *endedOut = Ended(query);
    return mnav_success;
}

mnavResult mnavFinishPath(mnavQuery* query, const mnavNavmesh* navmesh, mnavPath* pathOut)
{
    if (pathOut == nullptr)
    {
        return mnav_errorInvalid;
    }
    mnavResult result = CheckSearch(query, navmesh);
    if (result != mnav_success)
    {
        return result;
    }
    mnavSearch* s = &query->search;
    mnavPathEnd end = s->found != MNAV_NO_NODE ? mnav_pathFound
                      : Ended(query)           ? EndOf(s)
                                               : mnav_pathUnfinished;
    int32_t last = s->found != MNAV_NO_NODE ? s->found : s->best;
    int32_t linkCount = 0;
    int32_t pointCount = mnavStraighten(query, navmesh, last, &linkCount);
    *pathOut = (mnavPath){end,
                          query->nodes[last].cost,
                          query->nodes[last].length,
                          query->corridor,
                          mnavPathPolygons(query, navmesh, last),
                          query->points,
                          pointCount,
                          query->links,
                          linkCount};
    s->active = false;
    return mnav_success;
}

mnavResult mnavFindPath(mnavQuery* query, const mnavNavmesh* navmesh, const mnavQueryFilter* filter,
                        mnavPolygonId startPolygon, mnavPos3 start, mnavPolygonId endPolygon,
                        mnavPos3 end, mnavPath* pathOut)
{
    if (pathOut == nullptr)
    {
        return mnav_errorInvalid;
    }
    mnavResult result = mnavBeginPath(query, navmesh, filter, startPolygon, start, endPolygon, end);
    bool ended = false;
    while (result == mnav_success && !ended)
    {
        result = mnavContinuePath(query, navmesh, INT32_MAX, &ended);
    }
    return result == mnav_success ? mnavFinishPath(query, navmesh, pathOut) : result;
}

mnavResult mnavCheckReachable(mnavQuery* query, const mnavNavmesh* navmesh,
                              const mnavQueryFilter* filter, mnavPolygonId startPolygon,
                              mnavPos3 start, mnavPolygonId endPolygon, mnavPos3 end,
                              mnavPathEnd* endOut)
{
    if (endOut == nullptr)
    {
        return mnav_errorInvalid;
    }
    mnavResult result = mnavBeginPath(query, navmesh, filter, startPolygon, start, endPolygon, end);
    bool ended = false;
    while (result == mnav_success && !ended)
    {
        result = mnavContinuePath(query, navmesh, INT32_MAX, &ended);
    }
    if (result != mnav_success)
    {
        return result;
    }
    mnavSearch* s = &query->search;
    *endOut = s->found != MNAV_NO_NODE ? mnav_pathFound : EndOf(s);
    s->active = false;
    return mnav_success;
}
