// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Queries over a navmesh's committed tiles (mnav-0005). Every query reads
// and never changes the navmesh, and works in world coordinates: meters,
// right-handed, +Y up.

#ifndef MAUL_NAV_QUERY_H
#define MAUL_NAV_QUERY_H

#include "maul-nav/base.h"
#include "maul-nav/navmesh.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The cheapest and dearest an area's cost may be.
#define MNAV_MIN_AREA_COST 0.001f
#define MNAV_MAX_AREA_COST 1000000.0f

    // Which polygons a query may use and what crossing them costs, by area
    // type (mnav-0002). Build it with mnavDefaultQueryFilter.
    typedef struct mnavQueryFilter
    {
        uint32_t cookie;
        // MNAV_ABI_VERSION, set by mnavDefaultQueryFilter.
        uint32_t version;
        // What a meter costs in each area type, MNAV_MIN_AREA_COST to
        // MNAV_MAX_AREA_COST; 1 by default.
        float costs[MNAV_AREA_TYPES];
        // Bit n set when polygons of area type n may be used; every
        // walkable type by default. Bit 0 is ignored: area 0 is never
        // walkable.
        uint64_t areas;
        // Bit n set when the agent may cross off-mesh links of kind n; every
        // kind by default.
        uint64_t kinds;
    } mnavQueryFilter;

// The cookie mnavDefaultQueryFilter sets and every query checks.
#define MNAV_QUERY_FILTER_COOKIE 0x4E415646u

    /// Returns the filter that uses every walkable area at a cost of 1 and
    /// every kind of off-mesh link. Built in the program, so that it
    /// carries the version of the headers the program includes.
    ///
    /// @return The filter.
    /// @par Thread safety
    /// Safe from any thread.
    static inline mnavQueryFilter mnavDefaultQueryFilter(void)
    {
#ifdef __cplusplus
        mnavQueryFilter filter = {};
#else
    mnavQueryFilter filter = {0};
#endif
        filter.cookie = MNAV_QUERY_FILTER_COOKIE;
        filter.version = MNAV_ABI_VERSION;
        for (int area = 0; area < MNAV_AREA_TYPES; ++area)
        {
            filter.costs[area] = 1.0f;
        }
        // Every walkable area type: all but area 0.
        filter.areas = ~(uint64_t)1;
        filter.kinds = ~(uint64_t)0;
        return filter;
    }

    // The nearest point on the navmesh to a query point.
    typedef struct mnavNearest
    {
        // The polygon holding it; slot 0 when no polygon has its nearest
        // point in the box.
        mnavPolygonId polygon;
        // The point, on the polygon's detail surface.
        mnavPos3 point;
        // Whether the query point lies over or under the polygon, so that
        // the point is straight above or below it.
        bool over;
        // Whether part of the search box lies on places with no tile
        // loaded, where a nearer polygon may be.
        bool incomplete;
    } mnavNearest;

    // What a box query found.
    typedef struct mnavFound
    {
        // The polygons found, also beyond the buffer's capacity.
        int32_t count;
        // Whether part of the box lies on places with no tile loaded.
        bool incomplete;
    } mnavFound;

    /// Lists the polygons the filter includes whose bounds meet a box:
    /// their vertices on the ground and their detail's heights. They come
    /// by tile, in place order (x, then z), then by polygon index.
    ///
    /// @param navmesh     The navmesh.
    /// @param filter      The areas wanted, or NULL for every walkable one;
    ///                    one including mnav_areaNone finds blocked polygons.
    /// @param center      The box's center.
    /// @param halfExtents The box's half sizes, in meters, at least 0.
    /// @param polygonsOut Receives up to capacity ids; NULL when capacity
    ///                    is 0.
    /// @param capacity    The room, at least 0.
    /// @param foundOut    Receives the count and whether the box was all
    ///                    loaded.
    /// @return `mnav_success`; `mnav_errorCapacity` when more polygons meet
    /// the box than the buffer holds: it holds the first ones and foundOut
    /// counts all; `mnav_errorInvalid` for a NULL argument, a negative
    /// capacity, a center or extent that is not finite or a negative
    /// extent, or a filter not built from mnavDefaultQueryFilter;
    /// `mnav_errorRange` for a filter cost out of its range.
    /// @par Thread safety
    /// Safe from any thread. Any number of queries may run at once between
    /// commits.
    MNAV_NODISCARD MNAV_API mnavResult mnavFindPolygons(const mnavNavmesh* navmesh,
                                                        const mnavQueryFilter* filter,
                                                        mnavPos3 center, mnavVec3 halfExtents,
                                                        mnavPolygonId* polygonsOut,
                                                        int32_t capacity, mnavFound* foundOut);

    /// Finds the polygon, among those the filter includes, nearest a point
    /// whose nearest point lies within a box round it, and that point. A
    /// point over a polygon scores the height it lies beyond the agent's
    /// step, any other the distance to the polygon; ties go to the shorter
    /// distance, then the tile first by place (x, then z), then the lower
    /// polygon index.
    ///
    /// @param navmesh      The navmesh.
    /// @param filter       The areas usable, or NULL for every walkable one.
    /// @param point        The query point.
    /// @param halfExtents  The box's half sizes, in meters, at least 0.
    /// @param nearestOut   Receives the result.
    /// @return `mnav_success`, also when no polygon's nearest point lies in
    /// the box (the polygon's slot is then 0); `mnav_errorInvalid` for a NULL
    /// argument, a point or extent that is not finite, or a negative
    /// extent; `mnav_errorInvalid` for a filter not built from
    /// mnavDefaultQueryFilter; `mnav_errorRange` for a filter cost out of its
    /// range.
    /// @par Thread safety
    /// Safe from any thread. Any number of queries may run at once between
    /// commits.
    MNAV_NODISCARD MNAV_API mnavResult mnavFindNearest(const mnavNavmesh* navmesh,
                                                       const mnavQueryFilter* filter,
                                                       mnavPos3 point, mnavVec3 halfExtents,
                                                       mnavNearest* nearestOut);

