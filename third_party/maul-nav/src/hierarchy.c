// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Hierarchies (mnav-0008), after HPA* (Botea, Mueller and Schaeffer,
// 2004): clusters of tiles, a transition at the middle link of each run of
// tile links leaving a cluster and at each off-mesh link between clusters,
// and edges found by the navmesh search confined to the cluster entered.

#include "maul-nav/hierarchy.h"

#include "allocator.h"
#include "hierarchy.h"
#include "navmesh.h"
#include "offmesh.h"
#include "query.h"
#include "query_filter.h"
#include "sort.h"

#include "maul-nav/base.h"
#include "maul-nav/navmesh.h"
#include "maul-nav/query.h"

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Marks a def built by mnavDefaultHierarchyDef.
#define HIERARCHY_DEF_COOKIE 0x4E415648u

// Coordinates are biased into 32 unsigned bits for sort keys.
#define BIAS 0x80000000u

mnavHierarchyDef mnavDefaultHierarchyDef(void)
{
    return (mnavHierarchyDef){HIERARCHY_DEF_COOKIE, {0}, {4096, 16384, 262144}, 4};
}

static bool GoodDef(const mnavHierarchyDef* def)
{
    const mnavHierarchyLimits* l = &def->limits;
    return l->tiles >= 1 && l->tiles <= MNAV_MAX_HIERARCHY_TILES && l->transitions >= 1 &&
           l->transitions <= MNAV_MAX_HIERARCHY_TRANSITIONS && l->edges >= 1 &&
           l->edges <= MNAV_MAX_HIERARCHY_EDGES && def->clusterTiles >= 1 &&
           def->clusterTiles <= MNAV_MAX_CLUSTER_TILES;
}

// An array of count items for the hierarchy, kept going on a failure.
static void Take(mnavHierarchy* h, mnavResult* result, size_t count, size_t size, size_t align,
                 void** out)
{
    if (*result == mnav_success)
    {
        *result = mnavAllocate(&h->memory, count, size, align, out);
    }
}

static void Give(mnavHierarchy* h, void* block, size_t count, size_t size, size_t align)
{
    mnavMemory memory = h->memory;
    mnavRelease(&memory, block, count, size, align);
    h->memory = memory;
}

// The sizes each array is allocated with: tiles, transitions and edges.
typedef struct Sizes
{
    size_t tiles;
    size_t transitions;
    size_t edges;
} Sizes;

static Sizes SizesOf(const mnavHierarchyDef* def)
{
    return (Sizes){(size_t)def->limits.tiles, (size_t)def->limits.transitions,
                   (size_t)def->limits.edges};
}

static mnavResult Allocate(mnavHierarchy* h)
{
    Sizes n = SizesOf(&h->def);
    mnavResult r = mnav_success;
    Take(h, &r, n.tiles, sizeof(int32_t), alignof(int32_t), (void**)&h->clusterOf);
    Take(h, &r, n.tiles, sizeof(int32_t), alignof(int32_t), (void**)&h->firstOfSlot);
    Take(h, &r, n.tiles, sizeof(uint8_t), alignof(uint8_t), (void**)&h->inside);
    Take(h, &r, n.tiles, sizeof(uint64_t), alignof(uint64_t), (void**)&h->keys);
    Take(h, &r, n.tiles, sizeof(uint64_t), alignof(uint64_t), (void**)&h->scratch);
    Take(h, &r, n.tiles + 1, sizeof(int32_t), alignof(int32_t), (void**)&h->firstSlot);
    Take(h, &r, n.tiles, sizeof(int32_t), alignof(int32_t), (void**)&h->slots);
    Take(h, &r, n.tiles + 1, sizeof(int32_t), alignof(int32_t), (void**)&h->firstLeaving);
    Take(h, &r, n.transitions, sizeof(int32_t), alignof(int32_t), (void**)&h->leaving);
    Take(h, &r, n.transitions, sizeof(mnavTransition), alignof(mnavTransition),
         (void**)&h->transitions);
    Take(h, &r, n.transitions + 1, sizeof(int32_t), alignof(int32_t), (void**)&h->firstEdge);
    Take(h, &r, n.edges, sizeof(mnavEdge), alignof(mnavEdge), (void**)&h->edges);
    Take(h, &r, n.tiles, sizeof(uint32_t), alignof(uint32_t), (void**)&h->generations);
    Take(h, &r, n.tiles, sizeof(uint64_t), alignof(uint64_t), (void**)&h->areaHashes);
    Take(h, &r, n.tiles, sizeof(uint8_t), alignof(uint8_t), (void**)&h->changed);
    Take(h, &r, n.tiles, sizeof(uint8_t), alignof(uint8_t), (void**)&h->dirty);
    Take(h, &r, n.transitions + 1, sizeof(double), alignof(double), (void**)&h->costs);
    Take(h, &r, n.transitions, sizeof(double), alignof(double), (void**)&h->joins);
    Take(h, &r, n.transitions + 1, sizeof(int32_t), alignof(int32_t), (void**)&h->parents);
    Take(h, &r, n.transitions + 1, sizeof(int32_t), alignof(int32_t), (void**)&h->places);
    Take(h, &r, n.transitions + 1, sizeof(int32_t), alignof(int32_t), (void**)&h->heap);
    return r;
}

