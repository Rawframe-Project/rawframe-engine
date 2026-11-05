// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The navmesh queries read: tiles loaded from baked bytes, staged and
// committed together, their polygons named by generation-checked ids and
// linked across the tiles' shared sides.

#ifndef MAUL_NAV_NAVMESH_H
#define MAUL_NAV_NAVMESH_H

#include "maul-nav/bake.h"
#include "maul-nav/base.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The largest values a navmesh's limits may take.
#define MNAV_MAX_TILES      1048576
#define MNAV_MAX_TILE_LINKS 1048576
#define MNAV_MAX_LINKS      1048576

    // A navmesh. Made by mnavCreateNavmesh.
    typedef struct mnavNavmesh mnavNavmesh;

    // A navmesh's runtime tier: what may change once loaded.
    // Tiles and off-mesh links load and unload in every tier.
    typedef int32_t mnavTier;

    enum
    {
        // Nothing loaded changes.
        mnav_tierStatic = 0,
        // Polygons' areas change, and off-mesh links are enabled and
        // disabled.
        mnav_tierModifiers = 1,
        // Also, a loaded tile is replaced in one commit.
        mnav_tierDynamic = 2,
    };

    // The named limits of a navmesh. Each count is at least 1 and at most
    // its MNAV_MAX_ value. Reaching one is mnav_errorCapacity.
    typedef struct mnavNavmeshLimits
    {
        // Tiles committed at once.
        int32_t tiles;
        // Links one tile has to its own polygons and its neighbors', made
        // as it is committed.
        int32_t tileLinks;
        // Off-mesh links, staged or committed, counted by crossings: a
        // point link takes 1, an edge link 1 + ceil(width / spacing) up to
        // MNAV_MAX_LINK_CROSSINGS (see mnavLinkDef.width).
        int32_t links;
        // Bytes the navmesh may hold allocated at once, at least 1.
        uint64_t memoryBytes;
    } mnavNavmeshLimits;

    // How a navmesh is made. Build it with mnavDefaultNavmeshDef.
    typedef struct mnavNavmeshDef
    {
        uint32_t cookie;
        // MNAV_ABI_VERSION, set by mnavDefaultNavmeshDef.
        uint32_t version;
        // The allocator the navmesh uses; zeroed for the C library's.
        mnavAllocator allocator;
        // The def the navmesh's tiles are baked with, checked whole as a
        // baker checks it. The navmesh reads only what a tile's header
        // records of it, the origin, the cell size and height, the tile
        // cells and the agent, and refuses a tile baked with other values;
        // its allocator, limits and clock are the bakers'.
        mnavBakeDef bake;
        // What may change once loaded.
        mnavTier tier;
        mnavNavmeshLimits limits;
    } mnavNavmeshDef;

    // The setting a navmesh def check refused.
    typedef int32_t mnavNavmeshSetting;

    enum
    {
        mnav_navmeshSettingNone = 0,
        mnav_navmeshSettingCookie = 1,
        mnav_navmeshSettingAllocator = 2,
        // The bake def, its own setting named beside.
        mnav_navmeshSettingBake = 3,
        mnav_navmeshSettingTier = 4,
        mnav_navmeshSettingTiles = 5,
        mnav_navmeshSettingTileLinks = 6,
        mnav_navmeshSettingLinks = 7,
        mnav_navmeshSettingMemoryBytes = 8,
    };

    // A navmesh def check's outcome: the status, the first setting it
    // refused, and for mnav_navmeshSettingBake, the bake def's setting
    // (mnav_bakeSettingNone otherwise).
    typedef struct mnavNavmeshDefResult
    {
        mnavResult result;
        mnavNavmeshSetting setting;
        mnavBakeSetting bakeSetting;
    } mnavNavmeshDefResult;

