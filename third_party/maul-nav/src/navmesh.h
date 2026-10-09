// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The navmesh's insides (mnav-0004), for the modules that query it: tiles in
// slots, their links across tile sides, and the sorted index of places.

#ifndef MAUL_NAV_SRC_NAVMESH_H
#define MAUL_NAV_SRC_NAVMESH_H

#include "allocator.h"
#include "detail.h"
#include "polymesh.h"
#include "tile.h"

#include "maul-nav/bake.h"
#include "maul-nav/navmesh.h"

#include <stdint.h>

// A link from a polygon edge on a tile side to a polygon across it, over
// the part of the side from low to high, in cells along the side.
typedef struct mnavLink
{
    uint16_t polygon;
    uint8_t edge;
    uint8_t side;
    int32_t low;
    int32_t high;
    mnavPolygonId target;
} mnavLink;

// A tile as loaded, and its links, polygon by polygon: polygon p's are
// links[firstLink[p]] up to links[firstLink[p + 1]].
typedef struct mnavTile
{
    mnavTileInfo info;
    mnavPolyMesh mesh;
    mnavDetailMesh detail;
    mnavLink* links;
    int32_t linkCount;
    int32_t* firstLink;
} mnavTile;

// A staged change of a polygon's area (mnav-0004).
typedef struct mnavAreaChange
{
    mnavPolygonId polygon;
    mnavAreaType area;
} mnavAreaChange;

// A slot: its generation, its tile (NULL when free) and the tile's place.
// A retired slot is never used again.
typedef struct mnavSlot
{
    uint32_t generation;
    bool retired;
    int32_t x;
    int32_t z;
    mnavTile* tile;
} mnavSlot;

// A place and the 0-based slot holding its tile.
typedef struct mnavPlace
{
    int32_t x;
    int32_t z;
    int32_t slot;
} mnavPlace;

// A staged change: a tile to install at its place, or NULL to remove.
typedef struct mnavStaged
{
    int32_t x;
    int32_t z;
    mnavTile* tile;
} mnavStaged;

// Where an off-mesh link stands: a free slot, added at the next commit,
// committed, or removed at the next commit.
typedef uint8_t mnavLinkPhase;

enum
{
    MNAV_LINK_FREE = 0,
    MNAV_LINK_ADDING = 1,
    MNAV_LINK_LIVE = 2,
    MNAV_LINK_REMOVING = 3,
};

// An off-mesh link's slot: one crossing of a link, a point link having
// one; its def, its generation (0 before first use) and, once committed,
// where its ends snapped.
typedef struct mnavOffLink
{
    mnavLinkDef def;
    uint32_t generation;
    mnavLinkPhase phase;
    // Whether it is to be enabled at the next commit.
    bool enabled;
    mnavLinkState state;
    // The slot of the link's first crossing, which its id names, this one
    // for a point link; the crossing's place along the width, and in the
    // first, how many there are.
    int32_t parent;
    int32_t crossing;
    int32_t crossings;
} mnavOffLink;

struct mnavNavmesh
{
    mnavBakeDef def;
    mnavBakeCells cells;
    mnavMemory memory;
    mnavSlot* slots;
    int32_t slotCount;
    int32_t slotCapacity;
    // The committed tiles' places, sorted by x, then z.
    mnavPlace* places;
    int32_t placeCount;
    int32_t placeCapacity;
    mnavStaged* staged;
    int32_t stagedCount;
    int32_t stagedCapacity;
    // Off-mesh links by slot, the slots not free, and the links staged
    // to be added or removed.
    mnavOffLink* links;
    int32_t linkSlots;
    int32_t linkCapacity;
    int32_t linksHeld;
    int32_t linksPending;
    // Every attached link from each polygon it leaves, as keys sorted by
    // slot, polygon, link and direction (offmesh.h).
    uint64_t* attachments;
    int32_t attachmentCount;
    int32_t attachmentCapacity;
    // The same attachments seen from the polygon each lands on: keys of
    // the landing polygon's slot and index, the link and the direction,
    // sorted, for searches run backward.
    uint64_t* arrivals;
    int32_t arrivalCount;
    int32_t arrivalCapacity;
    // The areas staged to change, in staging order.
    mnavAreaChange* areaChanges;
    int32_t areaChangeCount;
    int32_t areaChangeCapacity;
    // For each link kind, the lowest cost per meter of its attached links'
    // spans, INFINITY when it has none: what scales the search's
    // heuristic (mnav-0005).
    double costPerMeter[MNAV_LINK_KINDS];
    // The commits that changed something, so a search can tell the
    // navmesh it began on from a later one.
    uint64_t commits;
};

// The committed tile at a place, or NULL; slotOut receives its 0-based
// slot.
const mnavTile* mnavTileAt(const mnavNavmesh* navmesh, int32_t x, int32_t z, int32_t* slotOut);

// The polygon an id names, or NULL when the id is stale or out of range;
// tileOut receives its tile.
const mnavPolygon* mnavPolygonOf(const mnavNavmesh* navmesh, mnavPolygonId id,
                                 const mnavTile** tileOut);

// The place one step from (x, z) across a side, 1 to 4 for -X, +Z, +X
// and -Z, and the side facing back.
void mnavAcross(int32_t side, int32_t* x, int32_t* z, int32_t* facing);

// A tile's frame: its cell (0, 0) corner in world meters and its cells'
// sizes.
typedef struct mnavFrame
{
    double x0;
    double z0;
    double y0;
    double cell;
    double height;
} mnavFrame;

mnavFrame mnavFrameOf(const mnavNavmesh* navmesh, int32_t x, int32_t z);

// Whether a polygon id names a polygon of the navmesh now:
// mnav_errorInvalid for one never handed out, mnav_errorStale for one whose
// tile was replaced or removed.
mnavResult mnavCheckPolygon(const mnavNavmesh* navmesh, mnavPolygonId id);

// A mesh vertex's world position in a tile's frame. Inline: the searches
// call it for every portal they open.
static inline mnavPos3 mnavVertexWorld(const mnavFrame* f, const mnavMeshVertex* v)
{
    return (mnavPos3){f->x0 + v->x * f->cell, f->y0 + (v->y - MNAV_HEIGHT_OFFSET) * f->height,
                      f->z0 + v->z * f->cell};
}

// New links for one tile, applied when the commit succeeds.
typedef struct mnavRelink
{
    int32_t slot;
    mnavLink* links;
    int32_t linkCount;
    int32_t* firstLink;
} mnavRelink;

// The navmesh after the commit, built beside it: slots, places, and the
// links of every tile next to a change.
typedef struct mnavTilePlan
{
    mnavSlot* slots;
    int32_t slotCount;
    int32_t slotCapacity;
    mnavPlace* places;
    int32_t placeCount;
    int32_t placeCapacity;
    mnavRelink* relinks;
    int32_t relinkCount;
    int32_t relinkCapacity;
} mnavTilePlan;

// Works out the commit of the staged tiles beside the navmesh; nothing
// changes. On failure the plan holds nothing.
mnavResult mnavPlanTiles(mnavNavmesh* navmesh, mnavTilePlan* plan);

// Swaps a plan in; nothing can fail.
void mnavApplyTiles(mnavNavmesh* navmesh, mnavTilePlan* plan);

// Releases a plan not applied.
void mnavDropTilePlan(mnavNavmesh* navmesh, mnavTilePlan* plan);

#endif // MAUL_NAV_SRC_NAVMESH_H