// The largest nodes one search may use.
#define MNAV_MAX_QUERY_NODES 1048576
// The longest path length limit, in meters.
#define MNAV_MAX_PATH_LENGTH 1.0e7f

    // A query context: the scratch memory searches use, sized by its
    // limits when made, so searches never allocate. Made by
    // mnavCreateQuery; one thread uses a context at a time.
    typedef struct mnavQuery mnavQuery;

    // The named limits that bound a search's work.
    typedef struct mnavQueryLimits
    {
        // Nodes one search may open, 1 to MNAV_MAX_QUERY_NODES; a node is
        // a polygon entered through one of its edges.
        int32_t nodes;
        // The longest path searched, in meters, more than 0 and at most
        // MNAV_MAX_PATH_LENGTH: no node is opened whose way from the start
        // and on to the end is longer.
        float pathLength;
    } mnavQueryLimits;

    // How a query context is made. Build it with mnavDefaultQueryDef.
    typedef struct mnavQueryDef
    {
        uint32_t cookie;
        // MNAV_ABI_VERSION, set by mnavDefaultQueryDef.
        uint32_t version;
        // The allocator the context uses; zeroed for the C library's.
        mnavAllocator allocator;
        // The limits on each search.
        mnavQueryLimits limits;
    } mnavQueryDef;

    // How a path search ended, checked in this order.
    typedef uint8_t mnavPathEnd;

    enum
    {
        // The end point was reached.
        mnav_pathFound = 0,
        // The search used every node of its budget before reaching it.
        mnav_pathOutOfNodes = 1,
        // Every way on was longer than the path length limit.
        mnav_pathTooLong = 2,
        // Every way on ran into places with no tile loaded.
        mnav_pathNotLoaded = 3,
        // The end point cannot be reached from the start.
        mnav_pathNone = 4,
        // The caller finished a sliced search before it ended.
        mnav_pathUnfinished = 5,
    };

    // An off-mesh link a path crosses: the link, its kind, and the index of
    // its takeoff point in the straight path; the landing point follows it.
    typedef struct mnavPathLink
    {
        mnavLinkId link;
        mnavLinkKind kind;
        int32_t point;
    } mnavPathLink;

    // A path search's result. Short of the end point, the corridor runs to
    // the polygon nearest it.
    typedef struct mnavPath
    {
        mnavPathEnd end;
        // The cost of the way searched, through the midpoints of the edges
        // the corridor crosses: each step's length times the cost of the
        // area it lies in.
        double cost;
        // That way's length in meters; the straight path is never longer.
        double length;
        // The polygons from the start polygon on, each visit once: a polygon
        // an off-mesh link leaves and lands back on appears before the link
        // and after it. In the context's memory until its next search.
        const mnavPolygonId* polygons;
        int32_t polygonCount;
        // The straight path from the start point, the corridor pulled
        // tight: its corners and, last, the end point or, short of it, the
        // midpoint of the last edge crossed. In the context's memory until
        // its next search; never cut short.
        const mnavPos3* points;
        int32_t pointCount;
        // The off-mesh links crossed, in order, in the context's memory
        // until its next search.
        const mnavPathLink* links;
        int32_t linkCount;
    } mnavPath;

