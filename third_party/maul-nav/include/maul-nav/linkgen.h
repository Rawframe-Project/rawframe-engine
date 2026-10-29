// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Link generation: off-mesh links for drops off ledges and
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
// The most polygons a landing's search box, a cell wide, may meet.
#define MNAV_MAX_LINK_LANDINGS 32

    // Decides whether the way between two points is free for the agent:
    // the host's collision test. True to keep the link. It is called only
    // within mnavGenerateLinks, on the thread that called it, before that
    // call returns; never afterward or from another thread. Its answer
    // decides the links, so it runs in the library's floating-point
    // environment: round to nearest, denormals kept. It may call what only
    // reads the navmesh, with a query context of its own, and
    // mnavStageTile and mnavStageTileRemoval, which change nothing
    // generation reads; while it runs, the navmesh's other stage calls,
    // commit and destroy and every call on the generating context are
    // refused with mnav_errorBusy, one misuse counted on that object.
    typedef bool (*mnavClearanceFn)(void* context, mnavPos3 from, mnavPos3 to);

    // How links are generated. Build it with mnavDefaultLinkGenDef.
    typedef struct mnavLinkGenDef
    {
        uint32_t cookie;
        // The distance between samples along an edge, in meters, at most
        // MNAV_MAX_LINK_REACH and at least the navmesh's cell size: samples
        // closer than a cell tell nothing a cell's resolution can.
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

// The cookie mnavDefaultLinkGenDef sets and mnavGenerateLinks checks.
#define MNAV_LINK_GEN_DEF_COOKIE 0x4E41564Cu

    /// Returns the default generation def: samples every 1 m, drops up to
    /// 3 m going both ways up to 0.5 m, jumps up to 2 m, links dropped
    /// when walking is within 3 times as far or their ends within 1 m of
    /// another's, a snap radius of 0.5 m, costs 2 and 4, kinds
    /// mnav_linkDrop and mnav_linkJump, and no clearance test. It carries
    /// only a cookie: mnavGenerateLinks also takes a navmesh and a query
    /// context, whose creations checked the version.
    ///
    /// @return The def.
    /// @par Thread safety
    /// Safe from any thread.
    static inline mnavLinkGenDef mnavDefaultLinkGenDef(void)
    {
#ifdef __cplusplus
        mnavLinkGenDef def = {};
#else
    mnavLinkGenDef def = {0};
#endif
        def.cookie = MNAV_LINK_GEN_DEF_COOKIE;
        def.spacing = 1.0f;
        def.dropMax = 3.0f;
        def.jumpMax = 2.0f;
        def.climbMax = 0.5f;
        def.detour = 3.0f;
        def.filterDistance = 1.0f;
        def.radius = 0.5f;
        def.dropCost = 2.0f;
        def.jumpCost = 4.0f;
        def.dropKind = mnav_linkDrop;
        def.jumpKind = mnav_linkJump;
        return def;
    }

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
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, a def
    /// not from mnavDefaultLinkGenDef, a def value NaN or below 0, a spacing of
    /// 0 or below the navmesh's cell size, a detour other than 0 below 1 or
    /// past 10^6, a kind of MNAV_LINK_KINDS or more, a tile range backward, or
    /// a filter not built from mnavDefaultQueryFilter or with a NaN cost;
    /// `mnav_errorLimit` for a def value past its MNAV_MAX_ bound, an infinity
    /// included, or a filter cost past MNAV_MIN_AREA_COST or
    /// MNAV_MAX_AREA_COST, an infinity included; `mnav_errorCapacity` when more
    /// links were generated than the buffer holds, the first capacity written;
    /// `mnav_errorLimit` when generation stops, the links found so far counted
    /// and the first capacity written: at a link past MNAV_MAX_LINKS, the most
    /// a navmesh holds, or at a landing's search box meeting more than
    /// MNAV_MAX_LINK_LANDINGS polygons. Also `mnav_errorBusy` when called back
    /// in from host code a call using the context runs (a link generation's
    /// clearance test, a hierarchy's allocator as it builds), one misuse
    /// counted.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time,
    /// and no commit runs on the navmesh. The def's clearance test runs on
    /// the calling thread, within the call; while it runs, the navmesh's
    /// stage calls but mnavStageTile and mnavStageTileRemoval are refused
    /// with `mnav_errorBusy` from any thread.
    MNAV_NODISCARD MNAV_API mnavResult
    mnavGenerateLinks(mnavQuery* query, const mnavNavmesh* navmesh, const mnavQueryFilter* filter,
                      const mnavLinkGenDef* def, int32_t tileX0, int32_t tileZ0, int32_t tileX1,
                      int32_t tileZ1, mnavLinkDef* linksOut, int32_t capacity, int32_t* countOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_NAV_LINKGEN_H
