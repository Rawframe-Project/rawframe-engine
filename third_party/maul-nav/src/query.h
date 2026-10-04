// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A query context's insides, shared by the queries that use it.

#ifndef MAUL_NAV_SRC_QUERY_H
#define MAUL_NAV_SRC_QUERY_H

#include "allocator.h"

#include "maul-nav/base.h"
#include "maul-nav/navmesh.h"
#include "maul-nav/query.h"

#include <stdint.h>

// A node's way in: an edge index below 8, 8 plus the side for a tile link,
// an off-mesh link (its low is the link's slot times 2, plus 1 when crossed
// backward), or the start or end point.
#define MNAV_TAG_LINK    8
#define MNAV_TAG_OFFMESH 16
#define MNAV_TAG_START   0xFE
#define MNAV_TAG_END     0xFF

// No node, or a node that has left the open list.
#define MNAV_NO_NODE (-1)

// A portal into a polygon, or the start or end point: where the search
// stands, the way it came, and its cost so far.
typedef struct mnavSearchNode
{
    // The portal's ends, in the order of the polygon left, and its midpoint;
    // for an off-mesh link, its takeoff and landing points, and the landing.
    mnavPos3 a;
    mnavPos3 b;
    mnavPos3 at;
    // The cost so far, the length so far in meters, and the heuristic: the
    // distance to the end times the cheapest included area's cost.
    double cost;
    double length;
    double remaining;
    // The polygon entered: its 0-based slot and index.
    int32_t slot;
    int32_t polygon;
    // The portal: TAG_ values, and the link's start along the side.
    int32_t tag;
    int32_t low;
    int32_t parent;
    // The node's place in the open list, or MNAV_NO_NODE once closed.
    int32_t heap;
} mnavSearchNode;

// A grid search's node (mnav-0005): a cell reached, the way it came, its
// cost and length so far and the heuristic, and its place in the open
// list, MNAV_NO_NODE before it enters it, or MNAV_CLOSED.
typedef struct mnavGridNode
{
    int32_t x;
    int32_t y;
    int32_t parent;
    int32_t heap;
    double cost;
    double length;
    double remaining;
} mnavGridNode;

// A grid node out of the open list, its cost final.
#define MNAV_CLOSED (-2)

// A portal's ends as the funnel sees them, walking into the polygon: the
// left one, then the right one.
typedef struct mnavPortal
{
    mnavPos3 left;
    mnavPos3 right;
    // At a takeoff point, the off-mesh link crossed next, as the search
    // node's low; -1 otherwise.
    int32_t link;
} mnavPortal;

// A search in progress: the end it heads for, what stopped ways on, and
// what it has found; kept in the context between slices.
typedef struct mnavSearch
{
    mnavQuery* query;
    const mnavNavmesh* navmesh;
    // The navmesh's commits when the search began.
    uint64_t commits;
    const mnavQueryFilter* filter;
    // The heuristic's scale (mnav-0005).
    double cheapest;
    mnavPos3 end;
    int32_t endSlot;
    int32_t endPolygon;
    double limit;
    bool outOfNodes;
    bool tooLong;
    bool notLoaded;
    bool active;
    // The node nearest the end so far, and the end's node once found.
    int32_t best;
    int32_t found;
    // A confined search's tiles, a flag per slot, or NULL for all; with
    // beyond set, nodes outside are opened but never expanded.
    const uint8_t* inside;
    bool beyond;
    // The nodes whose closing ends the search, its tag -1 for none: a
    // slot, a tag and a range of lows, as nodes' (mnav-0008).
    int32_t goal[4];
} mnavSearch;

struct mnavQuery
{
    mnavMemory memory;
    mnavQueryLimits limits;
    // One block of nodes serves a navmesh or a grid search, whichever ran
    // last; grid nodes are the smaller.
    union
    {
        mnavSearchNode* nodes;
        mnavGridNode* gridNodes;
    };
    int32_t nodeCount;
    int32_t* heap;
    int32_t heapCount;
    // Open addressing over node keys: node indices, MNAV_NO_NODE for empty.
    int32_t* table;
    uint32_t tableMask;
    mnavPolygonId* corridor;
    // The corridor's portals, then the straight path: a node gives one
    // portal, an off-mesh link two, and the end one more, so twice the
    // nodes plus one bound both. Then the links crossed.
    mnavPortal* portals;
    // The straight path's points, or a grid path's cells.
    union
    {
        mnavPos3* points;
        mnavCell* cells;
    };
    mnavPathLink* links;
    // The search, and its own copy of the filter it began with.
    mnavSearch search;
    mnavQueryFilter filter;
};

// The table cell holding the node with a key, or the empty cell where it
// would go.
uint32_t mnavFindNode(const mnavQuery* query, int32_t slot, int32_t polygon, int32_t tag,
                      int32_t low);

// Confines the search just begun to the slots flagged in inside; with
// beyond, nodes outside are opened but never expanded; with no end, the
// search is Dijkstra's and never reaches the end, so that it runs until
// its open list empties (mnav-0008).
void mnavConfineSearch(mnavQuery* query, const uint8_t* inside, bool beyond, bool noEnd);

// Aims the search at another point: the end point, and the end polygon's
// slot and index, or -1 for an end never reached; and at the nodes whose
// closing ends it, goal's slot, tag and lowest and highest low, its tag
// -1 for none (mnav-0008).
void mnavAimSearch(mnavQuery* query, mnavPos3 end, int32_t endSlot, int32_t endPolygon,
                   const int32_t goal[4]);

// Starts the search over from node n, as it was reached: keeps only the
// nodes on its way, closed, and opens n alone (mnav-0008).
void mnavRestartSearch(mnavQuery* query, int32_t n);

// The search heuristic's scale: the cheapest included area's cost, or less
// for a kind of link the filter crosses that costs less per meter.
double mnavHeuristicScale(const mnavNavmesh* navmesh, const mnavQueryFilter* filter);

#endif // MAUL_NAV_SRC_QUERY_H
