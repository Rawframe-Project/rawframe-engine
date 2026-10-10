// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Flow fields over the navmesh (mnav-0013): for every polygon, the cost of
// its cheapest way to the nearest of a set of goal points and the polygon
// to go to next, so that any number of agents sharing the goals find their
// way at the cost of one search.

#ifndef MAUL_NAV_NAVFLOW_H
#define MAUL_NAV_NAVFLOW_H

#include "maul-nav/base.h"
#include "maul-nav/draw.h"
#include "maul-nav/navmesh.h"
#include "maul-nav/query.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The most polygons, tile slots and off-mesh links a navmesh flow field
// may hold.
#define MNAV_MAX_NAVFLOW_POLYGONS 16777216
#define MNAV_MAX_NAVFLOW_TILES    1048576
#define MNAV_MAX_NAVFLOW_LINKS    1048576

    // What a navmesh flow field may hold; its memory is taken for them all
    // when it is made, and a build needs no more.
    typedef struct mnavNavFlowLimits
    {
        // Polygons in the tiles searched, 1 to MNAV_MAX_NAVFLOW_POLYGONS.
        int32_t polygons;
        // Tile slots of the navmesh, 1 to MNAV_MAX_NAVFLOW_TILES.
        int32_t tiles;
        // Off-mesh links of the navmesh, counted as its links limit counts
        // them (by crossings), 1 to MNAV_MAX_NAVFLOW_LINKS.
        int32_t links;
    } mnavNavFlowLimits;

    // How a navmesh flow field is made. Build it with
    // mnavDefaultNavFlowDef.
    typedef struct mnavNavFlowDef
    {
        uint32_t cookie;
        // MNAV_ABI_VERSION, set by mnavDefaultNavFlowDef.
        uint32_t version;
        // The allocator the field uses; zeroed for the C library's.
        mnavAllocator allocator;
        mnavNavFlowLimits limits;
    } mnavNavFlowDef;

    // A navmesh flow field: the memory for its polygons, and the field last
    // built.
    typedef struct mnavNavFlow mnavNavFlow;

    // A goal: a point on a polygon.
    typedef struct mnavNavFlowGoal
    {
        mnavPolygonId polygon;
        mnavPos3 point;
    } mnavNavFlowGoal;

    // A polygon's way to the goals. An agent on the polygon heads for the
    // portal from left to right, then goes on to the next polygon; where
    // the way leaves by an off-mesh link, both ends are the link's takeoff
    // point and link names it.
    typedef struct mnavPolygonFlow
    {
        // The cost from the portal's midpoint, or the goal point on a goal
        // polygon; infinite where no goal is reached.
        double cost;
        // The next polygon, zeroed on a goal polygon and where no goal is
        // reached.
        mnavPolygonId next;
        mnavPos3 left;
        mnavPos3 right;
        // The link taken, zeroed when none.
        mnavLinkId link;
    } mnavPolygonFlow;

