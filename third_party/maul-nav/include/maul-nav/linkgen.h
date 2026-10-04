// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Link generation (mnav-0009): off-mesh links for drops off ledges and
// jumps across gaps, made from a committed navmesh for the host to stage.

#ifndef MAUL_NAV_LINKGEN_H
#define MAUL_NAV_LINKGEN_H

#include "maul-nav/base.h"
#include "maul-nav/navmesh.h"
#include "maul-nav/query.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The farthest a generated link may reach, across or down, in meters.
#define MNAV_MAX_LINK_REACH 100.0f

    // Decides whether the way between two points is free for the agent:
    // the host's collision test. True to keep the link.
    typedef bool (*mnavClearanceFn)(void* context, mnavPos3 from, mnavPos3 to);

    // How links are generated. Build it with mnavDefaultLinkGenDef.
    typedef struct mnavLinkGenDef
    {
        uint32_t cookie;
        // The distance between samples along an edge, in meters, more than
        // 0 and at most MNAV_MAX_LINK_REACH.
        float spacing;
        // The deepest drop, and the farthest jump across, in meters, 0 to
        // MNAV_MAX_LINK_REACH; 0 turns that kind off.
        float dropMax;
        float jumpMax;
        // Drops no deeper than this go both ways, in meters, at least 0.
        float climbMax;
        // A link is dropped when the navmesh walks from its start to its
        // landing within this many times the straight distance; at least
        // 1, or 0 to keep every link.
        float detour;
        // A link is dropped when both its ends lie within this distance of
        // an earlier link's, in meters, at least 0.
        float filterDistance;
        // The generated links' snap radius, 0 to MNAV_MAX_LINK_RADIUS, and
        // their costs, 0 to MNAV_MAX_LINK_COST.
        float radius;
        float dropCost;
        float jumpCost;
        mnavLinkKind dropKind;
        mnavLinkKind jumpKind;
        // The host's collision test, or NULL for none, and its context.
        mnavClearanceFn clear;
        void* context;
    } mnavLinkGenDef;

    /// Returns the default generation def: samples every 1 m, drops up to
    /// 3 m going both ways up to 0.5 m, jumps up to 2 m, links dropped
    /// when walking is within 3 times as far or their ends within 1 m of
    /// another's, a snap radius of 0.5 m, costs 2 and 4, kinds
    /// mnav_linkDrop and mnav_linkJump, and no clearance test.
    ///
    /// @return The def.
    /// @par Thread safety
    /// Safe from any thread.
    MNAV_API mnavLinkGenDef mnavDefaultLinkGenDef(void);

    /// Generates links from the edges of the polygons on a range of tile
    /// places that have nothing across them and are not tile sides. Each
    /// edge is sampled every `spacing` meters; a sample tries a drop,
    /// landing `2 * radius + 4 * cellSize` out from the edge on the highest
    /// surface between the agent's step height and `dropMax` below, and a
    /// jump, landing from `2 * radius` to `jumpMax` out at the nearest
    /// distance where a surface lies within the step height of the start,
    /// the radius, step height and cell size being the navmesh's. A link is
    /// kept when the clearance test, if any, passes it, when the navmesh
    /// does not already walk from its start to its landing within `detour`
    /// times the straight distance, and when no earlier link has both ends
    /// within `filterDistance` of its own. The order is tile place,
    /// polygon, edge, sample; the same navmesh gives the same links.
    ///
    /// @param query     A context for the walking searches; its last search
    ///                  ends.
    /// @param navmesh   The navmesh.
    /// @param filter    The areas links may start and land on, and walk
    ///                  through, or NULL.
    /// @param def       The def, from mnavDefaultLinkGenDef.
    /// @param tileX0    The first tile column.
    /// @param tileZ0    The first tile row.
    /// @param tileX1    The last tile column, at least tileX0.
    /// @param tileZ1    The last tile row, at least tileZ0.
    /// @param linksOut  Receives the links, up to capacity.
    /// @param capacity  The buffer's size in links, at least 0.
    /// @param countOut  Receives the number of links generated, also beyond
    ///                  the capacity.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, a
    /// def not from mnavDefaultLinkGenDef, a tile range backward, or a
    /// filter not built from mnavDefaultQueryFilter; `mnav_errorRange` for
    /// a def value or filter cost out of its range; `mnav_errorCapacity`
    /// when more links were generated than the buffer holds, the first
    /// capacity written.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time,
    /// and no commit runs on the navmesh.
    MNAV_NODISCARD MNAV_API mnavResult
    mnavGenerateLinks(mnavQuery* query, const mnavNavmesh* navmesh, const mnavQueryFilter* filter,
                      const mnavLinkGenDef* def, int32_t tileX0, int32_t tileZ0, int32_t tileX1,
                      int32_t tileZ1, mnavLinkDef* linksOut, int32_t capacity, int32_t* countOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_NAV_LINKGEN_H