// The cookie mnavDefaultQueryDef sets and mnavCreateQuery checks.
#define MNAV_QUERY_DEF_COOKIE 0x4E415651u

    /// Returns a query def with 8,192 nodes per search, paths up to
    /// 1,000 m and the C library's allocator. Built in the program, so
    /// that it carries the version of the headers the program includes.
    ///
    /// @return The def.
    /// @par Thread safety
    /// Safe from any thread.
    static inline mnavQueryDef mnavDefaultQueryDef(void)
    {
#ifdef __cplusplus
        mnavQueryDef def = {};
#else
    mnavQueryDef def = {0};
#endif
        def.cookie = MNAV_QUERY_DEF_COOKIE;
        def.version = MNAV_ABI_VERSION;
        def.limits.nodes = 8192;
        def.limits.pathLength = 1000.0f;
        return def;
    }

    /// Makes a query context with the memory its limits need.
    ///
    /// @param def      The def, from mnavDefaultQueryDef.
    /// @param queryOut Receives the context, or NULL on failure.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument or a def
    /// not from mnavDefaultQueryDef; `mnav_errorVersion` for one built against
    /// another major or minor version; `mnav_errorRange` for a limit out of its
    /// range; `mnav_errorCapacity` when the allocator fails.
    /// @par Thread safety
    /// Safe from any thread.
    MNAV_NODISCARD MNAV_API mnavResult mnavCreateQuery(const mnavQueryDef* def,
                                                       mnavQuery** queryOut);

    /// Destroys a query context. NULL is ignored.
    ///
    /// @param query    The context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MNAV_API void mnavDestroyQuery(mnavQuery* query);

    /// Searches for the shortest way from a point on one polygon to a point
    /// on another (mnav-0005): A* over the edges between polygons, its
    /// heuristic the straight distance to the end point; ties go to the
    /// node made first. A step costs its length times the cost of the area
    /// it crosses, and the heuristic is scaled by the cheapest included
    /// area's cost; polygons of excluded areas other than the start
    /// polygon are not entered. Attached off-mesh links of included kinds are
    /// crossed at their declared cost, the heuristic scaled down to the
    /// lowest cost per meter among them (mnav-0005). The corridor found is pulled
    /// tight into a straight path with the funnel algorithm, stretch by
    /// stretch between links.
    ///
    /// @param query        The context; its memory holds the result.
    /// @param navmesh      The navmesh.
    /// @param filter       The areas usable and their costs, or NULL for
    ///                     every walkable area at a cost of 1.
    /// @param startPolygon The polygon the start point lies on, as
    ///                     mnavFindNearest gives it.
    /// @param start        The start point.
    /// @param endPolygon   The polygon the end point lies on.
    /// @param end          The end point.
    /// @param pathOut      Receives the result.
    /// @return `mnav_success` whenever a search ran, however it ended;
    /// `mnav_errorInvalid` for a NULL argument, a point that is not finite
    /// or a polygon id that never existed; `mnav_errorInvalid` for a filter not built from
    /// mnavDefaultQueryFilter; `mnav_errorRange` for a filter cost out of its
    /// range; `mnav_errorStale` for a polygon id whose tile
    /// has been replaced or removed.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    /// Any number of contexts may search one navmesh at once between
    /// commits.
    MNAV_NODISCARD MNAV_API mnavResult mnavFindPath(mnavQuery* query, const mnavNavmesh* navmesh,
                                                    const mnavQueryFilter* filter,
                                                    mnavPolygonId startPolygon, mnavPos3 start,
                                                    mnavPolygonId endPolygon, mnavPos3 end,
                                                    mnavPath* pathOut);

    /// Searches for the shortest way on the ground from a point on one
    /// polygon to a point on another (mnav-0005): Polyanya (Cui, Harabor
    /// and Grastien, 2017), whose nodes are intervals of portals seen from a
    /// root, the start, a corner turned at or an off-mesh link's landing
    /// point. Every included area must have the same cost: a step costs its
    /// length times that cost, so the way found is the shortest the navmesh
    /// allows, measured in x and z, not a way through portal midpoints.
    /// Polygons of excluded areas other than the start polygon are not
    /// entered. Attached off-mesh links of included kinds are crossed at
    /// their declared cost, from the takeoff point to the landing point.
    /// Ties go to the node made first. When the nodes run out, the
    /// A* search tells whether the end can be reached at all, and an end it
    /// cannot reach ends the search as no path. The result is that of mnavFindPath:
    /// the polygons crossed, the turning points, which are the path, and
    /// the links crossed; cost and length are those of the points, in three
    /// dimensions. A sliced path search in the same context ends.
    ///
    /// @param query        The context; its memory holds the result.
    /// @param navmesh      The navmesh.
    /// @param filter       The areas usable and their costs, or NULL for
    ///                     every walkable area at a cost of 1.
    /// @param startPolygon The polygon the start point lies on, as
    ///                     mnavFindNearest gives it.
    /// @param start        The start point.
    /// @param endPolygon   The polygon the end point lies on.
    /// @param end          The end point.
    /// @param pathOut      Receives the result.
    /// @return `mnav_success` whenever a search ran, however it ended;
    /// `mnav_errorInvalid` for a NULL argument, a point that is not finite,
    /// a polygon id that never existed, a filter not built from
    /// mnavDefaultQueryFilter or one whose included areas differ in cost;
    /// `mnav_errorRange` for a filter cost out of its range;
    /// `mnav_errorStale` for a polygon id whose tile has been replaced or
    /// removed.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    /// Any number of contexts may search one navmesh at once between
    /// commits.
    MNAV_NODISCARD MNAV_API mnavResult
    mnavFindShortestPath(mnavQuery* query, const mnavNavmesh* navmesh,
                         const mnavQueryFilter* filter, mnavPolygonId startPolygon, mnavPos3 start,
                         mnavPolygonId endPolygon, mnavPos3 end, mnavPath* pathOut);

    /// Begins a path search to run in slices (mnav-0005): the same search
    /// as mnavFindPath, with its own copy of the filter, continued by
    /// mnavContinuePath and ended by mnavFinishPath. Whatever the slices,
    /// the result is the one mnavFindPath gives.
    ///
    /// @param query        The context; it holds the search until the next
    ///                     search of any kind, or a move along the
    ///                     surface, begins.
    /// @param navmesh      The navmesh.
    /// @param filter       The areas usable and their costs, or NULL for
    ///                     every walkable area at a cost of 1; copied.
    /// @param startPolygon The polygon the start point lies on.
    /// @param start        The start point.
    /// @param endPolygon   The polygon the end point lies on.
    /// @param end          The end point.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, a
    /// point that is not finite, a polygon id that never existed or a
    /// filter not built from mnavDefaultQueryFilter; `mnav_errorRange` for a
    /// filter cost out of its range; `mnav_errorStale` for a polygon id
    /// whose tile has been replaced or removed.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavBeginPath(mnavQuery* query, const mnavNavmesh* navmesh,
                                                     const mnavQueryFilter* filter,
                                                     mnavPolygonId startPolygon, mnavPos3 start,
                                                     mnavPolygonId endPolygon, mnavPos3 end);

    /// Continues a search begun by mnavBeginPath: closes up to a number of
    /// nodes, fewer when the search ends first.
    ///
    /// @param query    The context.
    /// @param navmesh  The navmesh the search began on.
    /// @param nodes    The most nodes to close, at least 1.
    /// @param endedOut Receives whether the search has ended.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, no
    /// search begun, or fewer than 1 node; `mnav_errorStale` for another
    /// navmesh, or one committed to since the search began: begin again.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavContinuePath(mnavQuery* query,
                                                        const mnavNavmesh* navmesh, int32_t nodes,
                                                        bool* endedOut);

    /// Ends a search begun by mnavBeginPath and writes its result: as
    /// mnavFindPath's when it has ended; otherwise toward the node nearest
    /// the end, its end mnav_pathUnfinished.
    ///
    /// @param query    The context; its memory holds the result.
    /// @param navmesh  The navmesh the search began on.
    /// @param pathOut  Receives the result.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument or no
    /// search begun; `mnav_errorStale` for another navmesh, or one
    /// committed to since the search began.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavFinishPath(mnavQuery* query, const mnavNavmesh* navmesh,
                                                      mnavPath* pathOut);

    // How a move along the surface ended.
    typedef uint8_t mnavMoveEnd;

    enum
    {
        // The wanted point was reached.
        mnav_moveReached = 0,
        // Walls stopped the move; the point is the nearest on them.
        mnav_moveWall = 1,
        // As at a wall, the nearest point lying on a tile side with no
        // tile loaded beyond.
        mnav_moveNotLoaded = 2,
        // The polygons visited reached the context's node limit; the point
        // is the nearest on the walls met so far.
        mnav_moveOutOfNodes = 3,
    };

    // A move along the surface's result.
    typedef struct mnavMove
    {
        mnavMoveEnd end;
        // The point reached, on the detail surface, and its polygon.
        mnavPos3 point;
        mnavPolygonId polygon;
        // The polygons from the start polygon to the one reached, in the
        // context's memory until its next query.
        const mnavPolygonId* polygons;
        int32_t polygonCount;
    } mnavMove;

    /// Moves from a point on a polygon toward a wanted point along the
    /// navmesh, as far as the walls allow (mnav-0005): a breadth-first
    /// walk over the polygons the filter includes whose shared edges meet
    /// the circle round the move, in edge order. In the polygon holding the
    /// wanted point the move reaches it; otherwise it ends at the point
    /// nearest it on the walls met, ties to the wall met first.
    ///
    /// @param query        The context; its memory holds the polygons.
    /// @param navmesh      The navmesh.
    /// @param filter       The areas usable, or NULL for every walkable one.
    /// @param startPolygon The polygon the start point lies on.
    /// @param start        The start point.
    /// @param end          The wanted point; only its ground position
    ///                     counts.
    /// @param moveOut      Receives the result.
    /// @return `mnav_success` whenever the move ran, however it ended;
    /// `mnav_errorInvalid` for a NULL argument, a point that is not finite,
    /// a polygon id that never existed or a filter not built from
    /// mnavDefaultQueryFilter; `mnav_errorRange` for a filter cost out of
    /// its range; `mnav_errorStale` for a polygon id whose tile has been
    /// replaced or removed.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    /// Any number of contexts may move on one navmesh at once between
    /// commits.
    MNAV_NODISCARD MNAV_API mnavResult mnavMoveAlongSurface(
        mnavQuery* query, const mnavNavmesh* navmesh, const mnavQueryFilter* filter,
        mnavPolygonId startPolygon, mnavPos3 start, mnavPos3 end, mnavMove* moveOut);

    // A path corridor (mnav-0005): the polygons from the agent's to its
    // target's, in a buffer the caller owns, with the agent's position on
    // the first and the target on the last.
    typedef struct mnavCorridor
    {
        mnavPos3 position;
        mnavPos3 target;
        mnavPolygonId* polygons;
        int32_t count;
        int32_t capacity;
    } mnavCorridor;

    // A corridor's straight path: from the position, its corners, to the
    // target, with the off-mesh links crossed, in a query context's memory
    // until its next query.
    typedef struct mnavCorners
    {
        const mnavPos3* points;
        int32_t pointCount;
        const mnavPathLink* links;
        int32_t linkCount;
    } mnavCorners;

    // A run of a straight path over one area (mnav-0005): from its start
    // on, the path crosses polygons of that area until the next run's
    // start.
    typedef struct mnavAreaRun
    {
        mnavPos3 start;
        // The straight path's point the run starts at or after: start lies
        // between it and the next point.
        int32_t corner;
        mnavAreaType area;
    } mnavAreaRun;

    /// Starts a corridor over a buffer: one polygon, the position and the
    /// target both at a point on it.
    ///
    /// @param corridor The corridor.
    /// @param buffer   Room for capacity polygons, kept by the corridor.
    /// @param capacity The buffer's polygons, at least 1.
    /// @param polygon  The polygon the point lies on.
    /// @param position The point.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, a
    /// capacity below 1 or a point that is not finite.
    /// @par Thread safety
    /// Safe from any thread; the corridor is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavResetCorridor(mnavCorridor* corridor,
                                                         mnavPolygonId* buffer, int32_t capacity,
                                                         mnavPolygonId polygon, mnavPos3 position);

    /// Loads a path into a corridor: its polygons, its first point as the
    /// position and its last as the target.
    ///
    /// @param corridor The corridor.
    /// @param path     A path from mnavFindPath or mnavFinishPath.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument or a
    /// path with no polygon or point; `mnav_errorCapacity` when the path
    /// has more polygons than the corridor's buffer, which is unchanged.
    /// @par Thread safety
    /// Safe from any thread; the corridor is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavSetCorridor(mnavCorridor* corridor,
                                                       const mnavPath* path);

    /// Counts a corridor's leading polygons that are still current, of
    /// areas the filter includes, and joined to the one before by an
    /// edge, a tile link or an off-mesh link of a kind it includes. Fewer
    /// than all means the corridor needs trimming there and replanning.
    ///
    /// @param navmesh  The navmesh.
    /// @param filter   The areas and kinds usable, or NULL for all.
    /// @param corridor The corridor.
    /// @param validOut Receives the count.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument or a
    /// filter not built from mnavDefaultQueryFilter; `mnav_errorRange` for
    /// a filter cost out of its range.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may check corridors at
    /// once between commits.
    MNAV_NODISCARD MNAV_API mnavResult mnavCheckCorridor(const mnavNavmesh* navmesh,
                                                         const mnavQueryFilter* filter,
                                                         const mnavCorridor* corridor,
                                                         int32_t* validOut);

    /// Finds a corridor's straight path from its position to its target:
    /// the funnel over the portals between its polygons.
    ///
    /// @param query    The context; its memory holds the corners.
    /// @param navmesh  The navmesh.
    /// @param corridor The corridor.
    /// @param cornersOut Receives the straight path.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument or
    /// polygons not joined; `mnav_errorStale` for a polygon whose tile has
    /// been replaced or removed; `mnav_errorLimit` for a corridor of more
    /// polygons than the context's node limit.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavCorridorCorners(mnavQuery* query,
                                                           const mnavNavmesh* navmesh,
                                                           const mnavCorridor* corridor,
                                                           mnavCorners* cornersOut);

    /// Finds the areas along a corridor's straight path: the corners
    /// mnavCorridorCorners gives, which replace the context's last ones,
    /// and a run where the path starts, one wherever it crosses into a
    /// polygon of another area, and one where it lands from each off-mesh
    /// link, in order. Hosts change gait or sound at a run's start; a
    /// path's areas are its corridor's, once loaded with mnavSetCorridor.
    ///
    /// @param query      The context; its memory holds the corners.
    /// @param navmesh    The navmesh.
    /// @param corridor   The corridor.
    /// @param cornersOut Receives the straight path, or NULL.
    /// @param runsOut    Receives the runs, in order. May be NULL when
    ///                   capacity is 0.
    /// @param capacity   How many runs fit, at least 0.
    /// @param countOut   Receives how many runs there are, also when they
    ///                   do not all fit.
    /// @return As mnavCorridorCorners; also `mnav_errorInvalid` for a NULL
    /// countOut, a negative capacity or NULL runs with a positive one, and
    /// `mnav_errorCapacity` when the runs do not fit, the first capacity
    /// of them written.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavCorridorAreas(
        mnavQuery* query, const mnavNavmesh* navmesh, const mnavCorridor* corridor,
        mnavCorners* cornersOut, mnavAreaRun* runsOut, int32_t capacity, int32_t* countOut);

    // The fastest mnavSteer may steer, and the longest distance it slows
    // or stops within, in meters per second and meters.