// The cookie mnavDefaultNavFlowDef sets and mnavCreateNavFlow checks.
#define MNAV_NAV_FLOW_DEF_COOKIE 0x4E41564Eu

    /// Returns the default navmesh flow field def: up to 65,536 polygons,
    /// 4,096 tile slots and 4,096 off-mesh links.
    /// Built in the program, so that it carries the version of the
    /// headers the program includes.
    ///
    /// @return The def.
    /// @par Thread safety
    /// Safe from any thread.
    static inline mnavNavFlowDef mnavDefaultNavFlowDef(void)
    {
#ifdef __cplusplus
        mnavNavFlowDef def = {};
#else
    mnavNavFlowDef def = {0};
#endif
        def.cookie = MNAV_NAV_FLOW_DEF_COOKIE;
        def.version = MNAV_ABI_VERSION;
        def.limits.polygons = 65536;
        def.limits.tiles = 4096;
        def.limits.links = 4096;
        return def;
    }

    /// Makes a navmesh flow field with the memory its limits need.
    ///
    /// @param def      The def, from mnavDefaultNavFlowDef.
    /// @param fieldOut Receives the field, or NULL on failure.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument or a def
    /// not from mnavDefaultNavFlowDef; `mnav_errorVersion` for one built
    /// against another major or minor version; `mnav_errorRange` for a limit
    /// out of its range; `mnav_errorCapacity` when the allocator fails.
    /// @par Thread safety
    /// Safe from any thread.
    MNAV_NODISCARD MNAV_API mnavResult mnavCreateNavFlow(const mnavNavFlowDef* def,
                                                         mnavNavFlow** fieldOut);

    /// Destroys a navmesh flow field.
    ///
    /// @param field The field, or NULL.
    /// @par Thread safety
    /// Safe from any thread; the field is used by one thread at a time.
    MNAV_API void mnavDestroyNavFlow(mnavNavFlow* field);

    // A box of tile places, from (x0, z0) to (x1, z1), both included.
    typedef struct mnavNavFlowRegion
    {
        int32_t x0;
        int32_t z0;
        int32_t x1;
        int32_t z1;
    } mnavNavFlowRegion;

    /// Begins building the field over the tiles of a region, as
    /// mnavBuildNavFlow builds it over all; polygons outside the region
    /// are as left out, and only those inside count toward the field's
    /// limit. mnavContinueNavFlow does the work; however it is divided, the
    /// field is the same.
    ///
    /// @param field     The field; its last field is dropped.
    /// @param navmesh   The navmesh; the work needs it unchanged.
    /// @param filter    The areas usable and their costs, and the link
    ///                  kinds, or NULL; copied.
    /// @param region    The tiles searched, or NULL for all.
    /// @param goals     The goals; those outside the region are left out.
    /// @param goalCount How many, at least 0.
    /// @return As mnavBuildNavFlow; also `mnav_errorInvalid` for a region
    /// with x0 past x1 or z0 past z1.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read the navmesh at
    /// once while no commit runs on it; the field is used by one thread at
    /// a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavBeginNavFlow(
        mnavNavFlow* field, const mnavNavmesh* navmesh, const mnavQueryFilter* filter,
        const mnavNavFlowRegion* region, const mnavNavFlowGoal* goals, int32_t goalCount);

    /// Continues the work begun on a field: settles up to a number of
    /// polygons, fewer when the work ends first.
    ///
    /// @param field    The field.
    /// @param navmesh  The navmesh the work began on.
    /// @param polygons The most polygons to settle, at least 1.
    /// @param endedOut Receives whether the work has ended. May be NULL.
    /// @return `mnav_success`, also when the work had already ended;
    /// `mnav_errorInvalid` for a NULL field or navmesh, nothing begun, or
    /// fewer than 1 polygon; `mnav_errorStale` for another navmesh or one
    /// committed to since the work began: begin again.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read the navmesh at
    /// once while no commit runs on it; the field is used by one thread at
    /// a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavContinueNavFlow(mnavNavFlow* field,
                                                           const mnavNavmesh* navmesh,
                                                           int32_t polygons, bool* endedOut);

    /// Builds the field for a navmesh and a set of goal points by one
    /// search backward from all of them, as path searches price their ways
    /// (mnav-0005): a polygon stands at the midpoint of the portal it is
    /// left by toward the goals, and its cost is the next polygon's plus
    /// the walk between them times the next polygon's area cost, with an
    /// off-mesh link's cost where the way crosses one. Ties go to the lower
    /// cost, then the polygon of the lower slot and index; the same navmesh
    /// and goals give the same field on every platform. A polygon with two
    /// ways on of equal cost keeps one of them, the same on every platform,
    /// but which one is not promised. Goals on polygons the filter leaves
    /// out are left out.
    ///
    /// @param field     The field; its last field is replaced.
    /// @param navmesh   The navmesh; reads need it unchanged since.
    /// @param filter    The areas usable and their costs, and the link
    ///                  kinds, or NULL; copied.
    /// @param goals     The goals.
    /// @param goalCount How many, at least 0.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, a
    /// negative count, a goal polygon id never handed out or a goal point
    /// not finite, or a filter not built from mnavDefaultQueryFilter;
    /// `mnav_errorRange` for a filter cost out of its range;
    /// `mnav_errorStale` for a goal polygon whose tile was replaced or
    /// removed; `mnav_errorLimit` for a navmesh of more polygons (in the
    /// tiles searched), tile slots or off-mesh links than the field's
    /// limits. A build allocates nothing. On an error the field holds
    /// nothing.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read the navmesh at
    /// once while no commit runs on it; the field is used by one thread at
    /// a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavBuildNavFlow(mnavNavFlow* field,
                                                        const mnavNavmesh* navmesh,
                                                        const mnavQueryFilter* filter,
                                                        const mnavNavFlowGoal* goals,
                                                        int32_t goalCount);

    /// Reads a polygon's way to the goals from the field.
    ///
    /// @param field   The field.
    /// @param navmesh The navmesh the field was built on.
    /// @param polygon The polygon.
    /// @param flowOut Receives its way.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument,
    /// nothing begun, a polygon id never handed out or a polygon outside
    /// the region; `mnav_errorStale` for another navmesh, one committed to
    /// since the work began, a polygon whose tile was replaced or removed,
    /// or work not yet ended.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read a field at once
    /// while no build runs on it.
    MNAV_NODISCARD MNAV_API mnavResult mnavNavFlowAt(const mnavNavFlow* field,
                                                     const mnavNavmesh* navmesh,
                                                     mnavPolygonId polygon,
                                                     mnavPolygonFlow* flowOut);

    /// Appends an arrow for each polygon of the field that has a way on
    /// (mnav_debugFlow): from the mean of the polygon's corners to the
    /// midpoint of its portal, or to the takeoff point where the way leaves
    /// by an off-mesh link, with two barbs a quarter of its length (at most
    /// half a meter) on the ground plane.
    ///
    /// @param field   The field.
    /// @param navmesh The navmesh the field was built on.
    /// @param buffer  The buffer appended to.
    /// @return `mnav_success`, appending nothing when nothing was begun;
    /// `mnav_errorInvalid` for a NULL argument or a buffer with a count out
    /// of range, an array missing or an origin not finite;
    /// `mnav_errorStale` for another navmesh, one committed to since the
    /// work began, or work not yet ended; `mnav_errorCapacity` when the
    /// buffer filled, its counts saying what the whole needs.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read a field at once
    /// while no build runs on it; the buffer is used by one thread at a
    /// time.
    MNAV_NODISCARD MNAV_API mnavResult mnavDebugNavFlow(const mnavNavFlow* field,
                                                        const mnavNavmesh* navmesh,
                                                        mnavDebugBuffer* buffer);

#ifdef __cplusplus
}
#endif

#endif // MAUL_NAV_NAVFLOW_H