mnavResult mnavCreateHierarchy(const mnavHierarchyDef* def, mnavHierarchy** hierarchyOut)
{
    if (hierarchyOut == nullptr)
    {
        return mnav_errorInvalid;
    }
    *hierarchyOut = nullptr;
    if (def == nullptr || def->cookie != HIERARCHY_DEF_COOKIE ||
        (def->allocator.alloc == nullptr) != (def->allocator.free == nullptr))
    {
        return mnav_errorInvalid;
    }
    if (!GoodDef(def))
    {
        return mnav_errorRange;
    }
    mnavMemory memory = mnavMakeMemory(def->allocator, UINT64_MAX);
    mnavHierarchy* h = nullptr;
    mnavResult result =
        mnavAllocate(&memory, 1, sizeof(mnavHierarchy), alignof(mnavHierarchy), (void**)&h);
    if (result != mnav_success)
    {
        return result;
    }
    *h = (mnavHierarchy){0};
    h->memory = memory;
    h->def = *def;
    result = Allocate(h);
    if (result != mnav_success)
    {
        mnavDestroyHierarchy(h);
        return result;
    }
    memset(h->inside, 0, (size_t)def->limits.tiles);
    *hierarchyOut = h;
    return mnav_success;
}

void mnavDestroyHierarchy(mnavHierarchy* hierarchy)
{
    if (hierarchy == nullptr)
    {
        return;
    }
    mnavHierarchy* h = hierarchy;
    Sizes n = SizesOf(&h->def);
    Give(h, h->clusterOf, n.tiles, sizeof(int32_t), alignof(int32_t));
    Give(h, h->firstOfSlot, n.tiles, sizeof(int32_t), alignof(int32_t));
    Give(h, h->inside, n.tiles, sizeof(uint8_t), alignof(uint8_t));
    Give(h, h->keys, n.tiles, sizeof(uint64_t), alignof(uint64_t));
    Give(h, h->scratch, n.tiles, sizeof(uint64_t), alignof(uint64_t));
    Give(h, h->firstSlot, n.tiles + 1, sizeof(int32_t), alignof(int32_t));
    Give(h, h->slots, n.tiles, sizeof(int32_t), alignof(int32_t));
    Give(h, h->firstLeaving, n.tiles + 1, sizeof(int32_t), alignof(int32_t));
    Give(h, h->leaving, n.transitions, sizeof(int32_t), alignof(int32_t));
    Give(h, h->transitions, n.transitions, sizeof(mnavTransition), alignof(mnavTransition));
    Give(h, h->firstEdge, n.transitions + 1, sizeof(int32_t), alignof(int32_t));
    Give(h, h->edges, n.edges, sizeof(mnavEdge), alignof(mnavEdge));
    Give(h, h->generations, n.tiles, sizeof(uint32_t), alignof(uint32_t));
    Give(h, h->areaHashes, n.tiles, sizeof(uint64_t), alignof(uint64_t));
    Give(h, h->changed, n.tiles, sizeof(uint8_t), alignof(uint8_t));
    Give(h, h->dirty, n.tiles, sizeof(uint8_t), alignof(uint8_t));
    Give(h, h->costs, n.transitions + 1, sizeof(double), alignof(double));
    Give(h, h->joins, n.transitions, sizeof(double), alignof(double));
    Give(h, h->parents, n.transitions + 1, sizeof(int32_t), alignof(int32_t));
    Give(h, h->places, n.transitions + 1, sizeof(int32_t), alignof(int32_t));
    Give(h, h->heap, n.transitions + 1, sizeof(int32_t), alignof(int32_t));
    mnavMemory memory = h->memory;
    mnavRelease(&memory, h, 1, sizeof(mnavHierarchy), alignof(mnavHierarchy));
}

