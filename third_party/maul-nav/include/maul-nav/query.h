// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Queries over a navmesh's committed tiles. Every query reads
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
    // type. Build it with mnavDefaultQueryFilter.
    typedef struct mnavQueryFilter
    {
        uint32_t cookie;
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
#define MNAV_QUERY_FILTER_COOKIE 0x5246564Du

    /// Returns the filter that uses every walkable area at a cost of 1 and
    /// every kind of off-mesh link. It carries only a cookie: every call
    /// that reads a filter also takes an object whose creation checked the
    /// version.
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
        int32_t polygonCount;
        // Whether part of the box lies on places with no tile loaded.
        bool incomplete;
    } mnavFound;

    /// Lists the polygons the filter includes whose bounds meet a box:
    /// their vertices on the ground and their detail's heights. They come
    /// by tile, in place order (x, then z), then by polygon index.
    ///
    /// @param navmesh     The navmesh.
    /// @param center      The box's center.
    /// @param halfExtents The box's half sizes, in meters, at least 0.
    /// @param filter      The areas wanted, or NULL for every walkable one;
    ///                    one including mnav_areaNone finds blocked polygons.
    /// @param polygonsOut Receives up to capacity ids; NULL when capacity
    ///                    is 0. Zeroed over the capacity on any failure but
    ///                    mnav_errorCapacity, which keeps the first capacity.
    /// @param capacity    The room, at least 0.
    /// @param foundOut    Receives the count and whether the box was all loaded.
    ///                    Zeroed on any failure but mnav_errorCapacity, which gives
    ///                    the total.
    /// @return `mnav_success`; `mnav_errorCapacity` when more polygons meet the
    /// box than the buffer holds: it holds the first ones and foundOut counts
    /// all; `mnav_errorInvalid` for a NULL argument, a negative capacity, a
    /// center or extent that is not finite or a negative extent, or a filter
    /// not built from mnavDefaultQueryFilter or with a NaN cost;
    /// `mnav_errorLimit` for a center more than MNAV_MAX_QUERY_DISTANCE from
    /// the navmesh's origin on an axis, or a filter cost past
    /// MNAV_MIN_AREA_COST or MNAV_MAX_AREA_COST, an infinity included.
    /// @par Thread safety
    /// Safe from any thread. Any number of queries may run at once between
    /// commits.
    MNAV_NODISCARD MNAV_API mnavResult
    mnavNavmesh_FindPolygons(const mnavNavmesh* navmesh, mnavPos3 center, mnavVec3 halfExtents,
                             const mnavQueryFilter* filter, mnavPolygonId* polygonsOut,
                             int32_t capacity, mnavFound* foundOut);

    /// Finds the polygon, among those the filter includes, nearest a point
    /// whose nearest point lies within a box round it, and that point. A
    /// point over a polygon scores the height it lies beyond the agent's
    /// step, any other the distance to the polygon; ties go to the shorter
    /// distance, then the tile first by place (x, then z), then the lower
    /// polygon index.
    ///
    /// @param navmesh      The navmesh.
    /// @param point        The query point.
    /// @param halfExtents  The box's half sizes, in meters, at least 0.
    /// @param filter       The areas usable, or NULL for every walkable one.
    /// @param nearestOut   Receives the result. Zeroed on any failure.
    /// @return `mnav_success`, also when no polygon's nearest point lies in the
    /// box (the polygon's slot is then 0); `mnav_errorInvalid` for a NULL
    /// argument, a point or extent that is not finite, a negative extent, or a
    /// filter not built from mnavDefaultQueryFilter or with a NaN cost;
    /// `mnav_errorLimit` for a point more than MNAV_MAX_QUERY_DISTANCE from the
    /// navmesh's origin on an axis, or a filter cost past MNAV_MIN_AREA_COST or
    /// MNAV_MAX_AREA_COST, an infinity included.
    /// @par Thread safety
    /// Safe from any thread. Any number of queries may run at once between
    /// commits.
    MNAV_NODISCARD MNAV_API mnavResult mnavNavmesh_FindNearest(const mnavNavmesh* navmesh,
                                                               mnavPos3 point, mnavVec3 halfExtents,
                                                               const mnavQueryFilter* filter,
                                                               mnavNearest* nearestOut);

// The most corners a polygon clipped to a circle holds: a polygon's six
// and one more for each of the 32 sides of the 32-gon standing for the
// circle; rounding can add a second at a side, which is refused.
#define MNAV_MAX_CLIP_CORNERS 38

// The largest nodes one search may use.
#define MNAV_MAX_QUERY_NODES 1048576
// The longest path length limit, in meters.
#define MNAV_MAX_PATH_LENGTH 1.0e7f
// How far a query's points, ends, centers and radii may reach from the
// navmesh's origin (its bake def's origin), in meters, on each axis; one
// past it is mnav_errorLimit. Within it, every difference between such a
// point and the navmesh's own, and its square, is a finite double, so a
// query's arithmetic never overflows; no world comes near it.
#define MNAV_MAX_QUERY_DISTANCE 1.0e100

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
        // The transitions of the largest hierarchy the context searches
        // (mnavQuery_FindHierarchicalPath), 0 to
        // MNAV_MAX_HIERARCHY_TRANSITIONS; 0 for a context that searches
        // none. The context holds the search over them, 28 bytes each.
        int32_t transitions;
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
    typedef int32_t mnavPathEnd;

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
        // The context whose memory holds the arrays, and which of its paths
        // this is, set by the search that gives it, so that mnavSetCorridor
        // can tell the arrays are still this path's.
        const mnavQuery* query;
        uint64_t generation;
    } mnavPath;

