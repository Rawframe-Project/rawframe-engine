// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Hierarchical paths (mnav-0008): an abstract graph over clusters of
// tiles, whose transitions stand at the portals between clusters, for
// long paths on large navmeshes; a path found on it is refined by the
// navmesh search confined to the clusters it crosses.

#ifndef MAUL_NAV_HIERARCHY_H
#define MAUL_NAV_HIERARCHY_H

#include "maul-nav/base.h"
#include "maul-nav/navmesh.h"
#include "maul-nav/query.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The most tiles, transitions and edges a hierarchy may hold, and the
// widest cluster, in tiles.
#define MNAV_MAX_HIERARCHY_TILES       1048576
#define MNAV_MAX_HIERARCHY_TRANSITIONS 4194304
#define MNAV_MAX_HIERARCHY_EDGES       67108864
#define MNAV_MAX_CLUSTER_TILES         64

    // What a hierarchy may hold.
    typedef struct mnavHierarchyLimits
    {
        // Tile slots of the navmesh, 1 to MNAV_MAX_HIERARCHY_TILES.
        int32_t tiles;
        // Transitions, 1 to MNAV_MAX_HIERARCHY_TRANSITIONS.
        int32_t transitions;
        // Edges between transitions, 1 to MNAV_MAX_HIERARCHY_EDGES.
        int32_t edges;
    } mnavHierarchyLimits;

    // How a hierarchy is made. Build it with mnavDefaultHierarchyDef.
    typedef struct mnavHierarchyDef
    {
        uint32_t cookie;
        // The allocator the hierarchy uses; zeroed for the C library's.
        mnavAllocator allocator;
        mnavHierarchyLimits limits;
        // A cluster's side, in tiles, 1 to MNAV_MAX_CLUSTER_TILES.
        int32_t clusterTiles;
    } mnavHierarchyDef;

    // A hierarchy: the abstract graph for one navmesh and filter.
    typedef struct mnavHierarchy mnavHierarchy;

    // What a build or an update made, and the searches within clusters it
    // ran.
    typedef struct mnavHierarchyReport
    {
        int32_t clusters;
        int32_t transitions;
        int32_t edges;
        int32_t searches;
    } mnavHierarchyReport;

    /// Returns the default hierarchy def: clusters of 4 by 4 tiles, up to
    /// 4,096 tile slots, 16,384 transitions and 262,144 edges.
    ///
    /// @return The def.
    /// @par Thread safety
    /// Safe from any thread.
    MNAV_API mnavHierarchyDef mnavDefaultHierarchyDef(void);

    /// Makes a hierarchy with the memory its limits need.
    ///
    /// @param def          The def, from mnavDefaultHierarchyDef.
    /// @param hierarchyOut Receives the hierarchy, or NULL on failure.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument or a
    /// def not from mnavDefaultHierarchyDef; `mnav_errorRange` for a limit
    /// or cluster side out of its range; `mnav_errorCapacity` when the
    /// allocator fails.
    /// @par Thread safety
    /// Safe from any thread.
    MNAV_NODISCARD MNAV_API mnavResult mnavCreateHierarchy(const mnavHierarchyDef* def,
                                                           mnavHierarchy** hierarchyOut);

    /// Destroys a hierarchy.
    ///
    /// @param hierarchy The hierarchy, or NULL.
    /// @par Thread safety
    /// Safe from any thread; the hierarchy is used by one thread at a time.
    MNAV_API void mnavDestroyHierarchy(mnavHierarchy* hierarchy);

    /// Builds the hierarchy for a navmesh as committed and a filter. Each
    /// run of tile links along a cluster border, one way, is an entrance
    /// whose middle link is a transition; each transition's edges to the
    /// transitions leaving the cluster it enters cost what the navmesh
    /// search finds within that cluster. Transitions are numbered by tile
    /// place, side and position, so the graph does not depend on the order
    /// tiles were loaded in. Each off-mesh link, one way, that the filter
    /// crosses from one cluster into another is a transition too. A later
    /// commit to the navmesh makes the hierarchy stale until it is built
    /// again.
    ///
    /// @param hierarchy The hierarchy; its last graph is replaced.
    /// @param query     A context for the searches within clusters; its
    ///                  last search ends.
    /// @param navmesh   The navmesh.
    /// @param filter    The areas usable and their costs, or NULL; the
    ///                  hierarchy keeps a copy.
    /// @param reportOut Receives what was made. May be NULL.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument or
    /// a filter not built from mnavDefaultQueryFilter; `mnav_errorRange`
    /// for a filter cost out of its range; `mnav_errorLimit` for more tile
    /// slots, transitions or edges than the limits, or a cluster needing
    /// more nodes than the context's limit. On an error the hierarchy holds
    /// no graph.
    /// @par Thread safety
    /// Safe from any thread; the hierarchy and the context are used by one
    /// thread at a time, and no commit runs on the navmesh.
    MNAV_NODISCARD MNAV_API mnavResult mnavBuildHierarchy(mnavHierarchy* hierarchy,
                                                          mnavQuery* query,
                                                          const mnavNavmesh* navmesh,
                                                          const mnavQueryFilter* filter,
                                                          mnavHierarchyReport* reportOut);

    /// Brings the hierarchy up to the navmesh's last commit. When only
    /// polygon areas changed, only the edges of transitions entering the
    /// clusters whose tiles changed are searched again; when tiles or
    /// off-mesh links changed, the hierarchy is built again. The graph is
    /// the one a build would make.
    ///
    /// @param hierarchy The hierarchy, built for this navmesh.
    /// @param query     A context for the searches within clusters; its
    ///                  last search ends.
    /// @param navmesh   The navmesh.
    /// @param reportOut Receives what the graph holds and the searches run.
    ///                  May be NULL.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, a
    /// hierarchy with no graph, or one built for another navmesh;
    /// `mnav_errorLimit` as mnavBuildHierarchy. On an error the hierarchy
    /// holds no graph.
    /// @par Thread safety
    /// Safe from any thread; the hierarchy and the context are used by one
    /// thread at a time, and no commit runs on the navmesh.
    MNAV_NODISCARD MNAV_API mnavResult mnavUpdateHierarchy(mnavHierarchy* hierarchy,
                                                           mnavQuery* query,
                                                           const mnavNavmesh* navmesh,
                                                           mnavHierarchyReport* reportOut);

    /// Finds a path as mnavFindPath does, with the hierarchy's filter,
    /// through the hierarchy: the start and the end join their clusters'
    /// transitions by searches within those clusters, A* over the
    /// transitions picks the clusters to cross, and the navmesh search
    /// confined to them gives the path. Paths within one cluster, and ends
    /// the graph does not reach, take the plain search. Paths are near the
    /// cheapest, not always the cheapest.
    ///
    /// @param query        The context; its memory holds the path.
    /// @param hierarchy    The hierarchy, built for this navmesh.
    /// @param navmesh      The navmesh.
    /// @param startPolygon The polygon the start lies in.
    /// @param start        The start point.
    /// @param endPolygon   The polygon the end lies in.
    /// @param end          The end point.
    /// @param pathOut      Receives the path.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, a
    /// point not finite, a hierarchy with no graph, or one built for
    /// another navmesh; `mnav_errorStale` for a navmesh committed since the
    /// build; the errors of mnavFindPath otherwise.
    /// @par Thread safety
    /// Safe from any thread; the context and the hierarchy are used by one
    /// thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult
    mnavFindHierarchicalPath(mnavQuery* query, mnavHierarchy* hierarchy, const mnavNavmesh* navmesh,
                             mnavPolygonId startPolygon, mnavPos3 start, mnavPolygonId endPolygon,
                             mnavPos3 end, mnavPath* pathOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_NAV_HIERARCHY_H