// Floor division, for clusters of tiles at negative places.
static int32_t Down(int32_t v, int32_t by)
{
    int32_t q = v / by;
    return (v % by != 0 && v < 0) ? q - 1 : q;
}

static uint64_t ClusterKey(const mnavHierarchy* h, int32_t x, int32_t z)
{
    uint32_t cx = (uint32_t)Down(x, h->def.clusterTiles) + BIAS;
    uint32_t cz = (uint32_t)Down(z, h->def.clusterTiles) + BIAS;
    return ((uint64_t)cx << 32) | cz;
}

// The index of a key among the sorted unique cluster keys.
static int32_t FindKey(const mnavHierarchy* h, uint64_t key)
{
    int32_t low = 0;
    int32_t high = h->clusterCount - 1;
    while (low < high)
    {
        int32_t middle = low + (high - low) / 2;
        if (h->keys[middle] < key)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low;
}

// Numbers the clusters by place and lists each one's slots, in the
// navmesh's place order.
static void FindClusters(mnavHierarchy* h, const mnavNavmesh* navmesh)
{
    for (int32_t i = 0; i < navmesh->placeCount; ++i)
    {
        h->keys[i] = ClusterKey(h, navmesh->places[i].x, navmesh->places[i].z);
    }
    h->clusterCount = (int32_t)mnavSortUnique(h->keys, h->scratch, (size_t)navmesh->placeCount);
    for (int32_t s = 0; s < navmesh->slotCount; ++s)
    {
        h->clusterOf[s] = -1;
        h->firstOfSlot[s] = 0;
    }
    memset(h->firstSlot, 0, ((size_t)h->clusterCount + 1) * sizeof(int32_t));
    for (int32_t i = 0; i < navmesh->placeCount; ++i)
    {
        const mnavPlace* p = &navmesh->places[i];
        int32_t c = FindKey(h, ClusterKey(h, p->x, p->z));
        h->clusterOf[p->slot] = c;
        h->firstSlot[c + 1] += 1;
    }
    for (int32_t c = 0; c < h->clusterCount; ++c)
    {
        h->firstSlot[c + 1] += h->firstSlot[c];
    }
    // Fills each cluster's range in place order, using firstLeaving as
    // the cursors for now.
    memcpy(h->firstLeaving, h->firstSlot, ((size_t)h->clusterCount + 1) * sizeof(int32_t));
    for (int32_t i = 0; i < navmesh->placeCount; ++i)
    {
        int32_t slot = navmesh->places[i].slot;
        h->slots[h->firstLeaving[h->clusterOf[slot]]++] = slot;
    }
}

// The point of edge ab whose coordinate along a tile side is u.
static mnavPos3 AlongSide(mnavPos3 a, mnavPos3 b, double au, double bu, double u)
{
    double t = (u - au) / (bu - au);
    return (mnavPos3){a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.z + t * (b.z - a.z)};
}

// The transition at tile link l of the tile in slot, which leaves its
// cluster, as the search crosses it.
static mnavTransition Cross(const mnavHierarchy* h, const mnavNavmesh* navmesh, int32_t slot,
                            int32_t l, int32_t runLow, int32_t runHigh)
{
    const mnavSlot* s = &navmesh->slots[slot];
    const mnavTile* tile = s->tile;
    const mnavLink* link = &tile->links[l];
    const mnavPolygon* polygon = &tile->mesh.polygons[link->polygon];
    int32_t side = polygon->sides[link->edge];
    int32_t x = s->x;
    int32_t z = s->z;
    int32_t facing = 0;
    mnavAcross(side, &x, &z, &facing);
    mnavFrame f = mnavFrameOf(navmesh, s->x, s->z);
    const mnavMeshVertex* va = &tile->mesh.vertices[polygon->vertices[link->edge]];
    const mnavMeshVertex* vb =
        &tile->mesh.vertices[polygon->vertices[(link->edge + 1) % polygon->count]];
    bool alongZ = side == 1 || side == 3;
    double au = alongZ ? va->z : va->x;
    double bu = alongZ ? vb->z : vb->x;
    mnavPos3 a = mnavVertexWorld(&f, va);
    mnavPos3 b = mnavVertexWorld(&f, vb);
    mnavPos3 first = AlongSide(a, b, au, bu, link->low);
    mnavPos3 second = AlongSide(a, b, au, bu, link->high);
    int32_t target = (int32_t)link->target.slot - 1;
    return (mnavTransition){
        {(first.x + second.x) * 0.5, (first.y + second.y) * 0.5, (first.z + second.z) * 0.5},
        target,
        (int32_t)link->target.polygon,
        MNAV_TAG_LINK + facing,
        link->low,
        h->clusterOf[target],
        h->clusterOf[slot],
        slot,
        side,
        runLow,
        runHigh,
        -1,
        false};
}

// Whether tile link l of a tile leaves the cluster across a side.
static bool Leaves(const mnavHierarchy* h, const mnavTile* tile, int32_t slot, int32_t l,
                   int32_t side)
{
    const mnavLink* link = &tile->links[l];
    int32_t target = (int32_t)link->target.slot - 1;
    return tile->mesh.polygons[link->polygon].sides[link->edge] == side &&
           h->clusterOf[target] != h->clusterOf[slot];
}

// Adds the transitions of one side of the tile in slot: the middle link of
// each run of touching links, by their start along the side.
static mnavResult AddSide(mnavHierarchy* h, const mnavNavmesh* navmesh, int32_t slot, int32_t side,
                          uint64_t* sorted, uint64_t* scratch)
{
    const mnavTile* tile = navmesh->slots[slot].tile;
    size_t count = 0;
    for (int32_t l = 0; l < tile->linkCount; ++l)
    {
        if (Leaves(h, tile, slot, l, side))
        {
            sorted[count++] = ((uint64_t)((uint32_t)tile->links[l].low + BIAS) << 32) | (uint32_t)l;
        }
    }
    count = mnavSortUnique(sorted, scratch, count);
    size_t start = 0;
    while (start < count)
    {
        int32_t high = tile->links[(uint32_t)sorted[start]].high;
        size_t end = start + 1;
        while (end < count && tile->links[(uint32_t)sorted[end]].low <= high)
        {
            int32_t next = tile->links[(uint32_t)sorted[end]].high;
            high = next > high ? next : high;
            end += 1;
        }
        if (h->transitionCount == h->def.limits.transitions)
        {
            return mnav_errorLimit;
        }
        int32_t middle = (int32_t)(uint32_t)sorted[start + (end - start) / 2];
        int32_t runLow = tile->links[(uint32_t)sorted[start]].low;
        int32_t runHigh = tile->links[(uint32_t)sorted[end - 1]].low;
        h->transitions[h->transitionCount++] = Cross(h, navmesh, slot, middle, runLow, runHigh);
        start = end;
    }
    return mnav_success;
}

// Adds a transition for each off-mesh link, one way, that the filter
// crosses from one cluster into another, in the attachments' order: by
// takeoff slot and polygon, then link.
static mnavResult AddLinks(mnavHierarchy* h, const mnavNavmesh* navmesh)
{
    for (int32_t i = 0; i < navmesh->attachmentCount; ++i)
    {
        mnavAttachment a = mnavAttachmentOf(navmesh->attachments[i]);
        const mnavOffLink* link = &navmesh->links[a.link];
        const mnavLinkState* state = &link->state;
        mnavPolygonId landing = a.reverse ? state->startPolygon : state->endPolygon;
        int32_t slot = (int32_t)landing.slot - 1;
        const mnavTile* tile = navmesh->slots[slot].tile;
        if (h->clusterOf[slot] == h->clusterOf[a.slot] ||
            !mnavCrosses(&h->filter, link->def.kind) ||
            !mnavIncludes(&h->filter, tile->mesh.polygons[landing.polygon].area))
        {
            continue;
        }
        if (h->transitionCount == h->def.limits.transitions)
        {
            return mnav_errorLimit;
        }
        int32_t low = a.link * 2 + (a.reverse ? 1 : 0);
        h->transitions[h->transitionCount++] =
            (mnavTransition){a.reverse ? state->start : state->end,
                             slot,
                             (int32_t)landing.polygon,
                             MNAV_TAG_OFFMESH,
                             low,
                             h->clusterOf[slot],
                             h->clusterOf[a.slot],
                             a.slot,
                             0,
                             low,
                             low,
                             -1,
                             false};
    }
    return mnav_success;
}

// Finds the transition crossing u's portal the other way; an off-mesh
// link's has none.
static int32_t ReverseOf(const mnavHierarchy* h, int32_t u)
{
    const mnavTransition* t = &h->transitions[u];
    if (t->side == 0)
    {
        return -1;
    }
    int32_t facing = t->side <= 2 ? t->side + 2 : t->side - 2;
    int32_t first = h->firstOfSlot[t->slot];
    for (int32_t v = first; v < h->transitionCount && h->transitions[v].fromSlot == t->slot; ++v)
    {
        const mnavTransition* r = &h->transitions[v];
        if (r->side == facing && r->runLow == t->runLow)
        {
            return v;
        }
    }
    return -1;
}

// Adds every transition, in place order, then side, then position, and
// lists those leaving each cluster.
static mnavResult FindTransitions(mnavHierarchy* h, const mnavNavmesh* navmesh, uint64_t* sorted,
                                  uint64_t* scratch)
{
    h->transitionCount = 0;
    for (int32_t i = 0; i < navmesh->placeCount; ++i)
    {
        int32_t slot = navmesh->places[i].slot;
        h->firstOfSlot[slot] = h->transitionCount;
        for (int32_t side = 1; side <= 4; ++side)
        {
            mnavResult result = AddSide(h, navmesh, slot, side, sorted, scratch);
            if (result != mnav_success)
            {
                return result;
            }
        }
    }
    mnavResult result = AddLinks(h, navmesh);
    if (result != mnav_success)
    {
        return result;
    }
    memset(h->firstLeaving, 0, ((size_t)h->clusterCount + 1) * sizeof(int32_t));
    for (int32_t u = 0; u < h->transitionCount; ++u)
    {
        h->transitions[u].reverse = ReverseOf(h, u);
        h->firstLeaving[h->transitions[u].from + 1] += 1;
    }
    for (int32_t c = 0; c < h->clusterCount; ++c)
    {
        h->firstLeaving[c + 1] += h->firstLeaving[c];
    }
    // Counting sort by the cluster left, keeping transition order; the
    // end costs' slots serve as cursors.
    int32_t* cursors = h->parents;
    memcpy(cursors, h->firstLeaving, ((size_t)h->clusterCount + 1) * sizeof(int32_t));
    for (int32_t u = 0; u < h->transitionCount; ++u)
    {
        h->leaving[cursors[h->transitions[u].from]++] = u;
    }
    return mnav_success;
}

void mnavMarkCluster(mnavHierarchy* h, int32_t cluster, uint8_t value)
{
    for (int32_t i = h->firstSlot[cluster]; i < h->firstSlot[cluster + 1]; ++i)
    {
        h->inside[h->slots[i]] = value;
    }
}

static mnavPolygonId IdOf(const mnavNavmesh* navmesh, int32_t slot, int32_t polygon)
{
    return (mnavPolygonId){(uint32_t)slot + 1, navmesh->slots[slot].generation, (uint32_t)polygon};
}

mnavResult mnavSearchCluster(mnavHierarchy* h, mnavQuery* query, const mnavNavmesh* navmesh,
                             bool backward, int32_t cluster, mnavPolygonId polygon, mnavPos3 point)
{
    mnavResult result = mnavBeginPath(query, navmesh, &h->filter, polygon, point, polygon, point);
    if (result != mnav_success)
    {
        return result;
    }
    mnavMarkCluster(h, cluster, 1);
    mnavConfineSearch(query, h->inside, true, true);
    if (backward)
    {
        mnavTurnSearchBackward(query);
    }
    bool ended = false;
    while (result == mnav_success && !ended)
    {
        result = mnavContinuePath(query, navmesh, INT32_MAX, &ended);
    }
    mnavMarkCluster(h, cluster, 0);
    bool out = query->search.outOfNodes;
    query->search.active = false;
    return result != mnav_success ? result : (out ? mnav_errorLimit : mnav_success);
}

double mnavCostTo(const mnavQuery* query, const mnavTransition* v)
{
    int32_t n = query->table[mnavFindNode(query, v->slot, v->polygon, v->tag, v->low)];
    return n == MNAV_NO_NODE ? (double)INFINITY : query->nodes[n].cost;
}

// Appends transition u's edges to those leaving the cluster it enters,
// found by a search within that cluster, to an edge array.
static mnavResult AddEdges(mnavHierarchy* h, mnavQuery* query, const mnavNavmesh* navmesh,
                           int32_t u, mnavEdge* edges)
{
    const mnavTransition* t = &h->transitions[u];
    h->searches += 1;
    mnavResult result = mnavSearchCluster(h, query, navmesh, false, t->cluster,
                                          IdOf(navmesh, t->slot, t->polygon), t->at);
    if (result != mnav_success)
    {
        return result;
    }
    for (int32_t i = h->firstLeaving[t->cluster]; i < h->firstLeaving[t->cluster + 1]; ++i)
    {
        int32_t v = h->leaving[i];
        double cost = mnavCostTo(query, &h->transitions[v]);
        if (v == t->reverse || !isfinite(cost))
        {
            continue;
        }
        if (h->edgeCount == h->def.limits.edges)
        {
            return mnav_errorLimit;
        }
        edges[h->edgeCount++] = (mnavEdge){v, cost};
    }
    return mnav_success;
}

// Finds each transition's edges to those leaving the cluster it enters.
static mnavResult FindEdges(mnavHierarchy* h, mnavQuery* query, const mnavNavmesh* navmesh)
{
    h->edgeCount = 0;
    for (int32_t u = 0; u < h->transitionCount; ++u)
    {
        h->firstEdge[u] = h->edgeCount;
        mnavResult result = AddEdges(h, query, navmesh, u, h->edges);
        if (result != mnav_success)
        {
            return result;
        }
    }
    h->firstEdge[h->transitionCount] = h->edgeCount;
    return mnav_success;
}

// The scratch for sorting one tile's links: the largest link count.
static size_t MostLinks(const mnavNavmesh* navmesh)
{
    size_t most = 1;
    for (int32_t i = 0; i < navmesh->placeCount; ++i)
    {
        size_t count = (size_t)navmesh->slots[navmesh->places[i].slot].tile->linkCount;
        most = count > most ? count : most;
    }
    return most;
}

static mnavResult Build(mnavHierarchy* h, mnavQuery* query, const mnavNavmesh* navmesh)
{
    if (navmesh->slotCount > h->def.limits.tiles)
    {
        return mnav_errorLimit;
    }
    FindClusters(h, navmesh);
    size_t most = MostLinks(navmesh);
    uint64_t* sorted = nullptr;
    uint64_t* scratch = nullptr;
    mnavResult result = mnav_success;
    Take(h, &result, most, sizeof(uint64_t), alignof(uint64_t), (void**)&sorted);
    Take(h, &result, most, sizeof(uint64_t), alignof(uint64_t), (void**)&scratch);
    if (result == mnav_success)
    {
        result = FindTransitions(h, navmesh, sorted, scratch);
    }
    Give(h, sorted, most, sizeof(uint64_t), alignof(uint64_t));
    Give(h, scratch, most, sizeof(uint64_t), alignof(uint64_t));
    return result == mnav_success ? FindEdges(h, query, navmesh) : result;
}

// The hash of a tile's polygon areas.
static uint64_t AreaHash(const mnavTile* tile)
{
    uint64_t hash = MNAV_HASH_INIT;
    for (int32_t p = 0; p < tile->mesh.polygonCount; ++p)
    {
        hash = mnavHash64(hash, &tile->mesh.polygons[p].area, (int32_t)sizeof(mnavAreaType));
    }
    return hash;
}

// The hash of the attachments and of what the search reads of their
// links: kind, cost, ends and the polygons they snapped to.
static uint64_t LinkHash(const mnavNavmesh* navmesh)
{
    uint64_t hash = mnavHash64(MNAV_HASH_INIT, navmesh->attachments,
                               (int32_t)((size_t)navmesh->attachmentCount * sizeof(uint64_t)));
    for (int32_t i = 0; i < navmesh->attachmentCount; ++i)
    {
        const mnavOffLink* link = &navmesh->links[mnavAttachmentOf(navmesh->attachments[i]).link];
        hash = mnavHash64(hash, &link->def.kind, (int32_t)sizeof(link->def.kind));
        hash = mnavHash64(hash, &link->def.cost, (int32_t)sizeof(link->def.cost));
        hash = mnavHash64(hash, &link->state.start, (int32_t)sizeof(mnavPos3));
        hash = mnavHash64(hash, &link->state.end, (int32_t)sizeof(mnavPos3));
        hash = mnavHash64(hash, &link->state.startPolygon, (int32_t)sizeof(mnavPolygonId));
        hash = mnavHash64(hash, &link->state.endPolygon, (int32_t)sizeof(mnavPolygonId));
    }
    return hash;
}

// Whether the hierarchy's filter includes the polygon transition u enters.
static bool Included(const mnavHierarchy* h, const mnavNavmesh* navmesh, int32_t u)
{
    const mnavTransition* t = &h->transitions[u];
    const mnavTile* tile = navmesh->slots[t->slot].tile;
    return mnavIncludes(&h->filter, tile->mesh.polygons[t->polygon].area);
}

// Keeps what the graph is built from: each slot's tile generation and
// areas, the links, and whether each transition's polygon is included.
static void Stamp(mnavHierarchy* h, const mnavNavmesh* navmesh)
{
    for (int32_t u = 0; u < h->transitionCount; ++u)
    {
        h->transitions[u].included = Included(h, navmesh, u);
    }
    memset(h->generations, 0, (size_t)navmesh->slotCount * sizeof(uint32_t));
    for (int32_t i = 0; i < navmesh->placeCount; ++i)
    {
        int32_t slot = navmesh->places[i].slot;
        h->generations[slot] = navmesh->slots[slot].generation;
        h->areaHashes[slot] = AreaHash(navmesh->slots[slot].tile);
    }
    h->linkHash = LinkHash(navmesh);
}

// Whether the tiles and links are those the graph was built from, so
// that only areas may have changed.
static bool SameShape(const mnavHierarchy* h, const mnavNavmesh* navmesh)
{
    if (navmesh->slotCount > h->def.limits.tiles || h->linkHash != LinkHash(navmesh))
    {
        return false;
    }
    int32_t held = 0;
    for (int32_t s = 0; s < navmesh->slotCount; ++s)
    {
        held += h->generations[s] != 0 ? 1 : 0;
    }
    for (int32_t i = 0; i < navmesh->placeCount; ++i)
    {
        int32_t slot = navmesh->places[i].slot;
        if (h->generations[slot] != navmesh->slots[slot].generation)
        {
            return false;
        }
    }
    return held == navmesh->placeCount;
}

// Marks the clusters holding a tile whose areas changed; whether a link
// lands in one, which may change the transitions. A transition's
// crossing lies in the tile it enters, and the searches of the cluster it
// leaves end there, opening the polygon entered but never walking it: its
// area costs them nothing, so that cluster is marked only when the
// filter has come to include or exclude it.
static bool MarkDirty(mnavHierarchy* h, const mnavNavmesh* navmesh)
{
    memset(h->dirty, 0, (size_t)h->clusterCount);
    memset(h->changed, 0, (size_t)navmesh->slotCount);
    for (int32_t i = 0; i < navmesh->placeCount; ++i)
    {
        int32_t slot = navmesh->places[i].slot;
        uint64_t hash = AreaHash(navmesh->slots[slot].tile);
        if (hash != h->areaHashes[slot])
        {
            h->changed[slot] = 1;
            h->dirty[h->clusterOf[slot]] = 1;
            h->areaHashes[slot] = hash;
        }
    }
    for (int32_t i = 0; i < navmesh->attachmentCount; ++i)
    {
        mnavAttachment a = mnavAttachmentOf(navmesh->attachments[i]);
        const mnavLinkState* state = &navmesh->links[a.link].state;
        mnavPolygonId landing = a.reverse ? state->startPolygon : state->endPolygon;
        if (h->dirty[h->clusterOf[landing.slot - 1]] != 0)
        {
            return true;
        }
    }
    for (int32_t u = 0; u < h->transitionCount; ++u)
    {
        mnavTransition* tr = &h->transitions[u];
        bool now = h->changed[tr->slot] != 0 ? Included(h, navmesh, u) : tr->included;
        h->dirty[tr->from] = now != tr->included ? 1 : h->dirty[tr->from];
        tr->included = now;
    }
    return false;
}

// Searches again from the transitions entering dirty clusters, keeping
// the other edges, through a scratch edge array.
static mnavResult Research(mnavHierarchy* h, mnavQuery* query, const mnavNavmesh* navmesh)
{
    size_t limit = (size_t)h->def.limits.edges;
    mnavEdge* edges = nullptr;
    mnavResult result = mnav_success;
    Take(h, &result, limit, sizeof(mnavEdge), alignof(mnavEdge), (void**)&edges);
    int32_t count = 0;
    for (int32_t u = 0; u < h->transitionCount && result == mnav_success; ++u)
    {
        int32_t first = h->firstEdge[u];
        int32_t last = h->firstEdge[u + 1];
        h->firstEdge[u] = count;
        if (h->dirty[h->transitions[u].cluster] != 0)
        {
            h->edgeCount = count;
            result = AddEdges(h, query, navmesh, u, edges);
            count = h->edgeCount;
            continue;
        }
        if ((size_t)count + (size_t)(last - first) > limit)
        {
            result = mnav_errorLimit;
            break;
        }
        memcpy(&edges[count], &h->edges[first], (size_t)(last - first) * sizeof(mnavEdge));
        count += last - first;
    }
    if (result == mnav_success)
    {
        h->firstEdge[h->transitionCount] = count;
        h->edgeCount = count;
        memcpy(h->edges, edges, (size_t)count * sizeof(mnavEdge));
    }
    Give(h, edges, limit, sizeof(mnavEdge), alignof(mnavEdge));
    return result;
}

// Builds the graph from nothing, keeping what it was built from.
static mnavResult Rebuild(mnavHierarchy* h, mnavQuery* query, const mnavNavmesh* navmesh)
{
    h->searches = 0;
    h->scale = mnavHeuristicScale(navmesh, &h->filter);
    mnavResult result = Build(h, query, navmesh);
    if (result == mnav_success)
    {
        Stamp(h, navmesh);
    }
    return result;
}

// Ends a build or update: the graph for this commit on success, none on
// an error; and the report.
static mnavResult Finish(mnavHierarchy* h, const mnavNavmesh* navmesh, mnavResult result,
                         mnavHierarchyReport* reportOut)
{
    if (result != mnav_success)
    {
        h->navmesh = nullptr;
        return result;
    }
    h->navmesh = navmesh;
    h->commits = navmesh->commits;
    if (reportOut != nullptr)
    {
        *reportOut =
            (mnavHierarchyReport){h->clusterCount, h->transitionCount, h->edgeCount, h->searches};
    }
    return mnav_success;
}

mnavResult mnavBuildHierarchy(mnavHierarchy* hierarchy, mnavQuery* query,
                              const mnavNavmesh* navmesh, const mnavQueryFilter* filter,
                              mnavHierarchyReport* reportOut)
{
    if (hierarchy == nullptr || query == nullptr || navmesh == nullptr)
    {
        return mnav_errorInvalid;
    }
    hierarchy->navmesh = nullptr;
    const mnavQueryFilter* usable = nullptr;
    mnavResult result = mnavCheckFilter(filter, &usable);
    if (result != mnav_success)
    {
        return result;
    }
    hierarchy->filter = *usable;
    return Finish(hierarchy, navmesh, Rebuild(hierarchy, query, navmesh), reportOut);
}

mnavResult mnavUpdateHierarchy(mnavHierarchy* hierarchy, mnavQuery* query,
                               const mnavNavmesh* navmesh, mnavHierarchyReport* reportOut)
{
    if (hierarchy == nullptr || query == nullptr || navmesh == nullptr ||
        hierarchy->navmesh != navmesh)
    {
        return mnav_errorInvalid;
    }
    mnavHierarchy* h = hierarchy;
    h->navmesh = nullptr;
    h->searches = 0;
    if (h->commits == navmesh->commits)
    {
        return Finish(h, navmesh, mnav_success, reportOut);
    }
    if (!SameShape(h, navmesh) || MarkDirty(h, navmesh))
    {
        return Finish(h, navmesh, Rebuild(h, query, navmesh), reportOut);
    }
    return Finish(h, navmesh, Research(h, query, navmesh), reportOut);
}