// The cookie mnavDefaultNavmeshDef sets and mnavCreateNavmesh checks.
#define MNAV_NAVMESH_DEF_COOKIE 0x4E41564Du

    /// Returns the default navmesh def: tiles of the default bake def, the
    /// static tier, and limits sized for a large level: 65536 tiles, 32768
    /// links in a tile, 4096 off-mesh links and 256 MiB.
    /// Built in the program, so that it carries the version of the
    /// headers the program includes.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    static inline mnavNavmeshDef mnavDefaultNavmeshDef(void)
    {
#ifdef __cplusplus
        mnavNavmeshDef def = {};
#else
    mnavNavmeshDef def = {0};
#endif
        def.cookie = MNAV_NAVMESH_DEF_COOKIE;
        def.version = MNAV_ABI_VERSION;
        def.bake = mnavDefaultBakeDef();
        def.tier = mnav_tierStatic;
        def.limits.tiles = 65536;
        def.limits.tileLinks = 32768;
        def.limits.links = 4096;
        def.limits.memoryBytes = 268435456;
        return def;
    }

    // A tile in a navmesh: its 1-based slot (0 for none) and the slot's
    // generation, which changes whenever the slot's tile does.
    typedef struct mnavTileId
    {
        uint32_t slot;
        uint32_t generation;
    } mnavTileId;

    // A polygon: its tile's slot and generation and its index there.
    typedef struct mnavPolygonId
    {
        uint32_t slot;
        uint32_t generation;
        uint32_t polygon;
    } mnavPolygonId;

    // A box of tile places, from (x0, z0) to (x1, z1), both included.
    typedef struct mnavTileRange
    {
        int32_t x0;
        int32_t z0;
        int32_t x1;
        int32_t z1;
    } mnavTileRange;

    // The part of tile bytes a load refused.
    typedef int32_t mnavTileSection;

    enum
    {
        mnav_tileHeader = 0,
        mnav_tileVertices = 1,
        mnav_tilePolygons = 2,
        mnav_tileDetailParts = 3,
        mnav_tileDetailVertices = 4,
        mnav_tileDetailTriangles = 5,
        // The payload's size or hash.
        mnav_tilePayload = 6,
        // A flight tile's cube classes, nodes and leaves.
        mnav_tileFlightRoots = 7,
        mnav_tileFlightNodes = 8,
        mnav_tileFlightLeaves = 9,
    };

    // A tile load's outcome: the status, and the section and element
    // refused (-1 for the section as a whole).
    typedef struct mnavTileResult
    {
        mnavResult result;
        mnavTileSection section;
        int32_t index;
    } mnavTileResult;

    /// Makes an empty navmesh from a def: checks the def and keeps a copy,
    /// its allocator and its limits.
    ///
    /// @param def          The def, from mnavDefaultNavmeshDef.
    /// @param navmeshOut   Receives the navmesh, or NULL on failure.
    /// @return `mnav_success`; `mnav_errorInvalid` with the setting for a NULL
    /// def, a def without its cookie, an allocator with one function, a
    /// negative tier or one past mnav_tierDynamic, a limit below 1;
    /// `mnav_errorLimit` with the setting for a limit past its MNAV_MAX_ value;
    /// `mnav_errorVersion` with no setting for a def built against another
    /// version of the headers; for a bake def mnavValidateBakeDef refuses, the
    /// status it gives with mnav_navmeshSettingBake and its setting;
    /// `mnav_errorInvalid` for a NULL navmeshOut; `mnav_errorCapacity` when the
    /// navmesh does not fit the def's memory limit or the allocator fails.
    /// @par Thread safety
    /// Safe from any thread.
    MNAV_NODISCARD MNAV_API mnavNavmeshDefResult mnavCreateNavmesh(const mnavNavmeshDef* def,
                                                                   mnavNavmesh** navmeshOut);

    /// Destroys a navmesh, its tiles and everything staged. Called back in from
    /// host code a call on the navmesh runs (its allocator, a link generation's
    /// clearance test, a hierarchy's allocator as it builds on it), it does
    /// nothing and counts one misuse.
    ///
    /// @param navmesh  The navmesh, or NULL.
    /// @par Thread safety
    /// Safe from any thread; the navmesh is used by one thread at a time.
    MNAV_API void mnavDestroyNavmesh(mnavNavmesh* navmesh);

    /// Loads tile bytes, checking every field as hostile input and the
    /// tile's settings against the navmesh's bake def, and stages the tile for
    /// the next commit, in place of anything staged at its place before.
    /// Queries do not see it until the commit.
    ///
    /// @param navmesh  The navmesh.
    /// @param bytes    The tile's bytes, as mnavBaker_CopyTile gives them.
    /// @param size     Their size in bytes.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument;
    /// `mnav_errorFormat` naming the section and element for malformed bytes,
    /// or a tile baked with other settings (the header); `mnav_errorVersion`
    /// for another tile format version; `mnav_errorCapacity` past the memory
    /// limit or when the allocator fails; `mnav_errorUnsupported` for a tile at
    /// a place where one is committed, below mnav_tierDynamic. Also
    /// `mnav_errorBusy` when called back in from the navmesh's allocator, one
    /// misuse counted; a link generation's clearance test, or a hierarchy's
    /// allocator as it builds on the navmesh, may call it.
    /// @par Thread safety
    /// Safe from any thread; the navmesh is used by one thread at a time.
    /// Staging changes nothing queries read, but it may not run beside
    /// another call that stages or commits.
    MNAV_NODISCARD MNAV_API mnavTileResult mnavNavmesh_StageTile(mnavNavmesh* navmesh,
                                                                 const uint8_t* bytes, size_t size);

    /// Stages the removal of the tile at a place for the next commit, in
    /// place of anything staged there before.
    ///
    /// @param navmesh  The navmesh.
    /// @param tileX    The tile's column.
    /// @param tileZ    The tile's row.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL navmesh;
    /// `mnav_errorCapacity` past the memory limit or when the allocator fails.
    /// Also `mnav_errorBusy` when called back in from the navmesh's allocator,
    /// one misuse counted; a link generation's clearance test, or a hierarchy's
    /// allocator as it builds on the navmesh, may call it.
    /// @par Thread safety
    /// Safe from any thread; the navmesh is used by one thread at a time.
    /// Staging changes nothing queries read, but it may not run beside
    /// another call that stages or commits.
    MNAV_NODISCARD MNAV_API mnavResult mnavNavmesh_StageTileRemoval(mnavNavmesh* navmesh,
                                                                    int32_t tileX, int32_t tileZ);

    /// Applies everything staged at once: installs and removes the tiles
    /// and links every polygon on a changed tile's sides to the polygons
    /// across them, adds and removes off-mesh links, and snaps every
    /// off-mesh link's ends again. Either all of it applies or, on
    /// failure, nothing does and the staged changes stay. A replaced or
    /// removed tile's ids become stale, and so do removed links' ids.
    ///
    /// @param navmesh  The navmesh.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL navmesh;
    /// `mnav_errorCapacity` when the tiles would pass the tiles limit, a tile
    /// its tileLinks limit, or memory its limit, or when the allocator fails.
    /// Also `mnav_errorBusy` when called back in from host code a call on the
    /// navmesh runs (its allocator, a link generation's clearance test, a
    /// hierarchy's allocator as it builds on it), one misuse counted.
    /// @par Thread safety
    /// Safe from any thread; the navmesh is used by one thread at a time,
    /// so no query may run during a commit.
    MNAV_NODISCARD MNAV_API mnavResult mnavNavmesh_Commit(mnavNavmesh* navmesh);

    /// Reads a navmesh's runtime tier.
    ///
    /// @param navmesh The navmesh.
    /// @return Its tier; mnav_tierStatic for NULL.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read it and query
    /// at once between commits; none may while a stage or commit call
    /// runs.
    MNAV_API mnavTier mnavNavmesh_GetTier(const mnavNavmesh* navmesh);

    /// Returns how many calls against the navmesh were refused as misuse since
    /// it was made: an argument refused with `mnav_errorInvalid` when the
    /// navmesh was the first object the call was given, and a call back in from
    /// host code it ran (`mnav_errorBusy`).
    ///
    /// @param navmesh The navmesh.
    /// @return The count; 0 for NULL.
    /// @par Thread safety
    /// Safe from any thread. It may be read at any time, also while host
    /// code the object runs calls back in.
    MNAV_API uint64_t mnavNavmesh_GetMisuse(const mnavNavmesh* navmesh);

    /// Stages a change of a polygon's area, applied at the next commit;
    /// mnav_areaNone blocks it. The change lasts while the polygon's tile
    /// is loaded, and is dropped when the commit replaces or removes it.
    /// Staged again before the commit, the last area counts.
    ///
    /// @param navmesh The navmesh, of mnav_tierModifiers or above.
    /// @param polygon The polygon.
    /// @param area    Its new area, below MNAV_AREA_TYPES.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL navmesh, an area
    /// out of range or an id never handed out; `mnav_errorStale` for a polygon
    /// whose tile has been replaced or removed; `mnav_errorUnsupported` for a
    /// static navmesh; `mnav_errorCapacity` when memory runs out. Also
    /// `mnav_errorBusy` when called back in from host code a call on the
    /// navmesh runs (its allocator, a link generation's clearance test, a
    /// hierarchy's allocator as it builds on it), one misuse counted.
    /// @par Thread safety
    /// Safe from any thread; the navmesh is used by one thread at a time,
    /// so no query may run during a stage call.
    MNAV_NODISCARD MNAV_API mnavResult mnavPolygon_StageArea(mnavNavmesh* navmesh,
                                                             mnavPolygonId polygon,
                                                             mnavAreaType area);

    /// Reads a polygon's area as committed.
    ///
    /// @param navmesh The navmesh.
    /// @param polygon The polygon.
    /// @param areaOut Receives its area. Zeroed on any failure.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument or
    /// an id never handed out; `mnav_errorStale` for a polygon whose tile
    /// has been replaced or removed.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read areas and
    /// query at once between commits; none may while a stage or commit
    /// call runs.
    MNAV_NODISCARD MNAV_API mnavResult mnavPolygon_GetArea(const mnavNavmesh* navmesh,
                                                           mnavPolygonId polygon,
                                                           mnavAreaType* areaOut);

    /// Finds the tile at a place.
    ///
    /// @param navmesh  The navmesh.
    /// @param tileX    The tile's column.
    /// @param tileZ    The tile's row.
    /// @param tileOut  Receives the tile's id, or a zeroed id. Zeroed on any
    ///                 failure.
    /// @return `mnav_success`; `mnav_errorNotLoaded` when no tile is
    /// committed there; `mnav_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may find tiles and
    /// query at once between commits; none may while a stage or commit
    /// call runs.
    MNAV_NODISCARD MNAV_API mnavResult mnavNavmesh_GetTile(const mnavNavmesh* navmesh,
                                                           int32_t tileX, int32_t tileZ,
                                                           mnavTileId* tileOut);