#define MNAV_MAX_STEER_SPEED    1000.0f
#define MNAV_MAX_STEER_DISTANCE 1000.0f

    // How mnavSteer steers (mnav-0005). Build it with mnavDefaultSteerDef.
    typedef struct mnavSteerDef
    {
        uint32_t cookie;
        // MNAV_ABI_VERSION, set by mnavDefaultSteerDef.
        uint32_t version;
        // The speed on the way, 0 to MNAV_MAX_STEER_SPEED.
        float maxSpeed;
        // Within this distance of the target along the corners, the speed
        // falls with the distance left; 0 to MNAV_MAX_STEER_DISTANCE.
        float slowDistance;
        // Within this distance of the target the agent has arrived, and
        // of an off-mesh link's takeoff it is at the link; 0 to
        // MNAV_MAX_STEER_DISTANCE.
        float arriveDistance;
        // Whether to swing wide of the next corner so as to meet the turn
        // already heading out of it, as Detour's crowd does.
        bool anticipateTurns;
    } mnavSteerDef;

    // What mnavSteer found.
    typedef uint8_t mnavSteerState;

    enum
    {
        mnav_steerMoving = 0,
        // Within the arrival distance of the target; the velocity is 0.
        mnav_steerArrived = 1,
        // Within the arrival distance of the next off-mesh link's takeoff;
        // the velocity is 0, and the host crosses the link.
        mnav_steerAtLink = 2,
    };

    // A velocity mnavSteer gives.
    typedef struct mnavSteering
    {
        // On the ground (y is 0), in meters per second: the agent's
        // preferred velocity for avoidance, x and z.
        mnavPos3 velocity;
        mnavSteerState state;
        // At a link: its index among the corners' links; otherwise -1.
        int32_t link;
    } mnavSteering;