// The cookie mnavDefaultQueryDef sets and mnavCreateQuery checks.
#define MNAV_QUERY_DEF_COOKIE 0x4E415651u

    /// Returns a query def with 8,192 nodes per search, paths up to
    /// 1,000 m, room for a hierarchy of 16,384 transitions, as the default
    /// hierarchy def's, and the C library's allocator. Built in the program, so
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
        def.limits.transitions = 16384;
        return def;
    }

    /// Makes a query context with the memory its limits need.
    ///
    /// @param def      The def, from mnavDefaultQueryDef.
    /// @param queryOut Receives the context, or NULL on failure.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument or a def
    /// not from mnavDefaultQueryDef; `mnav_errorVersion` for one built against
    /// another version of the headers; `mnav_errorInvalid` also for an
    /// allocator with one function, fewer than 1 node, a path length not more
    /// than 0 or NaN, or fewer than 0 transitions; `mnav_errorLimit` for more
    /// nodes than MNAV_MAX_QUERY_NODES, a path length past
    /// MNAV_MAX_PATH_LENGTH, an infinity included, or more transitions than
    /// MNAV_MAX_HIERARCHY_TRANSITIONS; `mnav_errorCapacity` when the
    /// allocator fails.
    /// @par Thread safety
    /// Safe from any thread.
    MNAV_NODISCARD MNAV_API mnavResult mnavCreateQuery(const mnavQueryDef* def,
                                                       mnavQuery** queryOut);

    /// Destroys a query context. NULL is ignored. Called back in from host code
    /// a call using the context runs (a link generation's clearance test, a
    /// hierarchy's allocator as it builds), it does nothing and counts one
    /// misuse.
    ///
    /// @param query    The context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MNAV_API void mnavDestroyQuery(mnavQuery* query);

    /// Returns how many calls against the context were refused as misuse since
    /// it was made: an argument refused with `mnav_errorInvalid` when the
    /// context was the first object the call was given, and a call back in from
    /// host code it ran (`mnav_errorBusy`).
    ///
    /// @param query    The context.
    /// @return The count; 0 for NULL.
    /// @par Thread safety
    /// Safe from any thread. It may be read at any time, also while host
    /// code the object runs calls back in.
    MNAV_API uint64_t mnavQuery_GetMisuse(const mnavQuery* query);

    /// Searches for the shortest way from a point on one polygon to a point
    /// on another: A* over the edges between polygons, its
    /// heuristic the straight distance to the end point; ties go to the
    /// node made first. A step costs its length times the cost of the area
    /// it crosses, and the heuristic is scaled by the cheapest included
    /// area's cost; polygons of excluded areas other than the start
    /// polygon are not entered. Attached off-mesh links of included kinds are
    /// crossed at their declared cost, the heuristic scaled down to the
    /// lowest cost per meter among them. The corridor found is pulled
    /// tight into a straight path with the funnel algorithm, stretch by
    /// stretch between links.
    ///
    /// @param query        The context; its memory holds the result.
    /// @param navmesh      The navmesh.
    /// @param startPolygon The polygon the start point lies on, as
    ///                     mnavNavmesh_FindNearest gives it.
    /// @param start        The start point.
    /// @param endPolygon   The polygon the end point lies on.
    /// @param end          The end point.
    /// @param filter       The areas usable and their costs, or NULL for
    ///                     every walkable area at a cost of 1.
    /// @param pathOut      Receives the result. Zeroed on any failure but
    ///                     mnav_errorBusy, which leaves it as it was.
    /// @return `mnav_success` whenever a search ran, however it ended;
    /// `mnav_errorInvalid` for a NULL argument, a point that is not finite, a
    /// polygon id that never existed, or a filter not built from
    /// mnavDefaultQueryFilter or with a NaN cost; `mnav_errorLimit` for a point
    /// more than MNAV_MAX_QUERY_DISTANCE from the navmesh's origin on an axis,
    /// or a filter cost past MNAV_MIN_AREA_COST or MNAV_MAX_AREA_COST, an
    /// infinity included; `mnav_errorStale` for a polygon id whose tile has
    /// been replaced or removed. Also `mnav_errorBusy` when called back in from
    /// host code a call using the context runs (a link generation's clearance
    /// test, a hierarchy's allocator as it builds), one misuse counted.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    /// Any number of contexts may search one navmesh at once between
    /// commits.
    MNAV_NODISCARD MNAV_API mnavResult mnavQuery_FindPath(
        mnavQuery* query, const mnavNavmesh* navmesh, mnavPolygonId startPolygon, mnavPos3 start,
        mnavPolygonId endPolygon, mnavPos3 end, const mnavQueryFilter* filter, mnavPath* pathOut);

    /// Searches for the shortest way on the ground from a point on one
    /// polygon to a point on another: Polyanya (Cui, Harabor
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
    /// cannot reach ends the search as no path. The result is that of mnavQuery_FindPath:
    /// the polygons crossed, the turning points, which are the path, and
    /// the links crossed; cost and length are those of the points, in three
    /// dimensions. A sliced path search in the same context ends.
    ///
    /// @param query        The context; its memory holds the result.
    /// @param navmesh      The navmesh.
    /// @param startPolygon The polygon the start point lies on, as
    ///                     mnavNavmesh_FindNearest gives it.
    /// @param start        The start point.
    /// @param endPolygon   The polygon the end point lies on.
    /// @param end          The end point.
    /// @param filter       The areas usable and their costs, or NULL for
    ///                     every walkable area at a cost of 1.
    /// @param pathOut      Receives the result. Zeroed on any failure but
    ///                     mnav_errorBusy, which leaves it as it was.
    /// @return `mnav_success` whenever a search ran, however it ended;
    /// `mnav_errorInvalid` for a NULL argument, a point that is not finite, a
    /// polygon id that never existed, a filter not built from
    /// mnavDefaultQueryFilter or with a NaN cost or one whose included areas
    /// differ in cost; `mnav_errorLimit` for a point more than
    /// MNAV_MAX_QUERY_DISTANCE from the navmesh's origin on an axis, or a
    /// filter cost past MNAV_MIN_AREA_COST or MNAV_MAX_AREA_COST, an infinity
    /// included; `mnav_errorStale` for a polygon id whose tile has been
    /// replaced or removed. Also `mnav_errorBusy` when called back in from host
    /// code a call using the context runs (a link generation's clearance test,
    /// a hierarchy's allocator as it builds), one misuse counted.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    /// Any number of contexts may search one navmesh at once between
    /// commits.
    MNAV_NODISCARD MNAV_API mnavResult mnavQuery_FindShortestPath(
        mnavQuery* query, const mnavNavmesh* navmesh, mnavPolygonId startPolygon, mnavPos3 start,
        mnavPolygonId endPolygon, mnavPos3 end, const mnavQueryFilter* filter, mnavPath* pathOut);

    /// Begins a path search to run in slices: the same search
    /// as mnavQuery_FindPath, with its own copy of the filter, continued by
    /// mnavQuery_ContinuePath and ended by mnavQuery_FinishPath. Whatever the slices,
    /// the result is the one mnavQuery_FindPath gives.
    ///
    /// @param query        The context; it holds the search until the next
    ///                     search of any kind, or a move along the
    ///                     surface, begins.
    /// @param navmesh      The navmesh.
    /// @param startPolygon The polygon the start point lies on.
    /// @param start        The start point.
    /// @param endPolygon   The polygon the end point lies on.
    /// @param end          The end point.
    /// @param filter       The areas usable and their costs, or NULL for
    ///                     every walkable area at a cost of 1; copied.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, a point
    /// that is not finite, a polygon id that never existed or a filter not
    /// built from mnavDefaultQueryFilter or with a NaN cost; `mnav_errorLimit`
    /// for a point more than MNAV_MAX_QUERY_DISTANCE from the navmesh's origin
    /// on an axis, or a filter cost past MNAV_MIN_AREA_COST or
    /// MNAV_MAX_AREA_COST, an infinity included; `mnav_errorStale` for a
    /// polygon id whose tile has been replaced or removed. Also
    /// `mnav_errorBusy` when called back in from host code a call using the
    /// context runs (a link generation's clearance test, a hierarchy's
    /// allocator as it builds), one misuse counted.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavQuery_BeginPath(
        mnavQuery* query, const mnavNavmesh* navmesh, mnavPolygonId startPolygon, mnavPos3 start,
        mnavPolygonId endPolygon, mnavPos3 end, const mnavQueryFilter* filter);

    /// Continues a search begun by mnavQuery_BeginPath: closes up to a number of
    /// nodes, fewer when the search ends first.
    ///
    /// @param query    The context.
    /// @param navmesh  The navmesh the search began on.
    /// @param nodes    The most nodes to close, at least 1.
    /// @param endedOut Receives whether the search has ended. Zeroed on any
    ///                 failure but mnav_errorBusy, which leaves it as it was.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, fewer
    /// than 1 node, or a navmesh other than the search's; `mnav_errorState` for
    /// no search begun, or a navmesh committed to since the search began: begin
    /// again. Also `mnav_errorBusy` when called back in from host code a call
    /// using the context runs (a link generation's clearance test, a
    /// hierarchy's allocator as it builds), one misuse counted.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavQuery_ContinuePath(mnavQuery* query,
                                                              const mnavNavmesh* navmesh,
                                                              int32_t nodes, bool* endedOut);

    /// Ends a search begun by mnavQuery_BeginPath and writes its result: as
    /// mnavQuery_FindPath's when it has ended; otherwise toward the node nearest
    /// the end, its end mnav_pathUnfinished.
    ///
    /// @param query    The context; its memory holds the result.
    /// @param navmesh  The navmesh the search began on.
    /// @param pathOut  Receives the result. Zeroed on any failure but
    ///                 mnav_errorBusy, which leaves it as it was.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument or a
    /// navmesh other than the search's; `mnav_errorState` for no search begun,
    /// or a navmesh committed to since the search began. Also `mnav_errorBusy`
    /// when called back in from host code a call using the context runs (a link
    /// generation's clearance test, a hierarchy's allocator as it builds), one
    /// misuse counted.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavQuery_FinishPath(mnavQuery* query,
                                                            const mnavNavmesh* navmesh,
                                                            mnavPath* pathOut);

    // How a move along the surface ended.
    typedef int32_t mnavMoveEnd;

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
    /// navmesh, as far as the walls allow: a breadth-first
    /// walk over the polygons the filter includes whose shared edges meet
    /// the circle round the move, in edge order. In the polygon holding the
    /// wanted point the move reaches it; otherwise it ends at the point
    /// nearest it on the walls met, ties to the wall met first.
    ///
    /// @param query        The context; its memory holds the polygons.
    /// @param navmesh      The navmesh.
    /// @param startPolygon The polygon the start point lies on.
    /// @param start        The start point.
    /// @param end          The wanted point; only its ground position
    ///                     counts.
    /// @param filter       The areas usable, or NULL for every walkable one.
    /// @param moveOut      Receives the result. Zeroed on any failure but
    ///                     mnav_errorBusy, which leaves it as it was.
    /// @return `mnav_success` whenever the move ran, however it ended;
    /// `mnav_errorInvalid` for a NULL argument, a point that is not finite, a
    /// polygon id that never existed or a filter not built from
    /// mnavDefaultQueryFilter or with a NaN cost; `mnav_errorLimit` for a point
    /// more than MNAV_MAX_QUERY_DISTANCE from the navmesh's origin on an axis
    /// (the end's x and z), or a filter cost past MNAV_MIN_AREA_COST or
    /// MNAV_MAX_AREA_COST, an infinity included; `mnav_errorStale` for a
    /// polygon id whose tile has been replaced or removed. Also
    /// `mnav_errorBusy` when called back in from host code a call using the
    /// context runs (a link generation's clearance test, a hierarchy's
    /// allocator as it builds), one misuse counted.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    /// Any number of contexts may move on one navmesh at once between
    /// commits.
    MNAV_NODISCARD MNAV_API mnavResult mnavQuery_MoveAlongSurface(
        mnavQuery* query, const mnavNavmesh* navmesh, mnavPolygonId startPolygon, mnavPos3 start,
        mnavPos3 end, const mnavQueryFilter* filter, mnavMove* moveOut);

    // A path corridor: the polygons from the agent's to its
    // target's, in a buffer the caller owns, with the agent's position on
    // the first and the target on the last. It is well formed with a
    // finite position and target, a buffer, a capacity of at least 1, and
    // from 1 to capacity polygons; every corridor call refuses one that is
    // not, and every one that takes a navmesh also one whose position or
    // target lies more than MNAV_MAX_QUERY_DISTANCE from the navmesh's
    // origin on an axis.
    typedef struct mnavCorridor
    {
        mnavPos3 position;
        mnavPos3 target;
        mnavPolygonId* polygons;
        int32_t polygonCount;
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

    // A run of a straight path over one area: from its start
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
    /// @param path     A path from mnavQuery_FindPath or mnavQuery_FinishPath,
    ///                 as the search gave it, before its context's next search.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, a
    /// corridor with no buffer or a capacity below 1, or a path with no
    /// polygon or point, a NULL `polygons` or `points`, or a path no search
    /// gave: arrays other than its context's, or more polygons, points or
    /// links than the search gave; `mnav_errorStale` for a path a later
    /// search of its context has replaced; `mnav_errorCapacity` when the
    /// path has more polygons than the corridor's buffer. The corridor is
    /// unchanged on any failure.
    /// @par Thread safety
    /// Safe from any thread; the corridor is used by one thread at a time,
    /// and the path's context, which it reads, by no other thread meanwhile.
    MNAV_NODISCARD MNAV_API mnavResult mnavSetCorridor(mnavCorridor* corridor,
                                                       const mnavPath* path);

    /// Counts a corridor's leading polygons that are still current, of
    /// areas the filter includes, and joined to the one before by an
    /// edge, a tile link or an off-mesh link of a kind it includes. Fewer
    /// than all means the corridor needs trimming there and replanning.
    ///
    /// @param navmesh       The navmesh.
    /// @param corridor      The corridor.
    /// @param filter        The areas and kinds usable, or NULL for all.
    /// @param validCountOut Receives the count. Zeroed on any failure.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, a
    /// corridor not well formed or a filter not built from
    /// mnavDefaultQueryFilter or with a NaN cost; `mnav_errorLimit` for a
    /// corridor whose position or target lies more than MNAV_MAX_QUERY_DISTANCE
    /// from the navmesh's origin on an axis, or a filter cost past
    /// MNAV_MIN_AREA_COST or MNAV_MAX_AREA_COST, an infinity included.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may check corridors at
    /// once between commits.
    MNAV_NODISCARD MNAV_API mnavResult mnavNavmesh_CheckCorridor(const mnavNavmesh* navmesh,
                                                                 const mnavCorridor* corridor,
                                                                 const mnavQueryFilter* filter,
                                                                 int32_t* validCountOut);

    /// Finds a corridor's straight path from its position to its target:
    /// the funnel over the portals between its polygons.
    ///
    /// @param query    The context; its memory holds the corners.
    /// @param corridor The corridor.
    /// @param navmesh  The navmesh.
    /// @param cornersOut Receives the straight path. Zeroed on any failure but
    ///                   mnav_errorBusy, which leaves it as it was.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, a
    /// corridor not well formed or polygons not joined; `mnav_errorLimit` for a
    /// corridor whose position or target lies more than MNAV_MAX_QUERY_DISTANCE
    /// from the navmesh's origin on an axis; `mnav_errorStale` for a polygon
    /// whose tile has been replaced or removed; `mnav_errorCapacity` for a
    /// corridor of more polygons than the context's node limit. Also
    /// `mnav_errorBusy` when called back in from host code a call using the
    /// context runs (a link generation's clearance test, a hierarchy's
    /// allocator as it builds), one misuse counted.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavQuery_FindCorridorCorners(mnavQuery* query,
                                                                     const mnavCorridor* corridor,
                                                                     const mnavNavmesh* navmesh,
                                                                     mnavCorners* cornersOut);

    /// Finds the areas along a corridor's straight path: the corners
    /// mnavQuery_FindCorridorCorners gives, which replace the context's last ones,
    /// and a run where the path starts, one wherever it crosses into a
    /// polygon of another area, and one where it lands from each off-mesh
    /// link, in order. Hosts change gait or sound at a run's start; a
    /// path's areas are its corridor's, once loaded with mnavSetCorridor.
    ///
    /// @param query      The context; its memory holds the corners.
    /// @param corridor   The corridor.
    /// @param navmesh    The navmesh.
    /// @param cornersOut Receives the straight path, or NULL. Zeroed on any
    ///                   failure but mnav_errorBusy, which leaves it as it was.
    /// @param runsOut    Receives the runs, in order. May be NULL when
    ///                   capacity is 0. Zeroed over the capacity on any
    ///                   failure but mnav_errorCapacity, which keeps the first
    ///                   capacity, and mnav_errorBusy, which leaves it as it
    ///                   was.
    /// @param capacity   How many runs fit, at least 0.
    /// @param countOut   Receives how many runs there are, also when they do not
    ///                   all fit. Zeroed on any failure but mnav_errorCapacity,
    ///                   which gives the total, and mnav_errorBusy, which leaves
    ///                   it as it was.
    /// @return As mnavQuery_FindCorridorCorners; also `mnav_errorInvalid` for a NULL
    /// countOut, a negative capacity or NULL runs with a positive one, and
    /// `mnav_errorCapacity` when the runs do not fit, the first capacity of
    /// them written. Also `mnav_errorBusy` when called back in from host code a
    /// call using the context runs (a link generation's clearance test, a
    /// hierarchy's allocator as it builds), one misuse counted.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavQuery_FindCorridorAreas(
        mnavQuery* query, const mnavCorridor* corridor, const mnavNavmesh* navmesh,
        mnavCorners* cornersOut, mnavAreaRun* runsOut, int32_t capacity, int32_t* countOut);

    // The fastest mnavSteer may steer, and the longest distance it slows
    // or stops within, in meters per second and meters.
