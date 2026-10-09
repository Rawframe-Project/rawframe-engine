// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Hierarchies (mnav-0008): the graph shared by its build and its queries,
// and the searches within one cluster both use.

#ifndef MAUL_NAV_SRC_HIERARCHY_H
#define MAUL_NAV_SRC_HIERARCHY_H

#include "allocator.h"
#include "query.h"

#include "maul-nav/base.h"
#include "maul-nav/hierarchy.h"
#include "maul-nav/navmesh.h"
#include "maul-nav/query.h"

#include <stdint.h>

// A transition: crossing a portal from one cluster into another.
typedef struct mnavTransition
{
    // The portal's midpoint, where the transition stands.
    mnavPos3 at;
    // The search node of the crossing: the polygon entered, its slot, the
    // tag and the link's start along the side.
    int32_t slot;
    int32_t polygon;
    int32_t tag;
    int32_t low;
    // The clusters entered and left, the slot left, its side and the
    // lowest and highest start of the run's links along it.
    int32_t cluster;
    int32_t from;
    int32_t fromSlot;
    int32_t side;
    int32_t runLow;
    int32_t runHigh;
    // The transition crossing the same portal the other way, or -1.
    int32_t reverse;
    // Whether the hierarchy's filter included the polygon entered when
    // the graph was last brought up to date: all the searches of the
    // cluster left see of that polygon.
    bool included;
} mnavTransition;

// An edge: the transition it leads to and what reaching it costs.
typedef struct mnavEdge
{
    int32_t to;
    double cost;
} mnavEdge;

struct mnavHierarchy
{
    mnavMemory memory;
    mnavHierarchyDef def;
    // The graph's navmesh and its commits when built; NULL with no graph.
    const mnavNavmesh* navmesh;
    uint64_t commits;
    mnavQueryFilter filter;
    double scale;
    // Per slot: its cluster, or -1, its place's first transition, and
    // whether a confined search may expand it.
    int32_t* clusterOf;
    int32_t* firstOfSlot;
    uint8_t* inside;
    // Clusters: their sort keys, their slots and the transitions leaving
    // them, each as ranges of the arrays after.
    uint64_t* keys;
    uint64_t* scratch;
    int32_t clusterCount;
    int32_t* firstSlot;
    int32_t* slots;
    int32_t* firstLeaving;
    int32_t* leaving;
    mnavTransition* transitions;
    int32_t transitionCount;
    int32_t* firstEdge;
    mnavEdge* edges;
    int32_t edgeCount;
    // What the graph was built from: per slot, the tile's generation, 0
    // for none, and its areas' hash; the off-mesh links' hash; per slot,
    // whether an update found its areas changed; and per cluster, whether
    // an update searches it again.
    uint32_t* generations;
    uint64_t* areaHashes;
    uint64_t linkHash;
    uint8_t* changed;
    uint8_t* dirty;
    int32_t searches;
    // The abstract search: per transition, and one more for the end.
    double* costs;
    double* joins;
    int32_t* parents;
    int32_t* places;
    int32_t* heap;
    int32_t heapCount;
};

// Sets the confinement flag of every slot of a cluster.
void mnavMarkCluster(mnavHierarchy* h, int32_t cluster, uint8_t value);

// Runs Dijkstra's search from a point in a polygon within a cluster,
// opening the nodes just beyond it, backward when asked (off-mesh links
// followed from where they land); mnav_errorLimit when it runs out of
// nodes.
mnavResult mnavSearchCluster(mnavHierarchy* h, mnavQuery* query, const mnavNavmesh* navmesh,
                             bool backward, int32_t cluster, mnavPolygonId polygon, mnavPos3 point);

// The cost the last search found to cross transition v's portal, or
// infinity.
double mnavCostTo(const mnavQuery* query, const mnavTransition* v);

#endif // MAUL_NAV_SRC_HIERARCHY_H