// The cookie mnavDefaultSteerDef sets and mnavSteer checks.
#define MNAV_STEER_DEF_COOKIE 0x5354564Du

    /// Returns a steering def of 3.5 m/s, slowing within 1 m of the
    /// target, arriving within 0.1 m, anticipating turns. Built in the
    /// program, so that it carries the version of the headers the program
    /// includes.
    ///
    /// @return The def.
    /// @par Thread safety
    /// Safe from any thread.
    static inline mnavSteerDef mnavDefaultSteerDef(void)
    {
#ifdef __cplusplus
        mnavSteerDef def = {};
#else
    mnavSteerDef def = {0};
#endif
        def.cookie = MNAV_STEER_DEF_COOKIE;
        def.version = MNAV_ABI_VERSION;
        def.maxSpeed = 3.5f;
        def.slowDistance = 1.0f;
        def.arriveDistance = 0.1f;
        def.anticipateTurns = true;
        return def;
    }

    /// Steers along a corridor's corners (mnav-0005): toward the next
    /// corner, or swinging wide of it, at the def's speed, slowing within
    /// its slowing distance of the target by the distance left along the
    /// corners over it; stopped within the arrival distance of the target
    /// or of the next link's takeoff. Keeps no state: call it each step
    /// with the corners mnavCorridorCorners gives, feed the velocity to
    /// avoidance, and limit acceleration and move the agent yourself.
    ///
    /// @param corners     The corners, the agent's position first.
    /// @param def         The def, from mnavDefaultSteerDef.
    /// @param steeringOut Receives the velocity and state.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, corners
    /// with no points or a def not from mnavDefaultSteerDef;
    /// `mnav_errorVersion` for one built against another major or minor
    /// version; `mnav_errorRange` for a def value out of its range or a corner
    /// not finite.
    /// @par Thread safety
    /// Safe from any thread.
    MNAV_NODISCARD MNAV_API mnavResult mnavSteer(const mnavCorners* corners,
                                                 const mnavSteerDef* def,
                                                 mnavSteering* steeringOut);

    /// Moves a corridor's position along the surface toward a wanted point
    /// (mnavMoveAlongSurface from its first polygon) and merges the
    /// polygons walked into its start: it keeps its polygons from the
    /// farthest one the walk passed, led to by the walk.
    ///
    /// @param query    The context, for the walk.
    /// @param navmesh  The navmesh.
    /// @param filter   The areas usable, or NULL for every walkable one.
    /// @param corridor The corridor.
    /// @param wanted   Where the agent would be; only its ground position
    ///                 counts.
    /// @param moveOut  Receives the walk, or NULL.
    /// @return As mnavMoveAlongSurface; also `mnav_errorInvalid` for a
    /// corridor with no buffer, and `mnav_errorCapacity` when the merged
    /// corridor would outgrow its buffer, which leaves it unchanged.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    /// So is the corridor.
    MNAV_NODISCARD MNAV_API mnavResult mnavMoveCorridor(mnavQuery* query,
                                                        const mnavNavmesh* navmesh,
                                                        const mnavQueryFilter* filter,
                                                        mnavCorridor* corridor, mnavPos3 wanted,
                                                        mnavMove* moveOut);

    /// Moves a corridor's target along the surface toward a wanted point
    /// (mnavMoveAlongSurface from its last polygon) and merges the polygons
    /// walked into its end: it keeps its polygons up to the first one the
    /// walk passed, then the rest of the walk.
    ///
    /// @param query    The context, for the walk.
    /// @param navmesh  The navmesh.
    /// @param filter   The areas usable, or NULL for every walkable one.
    /// @param corridor The corridor.
    /// @param wanted   Where the target would be; only its ground position
    ///                 counts.
    /// @param moveOut  Receives the walk, or NULL.
    /// @return As mnavMoveCorridor.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    /// So is the corridor.
    MNAV_NODISCARD MNAV_API mnavResult mnavMoveCorridorTarget(mnavQuery* query,
                                                              const mnavNavmesh* navmesh,
                                                              const mnavQueryFilter* filter,
                                                              mnavCorridor* corridor,
                                                              mnavPos3 wanted, mnavMove* moveOut);

    /// Shortens a corridor where the agent can see ahead: casts a ray from
    /// its position toward a point, usually a corner a few ahead; when the
    /// ray reaches it, the polygons it crossed replace the corridor's
    /// start up to the last corridor polygon it passed, if that is fewer.
    ///
    /// @param query        The context, for the ray.
    /// @param navmesh      The navmesh.
    /// @param filter       The areas usable, or NULL for every walkable one.
    /// @param corridor     The corridor.
    /// @param toward       The point to look toward.
    /// @param shortenedOut Receives whether the corridor changed.
    /// @return As mnavRaycast; also `mnav_errorInvalid` for a corridor
    /// with no buffer or a NULL shortenedOut.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    /// So is the corridor.
    MNAV_NODISCARD MNAV_API mnavResult mnavShortcutCorridor(mnavQuery* query,
                                                            const mnavNavmesh* navmesh,
                                                            const mnavQueryFilter* filter,
                                                            mnavCorridor* corridor, mnavPos3 toward,
                                                            bool* shortenedOut);

    /// Plans a corridor again from its position to its target and loads
    /// the path found: from its first and last polygons while they are
    /// current, else from the polygons nearest the position and the target
    /// within a box round each. A corridor whose position or target finds
    /// no polygon is left as it was, the path's end saying why.
    ///
    /// @param query       The context, for the search; it holds the path.
    /// @param navmesh     The navmesh.
    /// @param filter      The areas usable and their costs, or NULL.
    /// @param corridor    The corridor.
    /// @param halfExtents The box's half sizes for finding polygons again.
    /// @param pathOut     Receives the path, or an empty one ended
    ///                    mnav_pathNotLoaded or mnav_pathNone when an end
    ///                    found no polygon.
    /// @return `mnav_success` whenever the corridor was looked at;
    /// `mnav_errorInvalid` for a NULL argument or a corridor with no
    /// buffer; `mnav_errorCapacity` when the path has more polygons than
    /// the buffer, which leaves the corridor as it was; the errors of
    /// mnavFindNearest and mnavFindPath otherwise.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    /// So is the corridor.
    MNAV_NODISCARD MNAV_API mnavResult mnavReplanCorridor(mnavQuery* query,
                                                          const mnavNavmesh* navmesh,
                                                          const mnavQueryFilter* filter,
                                                          mnavCorridor* corridor,
                                                          mnavVec3 halfExtents, mnavPath* pathOut);

    // The longest side a grid may have, in cells.