// The number of off-mesh link kinds.
#define MNAV_LINK_KINDS 64
// The farthest an off-mesh link's end may snap, in meters.
#define MNAV_MAX_LINK_RADIUS 100.0f

// The widest an edge link may be, in meters, and the most points it is
// crossed at.
#define MNAV_MAX_LINK_WIDTH     100.0f
#define MNAV_MAX_LINK_CROSSINGS 64
// The dearest off-mesh link, in the filter's cost units.
#define MNAV_MAX_LINK_COST 1.0e9f

    // How an agent traverses an off-mesh link: kinds 0 to 5 are the
    // library's, 6 to MNAV_LINK_KINDS - 1 the host's to name.
    typedef int32_t mnavLinkKind;

    enum
    {
        mnav_linkJump = 0,
        mnav_linkDrop = 1,
        mnav_linkClimb = 2,
        mnav_linkLadder = 3,
        mnav_linkDoor = 4,
        mnav_linkTeleport = 5,
        // The first kind the host names.
        mnav_linkHostKinds = 6,
    };

    // An off-mesh link: a way from one point to another that the ground
    // does not make.
    typedef struct mnavLinkDef
    {
        mnavPos3 start;
        mnavPos3 end;
        // How far on the ground each end may lie from the polygon it
        // snaps to, in meters, 0 to MNAV_MAX_LINK_RADIUS.
        float radius;
        // What crossing it costs, in the filter's cost units, 0 to
        // MNAV_MAX_LINK_COST.
        float cost;
        mnavLinkKind kind;
        // Whether agents may cross it from its end to its start too.
        bool twoWay;
        // 0 for a point link; otherwise an edge link: the start
        // and end are the centers of two edges this wide, in meters, across
        // the link's direction on the ground, up to MNAV_MAX_LINK_WIDTH.
        // Agents cross it at points spaced along the width, no farther
        // apart than the agent's radius (the cell size when the radius is
        // 0): 1 + ceil(width / spacing) crossings, at most
        // MNAV_MAX_LINK_CROSSINGS. Each crossing takes one of the navmesh's
        // links (mnavNavmeshLimits.links), so a link 16 m wide for agents of
        // 0.5 m takes 33; size the limit by crossings, not by links.
        float width;
    } mnavLinkDef;

    // An off-mesh link in a navmesh: its 1-based slot and the slot's
    // generation.
    typedef struct mnavLinkId
    {
        uint32_t slot;
        uint32_t generation;
    } mnavLinkId;

    // An off-mesh link as committed.
    typedef struct mnavLinkState
    {
        // Whether both ends snapped to polygons at the last commit; a
        // detached link is not crossed.
        bool attached;
        // Whether the link may be crossed; a disabled link is still
        // snapped and reported.
        bool enabled;
        // The polygons the ends snapped to and the points on them; zeroed
        // when detached.
        mnavPolygonId startPolygon;
        mnavPolygonId endPolygon;
        mnavPos3 start;
        mnavPos3 end;
        // How many of its crossings are attached: 1 for an attached point
        // link; for an edge link, those whose ends both snapped, the
        // polygons and points above being the first of them along the
        // width.
        int32_t crossingCount;
    } mnavLinkState;

    /// Stages an off-mesh link; it is added at the next commit. Its ends
    /// snap, at every commit, to the nearest polygon within its radius on
    /// the ground and the agent's step in height.
    ///
    /// @param navmesh  The navmesh.
    /// @param def      The link.
    /// @param linkOut  Receives its id, usable once it is committed. Zeroed on any
    ///                 failure.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, a point
    /// with a NaN coordinate, or an edge link whose ends lie at one place on
    /// the ground or so near that the square of their distance rounds to 0, a
    /// radius, cost or width below 0 or NaN, or a negative kind or one of
    /// MNAV_LINK_KINDS or more; `mnav_errorLimit` for a point farther from the
    /// def's origin than bake input may lie (MNAV_MAX_EXTENT_CELLS cells on the
    /// ground, MNAV_MAX_HEIGHT_CELLS cell heights up or down), a radius, cost
    /// or width past its MNAV_MAX_ bound, an infinity included, or when a
    /// crossing would need a slot from MNAV_MAX_LINKS on (left only by slots
    /// worn out by 2^32 links each); `mnav_errorCapacity` when the links'
    /// crossings, staged and committed, would pass the links limit, or memory
    /// its limit, or when the allocator fails. A refused link is refused whole,
    /// none of its crossings taken. Also `mnav_errorBusy` when called back in
    /// from host code a call on the navmesh runs (its allocator, a link
    /// generation's clearance test, a hierarchy's allocator as it builds on
    /// it), one misuse counted.
    /// @par Thread safety
    /// Safe from any thread; the navmesh is used by one thread at a time,
    /// so no query may run while a link is staged.
    MNAV_NODISCARD MNAV_API mnavResult mnavStageLink(mnavNavmesh* navmesh, const mnavLinkDef* def,
                                                     mnavLinkId* linkOut);

    /// Stages an off-mesh link's removal; it goes at the next commit. A link
    /// staged and not yet committed goes at once.
    ///
    /// @param navmesh  The navmesh.
    /// @param link     The link.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL navmesh or an id
    /// never handed out; `mnav_errorStale` for a link already removed. Also
    /// `mnav_errorBusy` when called back in from host code a call on the
    /// navmesh runs (its allocator, a link generation's clearance test, a
    /// hierarchy's allocator as it builds on it), one misuse counted.
    /// @par Thread safety
    /// Safe from any thread; the navmesh is used by one thread at a time,
    /// so no query may run while a removal is staged.
    MNAV_NODISCARD MNAV_API mnavResult mnavStageLinkRemoval(mnavNavmesh* navmesh, mnavLinkId link);

    /// Stages enabling or disabling an off-mesh link, applied at the next
    /// commit; a link is enabled when added. A disabled link stays snapped
    /// but is not crossed.
    ///
    /// @param navmesh The navmesh, of mnav_tierModifiers or above.
    /// @param link    The link, committed or staged.
    /// @param enabled Whether it may be crossed.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL navmesh or an id
    /// never handed out; `mnav_errorStale` for a link removed or staged for
    /// removal; `mnav_errorUnsupported` for a static navmesh. Also
    /// `mnav_errorBusy` when called back in from host code a call on the
    /// navmesh runs (its allocator, a link generation's clearance test, a
    /// hierarchy's allocator as it builds on it), one misuse counted.
    /// @par Thread safety
    /// Safe from any thread; the navmesh is used by one thread at a time,
    /// so no query may run during a stage call.
    MNAV_NODISCARD MNAV_API mnavResult mnavLink_StageEnable(mnavNavmesh* navmesh, mnavLinkId link,
                                                            bool enabled);

    /// Reads whether an off-mesh link may be crossed, as committed: a
    /// toggle mnavLink_StageEnable stages shows only after the next commit.
    ///
    /// @param navmesh    The navmesh.
    /// @param link       The link.
    /// @param enabledOut Receives whether it is enabled. False on any
    ///                   failure.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument or an
    /// id never handed out; `mnav_errorState` for a link staged and not yet
    /// committed; `mnav_errorStale` for a link removed.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read links and
    /// query at once between commits; none may while a stage or commit
    /// call runs.
    MNAV_NODISCARD MNAV_API mnavResult mnavLink_IsEnabled(const mnavNavmesh* navmesh,
                                                          mnavLinkId link, bool* enabledOut);

    /// Reads an off-mesh link as committed.
    ///
    /// @param navmesh  The navmesh.
    /// @param link     The link.
    /// @param stateOut Receives its state. Zeroed on any failure.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument or an
    /// id never handed out; `mnav_errorState` for a link staged and not yet
    /// committed; `mnav_errorStale` for a link removed.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read links and
    /// query at once between commits; none may while a stage or commit
    /// call runs.
    MNAV_NODISCARD MNAV_API mnavResult mnavLink_GetState(const mnavNavmesh* navmesh,
                                                         mnavLinkId link, mnavLinkState* stateOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_NAV_NAVMESH_H