#define MNAV_MAX_STEER_SPEED    1000.0f
#define MNAV_MAX_STEER_DISTANCE 1000.0f

    // How mnavSteer steers. Build it with mnavDefaultSteerDef.
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
        bool enableTurnAnticipation;
    } mnavSteerDef;

    // What mnavSteer found.
    typedef int32_t mnavSteerState;

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
        mnavVec3 velocity;
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
        def.enableTurnAnticipation = true;
        return def;
    }

    /// Steers along a corridor's corners: toward the next
    /// corner, or swinging wide of it, at the def's speed, slowing within
    /// its slowing distance of the target by the distance left along the
    /// corners over it; stopped within the arrival distance of the target
    /// or of the next link's takeoff. Keeps no state: call it each step
    /// with the corners mnavQuery_FindCorridorCorners gives, feed the velocity to
    /// avoidance, and limit acceleration and move the agent yourself. The
    /// corners may be the caller's own: their arrays are the caller's
    /// memory, read only within `pointCount` and `linkCount`, every point
    /// checked; a link's `point` is compared, never followed.
    ///
    /// @param corners     The corners, the agent's position first.
    /// @param def         The def, from mnavDefaultSteerDef.
    /// @param steeringOut Receives the velocity and state. Left as it was on
    ///                    failure.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, corners
    /// with no points or a link count below 0, or a def not from
    /// mnavDefaultSteerDef;
    /// `mnav_errorVersion` for one built against another version of the
    /// headers; `mnav_errorInvalid` also for a def value below 0 or NaN, or a
    /// corner not finite; `mnav_errorLimit` for a speed past
    /// MNAV_MAX_STEER_SPEED or a distance past MNAV_MAX_STEER_DISTANCE, an
    /// infinity included.
    /// @par Thread safety
    /// Safe from any thread.
    MNAV_NODISCARD MNAV_API mnavResult mnavSteer(const mnavCorners* corners,
                                                 const mnavSteerDef* def,
                                                 mnavSteering* steeringOut);

    /// Moves a corridor's position along the surface toward a wanted point
    /// (mnavQuery_MoveAlongSurface from its first polygon) and merges the
    /// polygons walked into its start: it keeps its polygons from the
    /// farthest one the walk passed, led to by the walk.
    ///
    /// @param query    The context, for the walk.
    /// @param corridor The corridor.
    /// @param navmesh  The navmesh.
    /// @param wanted   Where the agent would be; only its ground position
    ///                 counts.
    /// @param filter   The areas usable, or NULL for every walkable one.
    /// @param moveOut  Receives the walk, or NULL. Zeroed on any failure but
    ///                 mnav_errorBusy, which leaves it as it was.
    /// @return As mnavQuery_MoveAlongSurface; also `mnav_errorInvalid` for a
    /// corridor not well formed, `mnav_errorLimit` for one whose position or
    /// target lies more than MNAV_MAX_QUERY_DISTANCE from the navmesh's origin
    /// on an axis, and `mnav_errorCapacity` when the merged corridor would
    /// outgrow its buffer, which leaves it unchanged. Also `mnav_errorBusy`
    /// when called back in from host code a call using the context runs (a link
    /// generation's clearance test, a hierarchy's allocator as it builds), one
    /// misuse counted.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    /// So is the corridor.
    MNAV_NODISCARD MNAV_API mnavResult
    mnavQuery_MoveCorridor(mnavQuery* query, mnavCorridor* corridor, const mnavNavmesh* navmesh,
                           mnavPos3 wanted, const mnavQueryFilter* filter, mnavMove* moveOut);

    /// Moves a corridor's target along the surface toward a wanted point
    /// (mnavQuery_MoveAlongSurface from its last polygon) and merges the polygons
    /// walked into its end: it keeps its polygons up to the first one the
    /// walk passed, then the rest of the walk.
    ///
    /// @param query    The context, for the walk.
    /// @param corridor The corridor.
    /// @param navmesh  The navmesh.
    /// @param wanted   Where the target would be; only its ground position
    ///                 counts.
    /// @param filter   The areas usable, or NULL for every walkable one.
    /// @param moveOut  Receives the walk, or NULL. Zeroed on any failure but
    ///                 mnav_errorBusy, which leaves it as it was.
    /// @return As mnavQuery_MoveCorridor. Also `mnav_errorBusy` when called back in
    /// from host code a call using the context runs (a link generation's
    /// clearance test, a hierarchy's allocator as it builds), one misuse
    /// counted.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    /// So is the corridor.
    MNAV_NODISCARD MNAV_API mnavResult mnavQuery_MoveCorridorTarget(
        mnavQuery* query, mnavCorridor* corridor, const mnavNavmesh* navmesh, mnavPos3 wanted,
        const mnavQueryFilter* filter, mnavMove* moveOut);

    /// Shortens a corridor where the agent can see ahead: casts a ray from
    /// its position toward a point, usually a corner a few ahead; when the
    /// ray reaches it, the polygons it crossed replace the corridor's
    /// start up to the last corridor polygon it passed, if that is fewer.
    ///
    /// @param query        The context, for the ray.
    /// @param corridor     The corridor.
    /// @param navmesh      The navmesh.
    /// @param toward       The point to look toward.
    /// @param filter       The areas usable, or NULL for every walkable one.
    /// @param shortenedOut Receives whether the corridor changed. Zeroed on any
    ///                     failure but mnav_errorBusy, which leaves it as it was.
    /// @return As mnavQuery_Raycast; also `mnav_errorInvalid` for a corridor
    /// not well formed or a NULL shortenedOut, and `mnav_errorLimit` for one
    /// whose position or target lies more than MNAV_MAX_QUERY_DISTANCE from the
    /// navmesh's origin on an axis. Also `mnav_errorBusy` when called back in
    /// from host code a call using the context runs (a link generation's
    /// clearance test, a hierarchy's allocator as it builds), one misuse
    /// counted.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    /// So is the corridor.
    MNAV_NODISCARD MNAV_API mnavResult
    mnavQuery_ShortcutCorridor(mnavQuery* query, mnavCorridor* corridor, const mnavNavmesh* navmesh,
                               mnavPos3 toward, const mnavQueryFilter* filter, bool* shortenedOut);

    /// Plans a corridor again from its position to its target and loads
    /// the path found: from its first and last polygons while they are
    /// current, else from the polygons nearest the position and the target
    /// within a box round each. A corridor whose position or target finds
    /// no polygon is left as it was, the path's end saying why.
    ///
    /// @param query       The context, for the search; it holds the path.
    /// @param corridor    The corridor.
    /// @param navmesh     The navmesh.
    /// @param halfExtents The box's half sizes for finding polygons again, in
    ///                    meters, at least 0; checked on every call.
    /// @param filter      The areas usable and their costs, or NULL.
    /// @param pathOut     Receives the path, or an empty one ended
    ///                    mnav_pathNotLoaded or mnav_pathNone when an end found
    ///                    no polygon. Zeroed on any failure but mnav_errorBusy,
    ///                    which leaves it as it was.
    /// @return `mnav_success` whenever the corridor was looked at;
    /// `mnav_errorInvalid` for a NULL argument, a corridor not well formed or a
    /// half size below 0 or not finite; `mnav_errorLimit` for a corridor whose
    /// position or target lies more than MNAV_MAX_QUERY_DISTANCE from the
    /// navmesh's origin on an axis; `mnav_errorCapacity` when the path has more
    /// polygons than the buffer, which leaves the corridor as it was; the
    /// errors of mnavNavmesh_FindNearest and mnavQuery_FindPath otherwise. Also
    /// `mnav_errorBusy` when called back in from host code a call using the
    /// context runs (a link generation's clearance test, a hierarchy's
    /// allocator as it builds), one misuse counted.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    /// So is the corridor.
    MNAV_NODISCARD MNAV_API mnavResult mnavQuery_ReplanCorridor(
        mnavQuery* query, mnavCorridor* corridor, const mnavNavmesh* navmesh, mnavVec3 halfExtents,
        const mnavQueryFilter* filter, mnavPath* pathOut);

    // The longest side a grid may have, in cells.