#define MNAV_MAX_GRID_SIDE 32768

    // A grid for grid pathfinding (mnav-0005): cells in rows from (0, 0),
    // each an area; mnav_areaNone, and areas a filter leaves out, block a
    // cell.
    typedef struct mnavGrid
    {
        // width times height areas, x fastest. Only read during a call.
        const mnavAreaType* areas;
        // The cells across and down, 1 to MNAV_MAX_GRID_SIDE.
        int32_t width;
        int32_t height;
        // A cell's side, in meters, more than 0.
        float cellSize;
    } mnavGrid;

    // A cell of a grid.
    typedef struct mnavCell
    {
        int32_t x;
        int32_t y;
    } mnavCell;

    // A grid path: how its search ended, its cost and length, and the
    // cells it turns at, the start first and the last cell reached last,
    // in a query context's memory until its next query.
    typedef struct mnavGridPath
    {
        mnavPathEnd end;
        double cost;
        double length;
        const mnavCell* cells;
        int32_t cellCount;
    } mnavGridPath;

    /// Finds the cheapest path between two cells of a grid, moving to the
    /// 8 neighbours and never across a blocked corner: a diagonal step
    /// needs both cells beside it open. A step costs its length times the
    /// mean of its two cells' area costs. When every area the filter
    /// includes costs the same, the search is jump point search, else A*;
    /// both keep the context's node limit and path length, and end as
    /// mnavFindPath does, with the path to the cell nearest the end when
    /// the end is not reached. A grid search ends any sliced search.
    ///
    /// @param query   The context; its memory holds the path.
    /// @param grid    The grid.
    /// @param filter  The areas usable and their costs, or NULL.
    /// @param start   The start cell.
    /// @param end     The end cell.
    /// @param pathOut Receives the path; mnav_pathNone with the start
    ///                alone when the start is blocked.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, a
    /// grid with no areas, a side out of range or a cell size not more than
    /// 0 or not finite, a cell outside the grid, or a filter not built from
    /// mnavDefaultQueryFilter; `mnav_errorRange` for a filter cost out of
    /// its range.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavFindGridPath(mnavQuery* query, const mnavGrid* grid,
                                                        const mnavQueryFilter* filter,
                                                        mnavCell start, mnavCell end,
                                                        mnavGridPath* pathOut);

    /// Reads the height of a polygon's detail surface at a point on the
    /// ground.
    ///
    /// @param navmesh   The navmesh.
    /// @param polygon   The polygon.
    /// @param x         The point's X.
    /// @param z         The point's Z.
    /// @param heightOut Receives the height.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, a
    /// coordinate not finite, or a polygon id never handed out;
    /// `mnav_errorStale` for a polygon whose tile has gone;
    /// `mnav_errorRange` for a point outside the polygon on the ground.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read the navmesh
    /// at once while no commit runs on it.
    MNAV_NODISCARD MNAV_API mnavResult mnavGetHeight(const mnavNavmesh* navmesh,
                                                     mnavPolygonId polygon, double x, double z,
                                                     double* heightOut);

    // The nearest wall a search within a radius found.
    typedef struct mnavWall
    {
        // Whether a wall lies within the radius; when not, the distance is
        // the radius and the point the center.
        bool found;
        // Whether the context's node limit cut the search short, so that a
        // nearer wall may lie unseen.
        bool limited;
        // The distance on the ground from the center to the wall point.
        double distance;
        // The wall's nearest point, and the unit direction on the ground
        // from it to the center.
        mnavPos3 point;
        mnavPos3 normal;
    } mnavWall;

    /// Finds the nearest wall to a point within a radius on the ground,
    /// searching from its polygon across the edges that come within the
    /// radius, off-mesh links aside. An edge is a wall where no polygon the
    /// filter includes lies across it: a tile side with no tile loaded
    /// beyond, and the parts of a side no polygon of the tile beyond
    /// covers, are walls. A wall exactly the radius away lies within it; of
    /// walls at the same distance it gives one, the same on every
    /// platform, but which one is not promised.
    ///
    /// @param query   The context; its last search ends.
    /// @param navmesh The navmesh.
    /// @param filter  The areas usable, or NULL.
    /// @param polygon The polygon the center lies in.
    /// @param center  The point.
    /// @param radius  How far to look, in meters, at least 0 and finite.
    /// @param wallOut Receives the wall.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, a
    /// point or radius not finite, a negative radius, a polygon id never
    /// handed out, or a filter not built from mnavDefaultQueryFilter;
    /// `mnav_errorStale` for a polygon whose tile has gone;
    /// `mnav_errorRange` for a filter cost out of its range.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavFindWallDistance(mnavQuery* query,
                                                            const mnavNavmesh* navmesh,
                                                            const mnavQueryFilter* filter,
                                                            mnavPolygonId polygon, mnavPos3 center,
                                                            double radius, mnavWall* wallOut);

    // A wall within a radius: an edge no polygon the filter includes lies
    // across.
    typedef struct mnavWallSegment
    {
        // Its ends, in the order of its polygon's corners.
        mnavPos3 start;
        mnavPos3 end;
        // The unit direction on the ground from it toward its polygon, the
        // walkable side; zero for an edge of no length on the ground.
        mnavPos3 normal;
        // The distance on the ground from the center to its nearest point.
        double distance;
    } mnavWallSegment;

    // What a search for the walls within a radius found.
    typedef struct mnavWallsFound
    {
        // The walls within the radius, also beyond the buffer's capacity.
        int32_t count;
        // Whether the context's node limit cut the search short, so that
        // more walls may lie unseen.
        bool limited;
    } mnavWallsFound;

    /// Lists the walls within a radius of a point on the ground, nearest
    /// first, for avoidance's segment obstacles: the search and the walls
    /// of mnavFindWallDistance, whose wall is the first listed. Walls at
    /// the same distance keep the order the search meets them in, the same
    /// on every platform.
    ///
    /// @param query    The context; its last search ends.
    /// @param navmesh  The navmesh.
    /// @param filter   The areas usable, or NULL.
    /// @param polygon  The polygon the center lies in.
    /// @param center   The point.
    /// @param radius   How far to look, in meters, at least 0 and finite.
    /// @param wallsOut Receives up to capacity walls, the nearest; NULL when
    ///                 capacity is 0.
    /// @param capacity The room, at least 0.
    /// @param foundOut Receives the count and whether the node limit cut
    ///                 the search short.
    /// @return `mnav_success`; `mnav_errorCapacity` when more walls lie
    /// within the radius than the buffer holds: it holds the nearest and
    /// foundOut counts all; `mnav_errorInvalid` for a NULL argument, a
    /// negative capacity, a point or radius not finite, a negative radius,
    /// a polygon id never handed out, or a filter not built from
    /// mnavDefaultQueryFilter; `mnav_errorStale` for a polygon whose tile
    /// has gone; `mnav_errorRange` for a filter cost out of its range.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavFindWalls(mnavQuery* query, const mnavNavmesh* navmesh,
                                                     const mnavQueryFilter* filter,
                                                     mnavPolygonId polygon, mnavPos3 center,
                                                     double radius, mnavWallSegment* wallsOut,
                                                     int32_t capacity, mnavWallsFound* foundOut);

    // A random point and the polygon it lies on; slot 0 when there was no
    // polygon to pick.
    typedef struct mnavRandomPoint
    {
        mnavPolygonId polygon;
        mnavPos3 point;
    } mnavRandomPoint;

    /// Picks a point uniformly over the ground area of every polygon the
    /// filter includes, from a seed: the same seed and navmesh give the
    /// same point on every platform. The point lies on the polygon's detail
    /// surface. The call reads every polygon twice.
    ///
    /// @param navmesh  The navmesh.
    /// @param filter   The areas usable, or NULL.
    /// @param seed     Any value.
    /// @param pointOut Receives the point.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument or a
    /// filter not built from mnavDefaultQueryFilter; `mnav_errorRange` for
    /// a filter cost out of its range.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read the navmesh
    /// at once while no commit runs on it.
    MNAV_NODISCARD MNAV_API mnavResult mnavFindRandomPoint(const mnavNavmesh* navmesh,
                                                           const mnavQueryFilter* filter,
                                                           uint64_t seed,
                                                           mnavRandomPoint* pointOut);

    /// Picks a point reachable from a center: a polygon the walls search
    /// reaches within the radius, by ground area, then a point uniformly
    /// on it, which may lie beyond the radius by up to the polygon's size.
    ///
    /// @param query    The context; its last search ends.
    /// @param navmesh  The navmesh.
    /// @param filter   The areas usable, or NULL.
    /// @param polygon  The polygon the center lies in.
    /// @param center   The center.
    /// @param radius   How far to search, in meters, at least 0 and finite.
    /// @param seed     Any value.
    /// @param pointOut Receives the point.
    /// @return As mnavFindWallDistance.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult
    mnavFindRandomPointAround(mnavQuery* query, const mnavNavmesh* navmesh,
                              const mnavQueryFilter* filter, mnavPolygonId polygon, mnavPos3 center,
                              double radius, uint64_t seed, mnavRandomPoint* pointOut);

    /// Tells whether the end can be reached from the start: the path search
    /// with its limits, without a path made. mnav_pathFound when it can,
    /// mnav_pathNone when it cannot, or the limit that stopped the search
    /// before it could tell.
    ///
    /// @param query        The context; its last search ends.
    /// @param navmesh      The navmesh.
    /// @param filter       The areas usable and their costs, or NULL.
    /// @param startPolygon The polygon the start lies in.
    /// @param start        The start point.
    /// @param endPolygon   The polygon the end lies in.
    /// @param end          The end point.
    /// @param endOut       Receives how the search ended.
    /// @return As mnavFindPath.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavCheckReachable(mnavQuery* query,
                                                          const mnavNavmesh* navmesh,
                                                          const mnavQueryFilter* filter,
                                                          mnavPolygonId startPolygon,
                                                          mnavPos3 start, mnavPolygonId endPolygon,
                                                          mnavPos3 end, mnavPathEnd* endOut);

    // How a raycast ended.
    typedef uint8_t mnavRayEnd;

    enum
    {
        // The ray reached its end point.
        mnav_rayReached = 0,
        // A wall stopped it.
        mnav_rayWall = 1,
        // It came to a tile side with no tile loaded beyond.
        mnav_rayNotLoaded = 2,
        // The polygons it crossed reached the context's node limit.
        mnav_rayOutOfNodes = 3,
    };

    // A raycast's result.
    typedef struct mnavRay
    {
        mnavRayEnd end;
        // How far the ray went, as a fraction of the way from its start to
        // its end point on the ground: 1 when it reached the end point.
        double t;
        // At a wall, the wall's normal on the ground, pointing back to
        // where the ray came from; 0 otherwise.
        double normalX;
        double normalZ;
        // The polygons crossed, from the start polygon to the one where the
        // ray ended, in the context's memory until its next query.
        const mnavPolygonId* polygons;
        int32_t polygonCount;
    } mnavRay;

    /// Casts a ray along the navmesh on the ground from a point on a polygon
    /// toward an end point (mnav-0005): polygon to polygon through the
    /// first edge it crosses, until it reaches the end point, meets a wall
    /// (an edge into a polygon the filter excludes is a wall too) or a tile
    /// side with no tile loaded, or crosses as many polygons as the
    /// context's node limit. Where it leaves through a corner, it goes
    /// on through an edge that leads on, the lowest-numbered first; at a
    /// corner where two walls meet, the normal is one of theirs, the same
    /// on every platform, but which one is not promised.
    ///
    /// @param query        The context; its memory holds the polygons.
    /// @param navmesh      The navmesh.
    /// @param filter       The areas usable, or NULL for every walkable one.
    /// @param startPolygon The polygon the start point lies on, as
    ///                     mnavFindNearest gives it.
    /// @param start        The start point.
    /// @param end          The end point; only its ground position counts.
    /// @param rayOut       Receives the result.
    /// @return `mnav_success` whenever the ray was cast, however it ended;
    /// `mnav_errorInvalid` for a NULL argument, a point that is not finite
    /// or a polygon id that never existed; `mnav_errorInvalid` for a filter not built from
    /// mnavDefaultQueryFilter; `mnav_errorRange` for a filter cost out of its
    /// range; `mnav_errorStale` for a polygon id whose tile
    /// has been replaced or removed.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    /// Any number of contexts may cast on one navmesh at once between
    /// commits.
    MNAV_NODISCARD MNAV_API mnavResult mnavRaycast(mnavQuery* query, const mnavNavmesh* navmesh,
                                                   const mnavQueryFilter* filter,
                                                   mnavPolygonId startPolygon, mnavPos3 start,
                                                   mnavPos3 end, mnavRay* rayOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_NAV_QUERY_H
