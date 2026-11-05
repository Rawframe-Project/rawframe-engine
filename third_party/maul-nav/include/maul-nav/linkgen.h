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
    // within mnavQuery_GenerateLinks, on the thread that called it, before that
    // call returns; never afterward or from another thread. Its answer
    // decides the links, so it runs in the library's floating-point
    // environment: round to nearest, denormals kept. It may call what only
    // reads the navmesh, with a query context of its own, and
    // mnavNavmesh_StageTile and mnavNavmesh_StageTileRemoval, which change nothing
    // generation reads; while it runs, the navmesh's other stage calls,
    // commit and destroy and every call on the generating context are
    // refused with mnav_errorBusy, one misuse counted on that object.
    typedef bool mnavClearanceFunction(void* context, mnavPos3 from, mnavPos3 to);

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
        mnavClearanceFunction* clearance;
        void* context;
    } mnavLinkGenDef;

// The cookie mnavDefaultLinkGenDef sets and mnavQuery_GenerateLinks checks.
#define MNAV_LINK_GEN_DEF_COOKIE 0x4E41564Cu

    /// Returns the default generation def: samples every 1 m, drops up to
    /// 3 m going both ways up to 0.5 m, jumps up to 2 m, links dropped
    /// when walking is within 3 times as far or their ends within 1 m of
    /// another's, a snap radius of 0.5 m, costs 2 and 4, kinds
    /// mnav_linkDrop and mnav_linkJump, and no clearance test. It carries
    /// only a cookie: mnavQuery_GenerateLinks also takes a navmesh and a query
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
    /// the radius, step height and cell size being the navmesh's. Neither
    /// lands on the polygon it starts from. A link is kept when the
    /// clearance test, if any, passes it, when the navmesh does not already
    /// walk from its start to its landing within `detour` times the
    /// straight distance, and when no earlier link in the buffer has both
    /// ends within `filterDistance` of its own. The order is tile
    /// place, polygon, edge, sample; the same navmesh gives the same links.
    /// A buffer too short holds the same first links as a longer one, but
    /// links past it are not filtered against each other, since the
    /// context holds no more than the buffer does: the count may then
    /// exceed what a long enough buffer keeps, and is always room enough.
    ///
    /// @param query     A context for the walking searches; its last search
    ///                  ends.
    /// @param navmesh   The navmesh.
    /// @param def       The def, from mnavDefaultLinkGenDef.
    /// @param tiles     The tile places whose edges are sampled, or NULL for
    ///                  every tile.
    /// @param filter    The areas links may start and land on, and walk
    ///                  through, or NULL.
    /// @param linksOut  Receives the links, up to capacity. Zeroed over the
    ///                  capacity on any failure but those that keep the links
    ///                  found, as countOut lists them; mnav_errorBusy leaves it
    ///                  as it was.
    /// @param capacity  The buffer's size in links, at least 0.
    /// @param countOut  Receives the number of links generated, also beyond the
    ///                  capacity, where it is room enough for a second call
    ///                  and may be more than that call keeps. Zeroed on any
    ///                  failure but those that count the links found:
    ///                  mnav_errorCapacity, and mnav_errorLimit when generation
    ///                  stops at a bound; mnav_errorBusy leaves it as it was.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, a def
    /// not from mnavDefaultLinkGenDef, a def value NaN or below 0, a spacing of
    /// 0 or below the navmesh's cell size, a detour other than 0 below 1 or
    /// past 10^6, a negative kind or one of MNAV_LINK_KINDS or more, a tile
    /// range backward, or a filter not built from mnavDefaultQueryFilter or
    /// with a NaN cost; `mnav_errorLimit` for a def value past its MNAV_MAX_
    /// bound, an infinity included, or a filter cost past MNAV_MIN_AREA_COST or
    /// MNAV_MAX_AREA_COST, an infinity included; `mnav_errorCapacity` when more
    /// links were generated than the buffer holds, the first capacity written
    /// and the count room enough for every link, generation stopping once the
    /// count reaches MNAV_MAX_LINKS, which is then the count; `mnav_errorLimit`
    /// when generation stops, the links found so far counted and the first
    /// capacity written: at a link past MNAV_MAX_LINKS, the most a navmesh
    /// holds, in a buffer holding that many, or at a landing's search box
    /// meeting more than MNAV_MAX_LINK_LANDINGS polygons. A call that measures
    /// with a short buffer is refused with mnav_errorLimit only where one with
    /// room for the count would be too. Also `mnav_errorBusy` when called back
    /// in from host code a call using the context runs (a link generation's
    /// clearance test, a hierarchy's allocator as it builds), one misuse
    /// counted.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time,
    /// and no commit runs on the navmesh. The def's clearance test runs on
    /// the calling thread, within the call; while it runs, the navmesh's
    /// stage calls but mnavNavmesh_StageTile and mnavNavmesh_StageTileRemoval are refused
    /// with `mnav_errorBusy` from any thread.
    MNAV_NODISCARD MNAV_API mnavResult
    mnavQuery_GenerateLinks(mnavQuery* query, const mnavNavmesh* navmesh, const mnavLinkGenDef* def,
                            const mnavTileRange* tiles, const mnavQueryFilter* filter,
                            mnavLinkDef* linksOut, int32_t capacity, int32_t* countOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_NAV_LINKGEN_H