#define MNAV_MAX_GRID_SIDE 32768

    // A grid for grid pathfinding: cells in rows from (0, 0),
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

    // A box of a grid's cells, from (x0, y0) to (x1, y1), both included.
    typedef struct mnavCellRange
    {
        int32_t x0;
        int32_t y0;
        int32_t x1;
        int32_t y1;
    } mnavCellRange;

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
    /// mnavQuery_FindPath does, with the path to the cell nearest the end when
    /// the end is not reached. A grid search ends any sliced search.
    ///
    /// @param query   The context; its memory holds the path.
    /// @param grid    The grid.
    /// @param start   The start cell.
    /// @param end     The end cell.
    /// @param filter  The areas usable and their costs, or NULL.
    /// @param pathOut Receives the path; mnav_pathNone with the start alone when
    ///                the start is blocked. Zeroed on any failure but
    ///                mnav_errorBusy, which leaves it as it was.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, a grid
    /// with no areas, a side below 1 or a cell size not more than 0 or not
    /// finite, a cell outside the grid, or a filter not built from
    /// mnavDefaultQueryFilter or with a NaN cost; `mnav_errorLimit` for a side
    /// past MNAV_MAX_GRID_SIDE or a filter cost past MNAV_MIN_AREA_COST or
    /// MNAV_MAX_AREA_COST, an infinity included. Also `mnav_errorBusy` when
    /// called back in from host code a call using the context runs (a link
    /// generation's clearance test, a hierarchy's allocator as it builds), one
    /// misuse counted.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavQuery_FindGridPath(mnavQuery* query,
                                                              const mnavGrid* grid, mnavCell start,
                                                              mnavCell end,
                                                              const mnavQueryFilter* filter,
                                                              mnavGridPath* pathOut);

    /// Reads the height of a polygon's detail surface at a point on the
    /// ground.
    ///
    /// @param navmesh   The navmesh.
    /// @param polygon   The polygon.
    /// @param x         The point's X.
    /// @param z         The point's Z.
    /// @param heightOut Receives the height. Zeroed on any failure.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, a
    /// coordinate not finite or a polygon id never handed out;
    /// `mnav_errorLimit` for a coordinate more than MNAV_MAX_QUERY_DISTANCE
    /// from the navmesh's origin on an axis; `mnav_errorOutside` for a point
    /// outside the polygon on the ground; `mnav_errorStale` for a polygon whose
    /// tile has gone.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read the navmesh
    /// at once while no commit runs on it.
    MNAV_NODISCARD MNAV_API mnavResult mnavPolygon_GetHeight(const mnavNavmesh* navmesh,
                                                             mnavPolygonId polygon, double x,
                                                             double z, double* heightOut);

    // A wall within a radius: an edge no polygon the filter includes lies
    // across.
    typedef struct mnavWall
    {
        // Its ends, in the order of its polygon's corners.
        mnavPos3 start;
        mnavPos3 end;
        // Its point nearest the center.
        mnavPos3 point;
        // The unit direction on the ground from it toward its polygon, the
        // walkable side; zero for an edge of no length on the ground. The
        // way from the wall to the center is (center - point) / distance.
        mnavVec3 normal;
        // The distance on the ground from the center to its nearest point.
        double distance;
    } mnavWall;

    // The nearest wall a search within a radius found.
    typedef struct mnavNearestWall
    {
        // Whether a wall lies within the radius.
        bool found;
        // Whether the context's node limit cut the search short, so that a
        // nearer wall may lie unseen.
        bool limited;
        // The wall; zeroed when none was found.
        mnavWall wall;
    } mnavNearestWall;

    /// Finds the nearest wall to a point within a radius on the ground,
    /// searching from its polygon across the edges that come within the
    /// radius, off-mesh links aside. An edge is a wall where no polygon the
    /// filter includes lies across it: a tile side with no tile loaded
    /// beyond, and the parts of a side no polygon of the tile beyond
    /// covers, are walls. A wall counts only on the center's floor: its
    /// point nearest the center lies no more than the agent's step height
    /// below the center's height on its polygon, and no more than the
    /// agent's height above, for the agent the navmesh's tiles are baked
    /// for; the walls of a floor over or under the center are left out. A
    /// wall exactly the radius away lies within it; of walls at the same
    /// distance it gives one, the same on every platform, but which one is
    /// not promised.
    ///
    /// @param query      The context; its last search ends.
    /// @param navmesh    The navmesh.
    /// @param polygon    The polygon the center lies in.
    /// @param center     The point.
    /// @param radius     How far to look, in meters, at least 0 and at most
    ///                   MNAV_MAX_QUERY_DISTANCE.
    /// @param filter     The areas usable, or NULL.
    /// @param nearestOut Receives whether a wall was found, and the wall. Zeroed
    ///                   on any failure but mnav_errorBusy, which leaves it as it
    ///                   was.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, a point
    /// or radius not finite, a negative radius, a polygon id never handed out,
    /// or a filter not built from mnavDefaultQueryFilter or with a NaN cost;
    /// `mnav_errorStale` for a polygon whose tile has gone; `mnav_errorLimit`
    /// for a center more than MNAV_MAX_QUERY_DISTANCE from the navmesh's origin
    /// on an axis, a radius past MNAV_MAX_QUERY_DISTANCE, or a filter cost past
    /// MNAV_MIN_AREA_COST or MNAV_MAX_AREA_COST, an infinity included. Also
    /// `mnav_errorBusy` when called back in from host code a call using the
    /// context runs (a link generation's clearance test, a hierarchy's
    /// allocator as it builds), one misuse counted.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavQuery_FindWallDistance(
        mnavQuery* query, const mnavNavmesh* navmesh, mnavPolygonId polygon, mnavPos3 center,
        double radius, const mnavQueryFilter* filter, mnavNearestWall* nearestOut);

    // What a search for the walls within a radius found.
    typedef struct mnavWallsFound
    {
        // The walls within the radius, also beyond the buffer's capacity.
        int32_t wallCount;
        // Whether the context's node limit cut the search short, so that
        // more walls may lie unseen.
        bool limited;
    } mnavWallsFound;

    /// Lists the walls within a radius of a point on the ground, nearest
    /// first, for avoidance's segment obstacles: the search and the walls
    /// of mnavQuery_FindWallDistance, whose wall is the first listed: only
    /// walls on the center's floor, as it counts them. Walls at the same
    /// distance keep the order the search meets them in, the same on every
    /// platform.
    ///
    /// @param query    The context; its last search ends.
    /// @param navmesh  The navmesh.
    /// @param polygon  The polygon the center lies in.
    /// @param center   The point.
    /// @param radius   How far to look, in meters, at least 0 and at most
    ///                 MNAV_MAX_QUERY_DISTANCE.
    /// @param filter   The areas usable, or NULL.
    /// @param wallsOut Receives up to capacity walls, the nearest; NULL when
    ///                 capacity is 0. Zeroed over the capacity on any failure
    ///                 but mnav_errorCapacity, which keeps the first capacity,
    ///                 and mnav_errorBusy, which leaves it as it was.
    /// @param capacity The room, at least 0.
    /// @param foundOut Receives the count and whether the node limit cut the
    ///                 search short. Zeroed on any failure but
    ///                 mnav_errorCapacity, which gives the total, and
    ///                 mnav_errorBusy, which leaves it as it was.
    /// @return `mnav_success`; `mnav_errorCapacity` when more walls lie within
    /// the radius than the buffer holds: it holds the nearest and foundOut
    /// counts all; `mnav_errorInvalid` for a NULL argument, a negative
    /// capacity, a point or radius not finite, a negative radius, a polygon id
    /// never handed out, or a filter not built from mnavDefaultQueryFilter or
    /// with a NaN cost; `mnav_errorStale` for a polygon whose tile has gone;
    /// `mnav_errorLimit` for a center more than MNAV_MAX_QUERY_DISTANCE from
    /// the navmesh's origin on an axis, a radius past MNAV_MAX_QUERY_DISTANCE,
    /// or a filter cost past MNAV_MIN_AREA_COST or MNAV_MAX_AREA_COST, an
    /// infinity included. Also `mnav_errorBusy` when called back in from host
    /// code a call using the context runs (a link generation's clearance test,
    /// a hierarchy's allocator as it builds), one misuse counted.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult
    mnavQuery_FindWalls(mnavQuery* query, const mnavNavmesh* navmesh, mnavPolygonId polygon,
                        mnavPos3 center, double radius, const mnavQueryFilter* filter,
                        mnavWall* wallsOut, int32_t capacity, mnavWallsFound* foundOut);

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
    /// @param seed     Any value.
    /// @param filter   The areas usable, or NULL.
    /// @param pointOut Receives the point. Zeroed on any failure.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument or a
    /// filter not built from mnavDefaultQueryFilter or with a NaN cost;
    /// `mnav_errorLimit` for a filter cost past MNAV_MIN_AREA_COST or
    /// MNAV_MAX_AREA_COST, an infinity included.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read the navmesh
    /// at once while no commit runs on it.
    MNAV_NODISCARD MNAV_API mnavResult mnavNavmesh_FindRandomPoint(const mnavNavmesh* navmesh,
                                                                   uint64_t seed,
                                                                   const mnavQueryFilter* filter,
                                                                   mnavRandomPoint* pointOut);

    /// Picks a point reachable from a center: a polygon the walls search
    /// reaches within the radius, by ground area, then a point uniformly
    /// on it, which may lie beyond the radius by up to the polygon's size.
    ///
    /// @param query    The context; its last search ends.
    /// @param navmesh  The navmesh.
    /// @param polygon  The polygon the center lies in.
    /// @param center   The center.
    /// @param radius   How far to search, in meters, at least 0 and at most
    ///                 MNAV_MAX_QUERY_DISTANCE.
    /// @param seed     Any value.
    /// @param filter   The areas usable, or NULL.
    /// @param pointOut Receives the point. Zeroed on any failure but
    ///                 mnav_errorBusy, which leaves it as it was.
    /// @return As mnavQuery_FindWallDistance; also `mnav_errorLimit` when a polygon
    /// clipped to the circle, its 32-gon, has more than MNAV_MAX_CLIP_CORNERS
    /// corners, which rounding alone can cause. Also `mnav_errorBusy` when
    /// called back in from host code a call using the context runs (a link
    /// generation's clearance test, a hierarchy's allocator as it builds), one
    /// misuse counted.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavQuery_FindRandomPointAround(
        mnavQuery* query, const mnavNavmesh* navmesh, mnavPolygonId polygon, mnavPos3 center,
        double radius, uint64_t seed, const mnavQueryFilter* filter, mnavRandomPoint* pointOut);

    /// Tells whether the end can be reached from the start: the path search
    /// with its limits, without a path made. mnav_pathFound when it can,
    /// mnav_pathNone when it cannot, or the limit that stopped the search
    /// before it could tell.
    ///
    /// @param query        The context; its last search ends.
    /// @param navmesh      The navmesh.
    /// @param startPolygon The polygon the start lies in.
    /// @param start        The start point.
    /// @param endPolygon   The polygon the end lies in.
    /// @param end          The end point.
    /// @param filter       The areas usable and their costs, or NULL.
    /// @param endOut       Receives how the search ended. Zeroed on any failure
    ///                     but mnav_errorBusy, which leaves it as it was.
    /// @return As mnavQuery_FindPath. Also `mnav_errorBusy` when called back in from
    /// host code a call using the context runs (a link generation's clearance
    /// test, a hierarchy's allocator as it builds), one misuse counted.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavQuery_CheckReachable(
        mnavQuery* query, const mnavNavmesh* navmesh, mnavPolygonId startPolygon, mnavPos3 start,
        mnavPolygonId endPolygon, mnavPos3 end, const mnavQueryFilter* filter, mnavPathEnd* endOut);

    // How a raycast ended.
    typedef int32_t mnavRayEnd;

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
        mnavVec3 normal;
        // The polygons crossed, from the start polygon to the one where the
        // ray ended, in the context's memory until its next query.
        const mnavPolygonId* polygons;
        int32_t polygonCount;
    } mnavRay;

    /// Casts a ray along the navmesh on the ground from a point on a polygon
    /// toward an end point: polygon to polygon through the
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
    /// @param startPolygon The polygon the start point lies on, as
    ///                     mnavNavmesh_FindNearest gives it.
    /// @param start        The start point.
    /// @param end          The end point; only its ground position counts.
    /// @param filter       The areas usable, or NULL for every walkable one.
    /// @param rayOut       Receives the result. Zeroed on any failure but
    ///                     mnav_errorBusy, which leaves it as it was.
    /// @return `mnav_success` whenever the ray was cast, however it ended;
    /// `mnav_errorInvalid` for a NULL argument, a point that is not finite, a
    /// polygon id that never existed, or a filter not built from
    /// mnavDefaultQueryFilter or with a NaN cost; `mnav_errorLimit` for a point
    /// more than MNAV_MAX_QUERY_DISTANCE from the navmesh's origin on an axis
    /// (the end's x and z), or a filter cost past MNAV_MIN_AREA_COST or
    /// MNAV_MAX_AREA_COST, an infinity included; `mnav_errorStale` for a
    /// polygon id whose tile has been replaced or removed. Also
    /// `mnav_errorBusy` when called back in from host code a call using the
    /// context runs (a link generation's clearance test, a hierarchy's
    /// allocator as it builds), one misuse counted.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    /// Any number of contexts may cast on one navmesh at once between
    /// commits.
    MNAV_NODISCARD MNAV_API mnavResult
    mnavQuery_Raycast(mnavQuery* query, const mnavNavmesh* navmesh, mnavPolygonId startPolygon,
                      mnavPos3 start, mnavPos3 end, const mnavQueryFilter* filter, mnavRay* rayOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_NAV_QUERY_H
