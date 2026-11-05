// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Path corridors: a caller's buffer of polygons from an agent
// to its target, checked and pulled tight.

#include "fpenv.h"
#include "funnel.h"
#include "invariant.h"
#include "navmesh.h"
#include "nearest.h"
#include "offmesh.h"
#include "outputs.h"
#include "polymesh.h"
#include "query.h"
#include "query_filter.h"

#include "maul-nav/base.h"
#include "maul-nav/navmesh.h"
#include "maul-nav/query.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

MNAV_NODISCARD static bool Finite(mnavPos3 p)
{
    return isfinite(p.x) && isfinite(p.y) && isfinite(p.z);
}

MNAV_NODISCARD static bool SameId(mnavPolygonId a, mnavPolygonId b)
{
    return a.slot == b.slot && a.generation == b.generation && a.polygon == b.polygon;
}

// Whether a corridor is well formed: finite ends, a buffer, room for a
// polygon at least, and from one polygon to its room. Its fields are the
// caller's, so every call checks them before it reads the buffer.
MNAV_NODISCARD static bool WellFormed(const mnavCorridor* corridor)
{
    return corridor != nullptr && Finite(corridor->position) && Finite(corridor->target) &&
           corridor->polygons != nullptr && corridor->capacity >= 1 &&
           corridor->polygonCount >= 1 && corridor->polygonCount <= corridor->capacity;
}

// Whether a well formed corridor's ends lie within MNAV_MAX_QUERY_DISTANCE
// of the navmesh's origin: every call that takes a navmesh refuses one
// that does not with mnav_errorLimit. A NULL navmesh passes here, for the
// call to refuse as invalid.
MNAV_NODISCARD static bool InReach(const mnavNavmesh* navmesh, const mnavCorridor* corridor)
{
    return navmesh == nullptr || (mnavInQueryReach(navmesh, corridor->position) &&
                                  mnavInQueryReach(navmesh, corridor->target));
}

static MNAV_NOINLINE mnavResult ResetCorridorWork(mnavCorridor* corridor, mnavPolygonId* buffer,
                                                  int32_t capacity, mnavPolygonId polygon,
                                                  mnavPos3 position)
{
    if (corridor == nullptr || buffer == nullptr || capacity < 1 || !Finite(position))
    {
        return mnav_errorInvalid;
    }
    buffer[0] = polygon;
    *corridor = (mnavCorridor){position, position, buffer, 1, capacity};
    return mnav_success;
}

mnavResult mnavResetCorridor(mnavCorridor* corridor, mnavPolygonId* buffer, int32_t capacity,
                             mnavPolygonId polygon, mnavPos3 position)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavResult result = ResetCorridorWork(corridor, buffer, capacity, polygon, position);
    mnavFloatEnvLeave(env);
    return result;
}

// Whether a path is the one its context keeps, within the counts it
// kept: mnav_errorStale when a later search's path has replaced it,
// mnav_errorInvalid for a path no search gave or one whose arrays or
// counts were changed. Only the context's own fields are read, so no
// copied field reaches outside its arrays.
static mnavResult CheckPath(const mnavPath* path)
{
    const mnavQuery* query = path->query;
    if (query == nullptr)
    {
        return mnav_errorInvalid;
    }
    if (path->generation != query->generation)
    {
        return mnav_errorStale;
    }
    bool links = path->linkCount == 0 || path->links == query->pathLinks;
    return path->polygons == query->pathPolygons && path->points == query->pathPoints && links &&
                   path->polygonCount >= 1 && path->polygonCount <= query->pathPolygonCount &&
                   path->pointCount >= 1 && path->pointCount <= query->pathPointCount &&
                   path->linkCount >= 0 && path->linkCount <= query->pathLinkCount
               ? mnav_success
               : mnav_errorInvalid;
}

static MNAV_NOINLINE mnavResult SetCorridorWork(mnavCorridor* corridor, const mnavPath* path)
{
    // The count is replaced, so only the buffer and its room are read.
    if (corridor == nullptr || path == nullptr || corridor->polygons == nullptr ||
        corridor->capacity < 1 || path->polygonCount < 1 || path->polygons == nullptr ||
        path->pointCount < 1 || path->points == nullptr)
    {
        return mnav_errorInvalid;
    }
    mnavResult checked = CheckPath(path);
    if (checked != mnav_success)
    {
        return checked;
    }
    if (path->polygonCount > corridor->capacity)
    {
        return mnav_errorCapacity;
    }
    memcpy(corridor->polygons, path->polygons, (size_t)path->polygonCount * sizeof(mnavPolygonId));
    corridor->polygonCount = path->polygonCount;
    corridor->position = path->points[0];
    corridor->target = path->points[path->pointCount - 1];
    return mnav_success;
}

mnavResult mnavSetCorridor(mnavCorridor* corridor, const mnavPath* path)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavResult result = SetCorridorWork(corridor, path);
    mnavFloatEnvLeave(env);
    return result;
}

// The point of edge ab whose coordinate along a tile side is u.
static mnavPos3 AlongSide(mnavPos3 a, mnavPos3 b, double au, double bu, double u)
{
    double t = (u - au) / (bu - au);
    return (mnavPos3){a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.z + t * (b.z - a.z)};
}

// The portal where edge j of polygon p (a to b) meets polygon q across a
// tile side: the part of the side their link covers, in the edge's
// direction. False when no link of the edge reaches q.
MNAV_NODISCARD static bool SidePortal(const mnavTile* tile, mnavPolygonId p, int32_t j,
                                      mnavPolygonId q, const mnavMeshVertex* va,
                                      const mnavMeshVertex* vb, mnavPos3 a, mnavPos3 b,
                                      mnavPortal* out)
{
    for (int32_t l = tile->firstLink[p.polygon]; l < tile->firstLink[p.polygon + 1]; ++l)
    {
        const mnavLink* link = &tile->links[l];
        if (link->edge != j || !SameId(link->target, q))
        {
            continue;
        }
        bool alongZ = link->side == 1 || link->side == 3;
        double au = alongZ ? va->z : va->x;
        double bu = alongZ ? vb->z : vb->x;
        bool rising = bu > au;
        *out = (mnavPortal){AlongSide(a, b, au, bu, rising ? link->low : link->high),
                            AlongSide(a, b, au, bu, rising ? link->high : link->low), -1};
        return true;
    }
    return false;
}

// The portal from polygon p to polygon q across one of p's edges: the
// shared edge, or the part of a tile side a link covers.
MNAV_NODISCARD static bool EdgePortal(const mnavNavmesh* navmesh, mnavPolygonId p, mnavPolygonId q,
                                      mnavPortal* out)
{
    const mnavTile* tile = nullptr;
    const mnavPolygon* polygon = mnavPolygonOf(navmesh, p, &tile);
    const mnavSlot* slot = &navmesh->slots[p.slot - 1];
    mnavFrame f = mnavFrameOf(navmesh, slot->x, slot->z);
    for (int32_t j = 0; j < polygon->count; ++j)
    {
        const mnavMeshVertex* va = &tile->mesh.vertices[polygon->vertices[j]];
        const mnavMeshVertex* vb =
            &tile->mesh.vertices[polygon->vertices[(j + 1) % polygon->count]];
        mnavPos3 a = mnavVertexWorld(&f, va);
        mnavPos3 b = mnavVertexWorld(&f, vb);
        if (p.slot == q.slot && polygon->neighbors[j] == q.polygon)
        {
            *out = (mnavPortal){a, b, -1};
            return true;
        }
        if (polygon->sides[j] != 0 && SidePortal(tile, p, j, q, va, vb, a, b, out))
        {
            return true;
        }
    }
    return false;
}

// The portals from polygon p to polygon q over an off-mesh link of a kind
// the filter allows: its takeoff point, then its landing point.
MNAV_NODISCARD static bool LinkPortals(const mnavNavmesh* navmesh, const mnavQueryFilter* filter,
                                       mnavPolygonId p, mnavPolygonId q, mnavPortal* out)
{
    int32_t first = 0;
    int32_t count = mnavAttachmentsFrom(navmesh, (int32_t)p.slot - 1, (int32_t)p.polygon, &first);
    for (int32_t i = first; i < first + count; ++i)
    {
        mnavAttachment attachment = mnavAttachmentOf(navmesh->attachments[i]);
        const mnavOffLink* link = &navmesh->links[attachment.link];
        const mnavLinkState* state = &link->state;
        mnavPolygonId landing = attachment.reverse ? state->startPolygon : state->endPolygon;
        if (SameId(landing, q) && mnavCrosses(filter, link->def.kind))
        {
            mnavPos3 takeoff = attachment.reverse ? state->end : state->start;
            mnavPos3 land = attachment.reverse ? state->start : state->end;
            out[0] =
                (mnavPortal){takeoff, takeoff, attachment.link * 2 + (attachment.reverse ? 1 : 0)};
            out[1] = (mnavPortal){land, land, -1};
            return true;
        }
    }
    return false;
}

// Writes the portals from polygon p to polygon q, both current; returns
// how many, 0 when nothing the filter allows joins them.
static int32_t Join(const mnavNavmesh* navmesh, const mnavQueryFilter* filter, mnavPolygonId p,
                    mnavPolygonId q, mnavPortal* out)
{
    if (EdgePortal(navmesh, p, q, out))
    {
        return 1;
    }
    return LinkPortals(navmesh, filter, p, q, out) ? 2 : 0;
}

// The leading polygons that are current, included and joined; stale is
// set when the first one that is not has a stale id.
static int32_t Valid(const mnavNavmesh* navmesh, const mnavQueryFilter* filter,
                     const mnavCorridor* corridor, bool* stale)
{
    mnavPortal scratch[2];
    *stale = false;
    for (int32_t i = 0; i < corridor->polygonCount; ++i)
    {
        mnavPolygonId id = corridor->polygons[i];
        mnavResult checked = mnavCheckPolygon(navmesh, id);
        const mnavTile* tile = nullptr;
        if (checked != mnav_success)
        {
            *stale = checked == mnav_errorStale;
            return i;
        }
        const mnavPolygon* polygon = mnavPolygonOf(navmesh, id, &tile);
        bool usable = i == 0 || mnavIncludes(filter, polygon->area);
        if (!usable ||
            (i > 0 && Join(navmesh, filter, corridor->polygons[i - 1], id, scratch) == 0))
        {
            return i;
        }
    }
    return corridor->polygonCount;
}

static MNAV_NOINLINE mnavResult CheckCorridorWork(const mnavNavmesh* navmesh,
                                                  const mnavQueryFilter* filter,
                                                  const mnavCorridor* corridor,
                                                  int32_t* validCountOut)
{
    if (navmesh == nullptr || validCountOut == nullptr || !WellFormed(corridor))
    {
        return mnav_errorInvalid;
    }
    if (!InReach(navmesh, corridor))
    {
        return mnav_errorLimit;
    }
    const mnavQueryFilter* usable = nullptr;
    mnavResult result = mnavCheckFilter(filter, &usable);
    if (result != mnav_success)
    {
        return result;
    }
    bool stale = false;
    *validCountOut = Valid(navmesh, usable, corridor, &stale);
    return mnav_success;
}

static MNAV_NOINLINE mnavResult CheckCorridorCounted(const mnavNavmesh* navmesh,
                                                     const mnavQueryFilter* filter,
                                                     const mnavCorridor* corridor,
                                                     int32_t* validCountOut)
{
    mnavClearOutput(validCountOut, sizeof(*validCountOut));
    mnavResult result = mnavHostCountInvalid(
        MNAV_HOST(navmesh), CheckCorridorWork(navmesh, filter, corridor, validCountOut));
    return mnavClearOnFailure(result, validCountOut, sizeof(*validCountOut));
}

mnavResult mnavNavmesh_CheckCorridor(const mnavNavmesh* navmesh, const mnavCorridor* corridor,
                                     const mnavQueryFilter* filter, int32_t* validCountOut)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavResult result = CheckCorridorCounted(navmesh, filter, corridor, validCountOut);
    mnavFloatEnvLeave(env);
    return result;
}

static MNAV_NOINLINE mnavResult CorridorCornersWork(mnavQuery* query, const mnavNavmesh* navmesh,
                                                    const mnavCorridor* corridor,
                                                    mnavCorners* cornersOut)
{
    if (query != nullptr && mnavHostCalledBack(&query->host))
    {
        return mnav_errorBusy;
    }
    if (query == nullptr || navmesh == nullptr || cornersOut == nullptr || !WellFormed(corridor))
    {
        return mnav_errorInvalid;
    }
    if (!InReach(navmesh, corridor))
    {
        return mnav_errorLimit;
    }
    if (corridor->polygonCount > query->limits.nodes)
    {
        return mnav_errorCapacity;
    }
    // Every link kind: the corridor came from a search that chose them.
    const mnavQueryFilter* all = nullptr;
    (void)mnavCheckFilter(nullptr, &all);
    bool stale = false;
    if (Valid(navmesh, all, corridor, &stale) < corridor->polygonCount)
    {
        return stale ? mnav_errorStale : mnav_errorInvalid;
    }
    // A portal per join, two per off-mesh link, and the two points: at
    // most twice the node limit and one, the context's room.
    int32_t count = 0;
    query->portals[count++] = (mnavPortal){corridor->position, corridor->position, -1};
    for (int32_t i = 1; i < corridor->polygonCount; ++i)
    {
        count += Join(navmesh, all, corridor->polygons[i - 1], corridor->polygons[i],
                      &query->portals[count]);
    }
    query->portals[count++] = (mnavPortal){corridor->target, corridor->target, -1};
    int32_t links = 0;
    int32_t points = mnavPullPortals(query, navmesh, count, &links);
    *cornersOut = (mnavCorners){query->points, points, query->links, links};
    return mnav_success;
}

static MNAV_NOINLINE mnavResult CorridorCornersCounted(mnavQuery* query, const mnavNavmesh* navmesh,
                                                       const mnavCorridor* corridor,
                                                       mnavCorners* cornersOut)
{
    mnavCorners cornersMade = {0};
    mnavResult result =
        mnavHostCountInvalid(mnavHostFirst(MNAV_HOST(query), MNAV_HOST(navmesh)),
                             CorridorCornersWork(query, navmesh, corridor,
                                                 cornersOut != nullptr ? &cornersMade : nullptr));
    mnavGiveOutput(result, false, cornersOut, &cornersMade, sizeof(cornersMade));
    return result;
}

mnavResult mnavQuery_FindCorridorCorners(mnavQuery* query, const mnavCorridor* corridor,
                                         const mnavNavmesh* navmesh, mnavCorners* cornersOut)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavResult result = CorridorCornersCounted(query, navmesh, corridor, cornersOut);
    mnavFloatEnvLeave(env);
    return result;
}

static mnavAreaType AreaOf(const mnavNavmesh* navmesh, mnavPolygonId id)
{
    const mnavTile* tile = nullptr;
    return mnavPolygonOf(navmesh, id, &tile)->area;
}

// Where the straight path crosses an edge portal, from point *corner on:
// the first of its segments to meet the portal, or one that runs along
// it from a point on it; *corner moves to that segment. The funnel
// passes the path through every portal, so one does, but for rounding,
// when the current corner stands for the crossing.
static mnavPos3 Crossing(const mnavCorners* corners, const mnavPortal* portal, int32_t* corner)
{
    const double eps = 1e-9;
    double rx = portal->right.x - portal->left.x;
    double rz = portal->right.z - portal->left.z;
    for (int32_t c = *corner; c + 1 < corners->pointCount; ++c)
    {
        mnavPos3 a = corners->points[c];
        mnavPos3 b = corners->points[c + 1];
        double sx = b.x - a.x;
        double sz = b.z - a.z;
        double qx = portal->left.x - a.x;
        double qz = portal->left.z - a.z;
        double denominator = sx * rz - sz * rx;
        double reach = rx * rx + rz * rz;
        if (fabs(denominator) <= eps * (sx * sx + sz * sz + reach))
        {
            // Along the portal's line: crossed where the segment starts,
            // when that is on the portal, within its ends.
            double side = qx * rz - qz * rx;
            double w = reach > 0.0 ? -(qx * rx + qz * rz) / reach : 0.0;
            if (side * side <= 1e-6 * reach && w >= -eps && w <= 1.0 + eps)
            {
                *corner = c;
                return a;
            }
            continue;
        }
        double t = (qx * rz - qz * rx) / denominator;
        double u = (qx * sz - qz * sx) / denominator;
        if (t >= -eps && t <= 1.0 + eps && u >= -eps && u <= 1.0 + eps)
        {
            t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
            *corner = c;
            return (mnavPos3){a.x + t * sx, a.y + t * (b.y - a.y), a.z + t * sz};
        }
    }
    return corners->points[*corner];
}

// Writes a run when it fits; counts it either way.
static void Run(mnavAreaRun* runs, int32_t capacity, int32_t* count, mnavAreaRun run)
{
    if (*count < capacity)
    {
        runs[*count] = run;
    }
    *count += 1;
}

// The index of a point among the corners from first on, or the last.
static int32_t PointAt(const mnavCorners* corners, int32_t first, mnavPos3 p)
{
    for (int32_t c = first; c < corners->pointCount; ++c)
    {
        if (corners->points[c].x == p.x && corners->points[c].z == p.z)
        {
            return c;
        }
    }
    return corners->pointCount - 1;
}

static MNAV_NOINLINE mnavResult CorridorAreasWork(mnavQuery* query, const mnavNavmesh* navmesh,
                                                  const mnavCorridor* corridor,
                                                  mnavCorners* cornersOut, mnavAreaRun* runsOut,
                                                  int32_t capacity, int32_t* countOut)
{
    if (query != nullptr && mnavHostCalledBack(&query->host))
    {
        return mnav_errorBusy;
    }
    if (countOut == nullptr || capacity < 0 || (capacity > 0 && runsOut == nullptr))
    {
        return mnav_errorInvalid;
    }
    mnavCorners corners;
    mnavResult result = CorridorCornersWork(query, navmesh, corridor, &corners);
    if (result != mnav_success)
    {
        return result;
    }
    if (cornersOut != nullptr)
    {
        *cornersOut = corners;
    }
    const mnavQueryFilter* all = nullptr;
    (void)mnavCheckFilter(nullptr, &all);
    int32_t count = 0;
    int32_t corner = 0;
    mnavAreaType area = AreaOf(navmesh, corridor->polygons[0]);
    Run(runsOut, capacity, &count, (mnavAreaRun){corridor->position, 0, area});
    for (int32_t i = 1; i < corridor->polygonCount; ++i)
    {
        // Validated above: the polygons join, and the portals are written.
        mnavPortal portals[2] = {0};
        int32_t joined =
            Join(navmesh, all, corridor->polygons[i - 1], corridor->polygons[i], portals);
        mnavAreaType next = AreaOf(navmesh, corridor->polygons[i]);
        if (joined == 2)
        {
            // Landed from an off-mesh link: a run from the landing point.
            corner = PointAt(&corners, corner, portals[1].left);
            Run(runsOut, capacity, &count, (mnavAreaRun){portals[1].left, corner, next});
            area = next;
            continue;
        }
        mnavPos3 at = Crossing(&corners, &portals[0], &corner);
        if (next != area)
        {
            Run(runsOut, capacity, &count, (mnavAreaRun){at, corner, next});
            area = next;
        }
    }
    *countOut = count;
    return count <= capacity ? mnav_success : mnav_errorCapacity;
}

static MNAV_NOINLINE mnavResult CorridorAreasCounted(mnavQuery* query, const mnavNavmesh* navmesh,
                                                     const mnavCorridor* corridor,
                                                     mnavCorners* cornersOut, mnavAreaRun* runsOut,
                                                     int32_t capacity, int32_t* countOut)
{
    mnavCorners cornersMade = {0};
    int32_t countMade = 0;
    mnavResult result = mnavHostCountInvalid(
        mnavHostFirst(MNAV_HOST(query), MNAV_HOST(navmesh)),
        CorridorAreasWork(query, navmesh, corridor, cornersOut != nullptr ? &cornersMade : nullptr,
                          runsOut, capacity, countOut != nullptr ? &countMade : nullptr));
    mnavGiveOutput(result, false, cornersOut, &cornersMade, sizeof(cornersMade));
    mnavClearArray(result, result == mnav_errorCapacity, runsOut, capacity, sizeof(*runsOut));
    mnavGiveOutput(result, result == mnav_errorCapacity, countOut, &countMade, sizeof(countMade));
    return result;
}

mnavResult mnavQuery_FindCorridorAreas(mnavQuery* query, const mnavCorridor* corridor,
                                       const mnavNavmesh* navmesh, mnavCorners* cornersOut,
                                       mnavAreaRun* runsOut, int32_t capacity, int32_t* countOut)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavResult result =
        CorridorAreasCounted(query, navmesh, corridor, cornersOut, runsOut, capacity, countOut);
    mnavFloatEnvLeave(env);
    return result;
}

// The index of a polygon among the first count of a list, or -1.
static int32_t IndexOf(const mnavPolygonId* list, int32_t count, mnavPolygonId id)
{
    for (int32_t i = 0; i < count; ++i)
    {
        if (SameId(list[i], id))
        {
            return i;
        }
    }
    return -1;
}

// Merges a walk from the corridor's first polygon into its start: the
// farthest corridor polygon the walk passed, and the walk back to it from
// where it ended, lead the corridor from there on.
static mnavResult MergeStart(mnavCorridor* corridor, const mnavMove* move)
{
    const mnavPolygonId* walked = move->polygons;
    MNAV_ASSERT(move->polygonCount >= 1 && SameId(walked[0], corridor->polygons[0]));
    int32_t farthest = -1;
    int32_t at = -1;
    for (int32_t i = corridor->polygonCount - 1; i >= 0 && farthest < 0; --i)
    {
        int32_t j = IndexOf(walked, move->polygonCount, corridor->polygons[i]);
        farthest = j >= 0 ? i : -1;
        at = j;
    }
    // The walk began on the first polygon, so some polygon is shared.
    MNAV_ASSERT(farthest >= 0 && at >= 0);
    int32_t lead = move->polygonCount - at;
    int32_t rest = corridor->polygonCount - farthest - 1;
    if (lead + rest > corridor->capacity)
    {
        return mnav_errorCapacity;
    }
    memmove(corridor->polygons + lead, corridor->polygons + farthest + 1,
            (size_t)rest * sizeof(mnavPolygonId));
    for (int32_t i = 0; i < lead; ++i)
    {
        corridor->polygons[i] = walked[move->polygonCount - 1 - i];
    }
    corridor->polygonCount = lead + rest;
    return mnav_success;
}

// Merges a walk from the corridor's last polygon into its end: the
// corridor up to the first polygon the walk passed, then the walk on.
static mnavResult MergeEnd(mnavCorridor* corridor, const mnavMove* move)
{
    const mnavPolygonId* walked = move->polygons;
    MNAV_ASSERT(move->polygonCount >= 1 &&
                SameId(walked[0], corridor->polygons[corridor->polygonCount - 1]));
    int32_t first = -1;
    int32_t at = -1;
    for (int32_t i = 0; i < corridor->polygonCount && first < 0; ++i)
    {
        at = IndexOf(walked, move->polygonCount, corridor->polygons[i]);
        first = at >= 0 ? i : -1;
    }
    // The walk began on the last polygon, so some polygon is shared.
    MNAV_ASSERT(first >= 0 && at >= 0);
    int32_t tail = move->polygonCount - at - 1;
    if (first + 1 + tail > corridor->capacity)
    {
        return mnav_errorCapacity;
    }
    memcpy(corridor->polygons + first + 1, walked + at + 1, (size_t)tail * sizeof(mnavPolygonId));
    corridor->polygonCount = first + 1 + tail;
    return mnav_success;
}

static MNAV_NOINLINE mnavResult MoveCorridorWork(mnavQuery* query, const mnavNavmesh* navmesh,
                                                 const mnavQueryFilter* filter,
                                                 mnavCorridor* corridor, mnavPos3 wanted,
                                                 mnavMove* moveOut)
{
    if (query != nullptr && mnavHostCalledBack(&query->host))
    {
        return mnav_errorBusy;
    }
    if (!WellFormed(corridor))
    {
        return mnav_errorInvalid;
    }
    if (!InReach(navmesh, corridor))
    {
        return mnav_errorLimit;
    }
    mnavMove move;
    mnavResult result = mnavMoveAlongSurfaceWork(query, navmesh, filter, corridor->polygons[0],
                                                 corridor->position, wanted, &move);
    result = result == mnav_success ? MergeStart(corridor, &move) : result;
    if (result == mnav_success)
    {
        corridor->position = move.point;
        MNAV_ASSERT(SameId(corridor->polygons[0], move.polygon));
    }
    if (result == mnav_success && moveOut != nullptr)
    {
        *moveOut = move;
    }
    return result;
}

static MNAV_NOINLINE mnavResult MoveCorridorCounted(mnavQuery* query, const mnavNavmesh* navmesh,
                                                    const mnavQueryFilter* filter,
                                                    mnavCorridor* corridor, mnavPos3 wanted,
                                                    mnavMove* moveOut)
{
    mnavMove moveMade = {0};
    mnavResult result =
        mnavHostCountInvalid(mnavHostFirst(MNAV_HOST(query), MNAV_HOST(navmesh)),
                             MoveCorridorWork(query, navmesh, filter, corridor, wanted,
                                              moveOut != nullptr ? &moveMade : nullptr));
    mnavGiveOutput(result, false, moveOut, &moveMade, sizeof(moveMade));
    return result;
}

mnavResult mnavQuery_MoveCorridor(mnavQuery* query, mnavCorridor* corridor,
                                  const mnavNavmesh* navmesh, mnavPos3 wanted,
                                  const mnavQueryFilter* filter, mnavMove* moveOut)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavResult result = MoveCorridorCounted(query, navmesh, filter, corridor, wanted, moveOut);
    mnavFloatEnvLeave(env);
    return result;
}

static MNAV_NOINLINE mnavResult MoveCorridorTargetWork(mnavQuery* query, const mnavNavmesh* navmesh,
                                                       const mnavQueryFilter* filter,
                                                       mnavCorridor* corridor, mnavPos3 wanted,
                                                       mnavMove* moveOut)
{
    if (query != nullptr && mnavHostCalledBack(&query->host))
    {
        return mnav_errorBusy;
    }
    if (!WellFormed(corridor))
    {
        return mnav_errorInvalid;
    }
    if (!InReach(navmesh, corridor))
    {
        return mnav_errorLimit;
    }
    mnavMove move;
    mnavResult result = mnavMoveAlongSurfaceWork(query, navmesh, filter,
                                                 corridor->polygons[corridor->polygonCount - 1],
                                                 corridor->target, wanted, &move);
    result = result == mnav_success ? MergeEnd(corridor, &move) : result;
    if (result == mnav_success)
    {
        corridor->target = move.point;
        MNAV_ASSERT(SameId(corridor->polygons[corridor->polygonCount - 1], move.polygon));
    }
    if (result == mnav_success && moveOut != nullptr)
    {
        *moveOut = move;
    }
    return result;
}

static MNAV_NOINLINE mnavResult MoveCorridorTargetCounted(mnavQuery* query,
                                                          const mnavNavmesh* navmesh,
                                                          const mnavQueryFilter* filter,
                                                          mnavCorridor* corridor, mnavPos3 wanted,
                                                          mnavMove* moveOut)
{
    mnavMove moveMade = {0};
    mnavResult result =
        mnavHostCountInvalid(mnavHostFirst(MNAV_HOST(query), MNAV_HOST(navmesh)),
                             MoveCorridorTargetWork(query, navmesh, filter, corridor, wanted,
                                                    moveOut != nullptr ? &moveMade : nullptr));
    mnavGiveOutput(result, false, moveOut, &moveMade, sizeof(moveMade));
    return result;
}

mnavResult mnavQuery_MoveCorridorTarget(mnavQuery* query, mnavCorridor* corridor,
                                        const mnavNavmesh* navmesh, mnavPos3 wanted,
                                        const mnavQueryFilter* filter, mnavMove* moveOut)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavResult result =
        MoveCorridorTargetCounted(query, navmesh, filter, corridor, wanted, moveOut);
    mnavFloatEnvLeave(env);
    return result;
}

static MNAV_NOINLINE mnavResult ShortcutCorridorWork(mnavQuery* query, const mnavNavmesh* navmesh,
                                                     const mnavQueryFilter* filter,
                                                     mnavCorridor* corridor, mnavPos3 toward,
                                                     bool* shortenedOut)
{
    if (query != nullptr && mnavHostCalledBack(&query->host))
    {
        return mnav_errorBusy;
    }
    if (!WellFormed(corridor) || shortenedOut == nullptr)
    {
        return mnav_errorInvalid;
    }
    if (!InReach(navmesh, corridor))
    {
        return mnav_errorLimit;
    }
    *shortenedOut = false;
    mnavRay ray;
    mnavResult result = mnavRaycastWork(query, navmesh, filter, corridor->polygons[0],
                                        corridor->position, toward, &ray);
    if (result != mnav_success || ray.end != mnav_rayReached)
    {
        return result;
    }
    // The last corridor polygon the ray passed, and where it passed it.
    int32_t last = -1;
    int32_t at = -1;
    for (int32_t i = corridor->polygonCount - 1; i >= 0 && last < 0; --i)
    {
        at = IndexOf(ray.polygons, ray.polygonCount, corridor->polygons[i]);
        last = at >= 0 ? i : -1;
    }
    // The ray began on the first polygon, so some polygon is shared.
    MNAV_ASSERT(last >= 0 && at >= 0);
    if (at >= last)
    {
        return mnav_success;
    }
    int32_t rest = corridor->polygonCount - last - 1;
    memmove(corridor->polygons + at + 1, corridor->polygons + last + 1,
            (size_t)rest * sizeof(mnavPolygonId));
    memcpy(corridor->polygons, ray.polygons, (size_t)(at + 1) * sizeof(mnavPolygonId));
    corridor->polygonCount = at + 1 + rest;
    *shortenedOut = true;
    return mnav_success;
}

static MNAV_NOINLINE mnavResult ShortcutCorridorCounted(mnavQuery* query,
                                                        const mnavNavmesh* navmesh,
                                                        const mnavQueryFilter* filter,
                                                        mnavCorridor* corridor, mnavPos3 toward,
                                                        bool* shortenedOut)
{
    bool shortenedMade = false;
    mnavResult result = mnavHostCountInvalid(
        mnavHostFirst(MNAV_HOST(query), MNAV_HOST(navmesh)),
        ShortcutCorridorWork(query, navmesh, filter, corridor, toward,
                             shortenedOut != nullptr ? &shortenedMade : nullptr));
    mnavGiveOutput(result, false, shortenedOut, &shortenedMade, sizeof(shortenedMade));
    return result;
}

mnavResult mnavQuery_ShortcutCorridor(mnavQuery* query, mnavCorridor* corridor,
                                      const mnavNavmesh* navmesh, mnavPos3 toward,
                                      const mnavQueryFilter* filter, bool* shortenedOut)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavResult result =
        ShortcutCorridorCounted(query, navmesh, filter, corridor, toward, shortenedOut);
    mnavFloatEnvLeave(env);
    return result;
}

// The polygon to plan from at a corridor end: its own while current,
// else the nearest within the box.
static mnavResult EndPolygon(const mnavNavmesh* navmesh, const mnavQueryFilter* filter,
                             mnavPolygonId own, mnavPos3* point, mnavVec3 halfExtents,
                             mnavNearest* nearest)
{
    if (mnavCheckPolygon(navmesh, own) == mnav_success)
    {
        *nearest = (mnavNearest){own, *point, true, false};
        return mnav_success;
    }
    mnavResult result = mnavFindNearestWork(navmesh, filter, *point, halfExtents, nearest);
    *point = nearest->polygon.slot != 0 ? nearest->point : *point;
    return result;
}

static MNAV_NOINLINE mnavResult ReplanCorridorWork(mnavQuery* query, const mnavNavmesh* navmesh,
                                                   const mnavQueryFilter* filter,
                                                   mnavCorridor* corridor, mnavVec3 halfExtents,
                                                   mnavPath* pathOut)
{
    if (query != nullptr && mnavHostCalledBack(&query->host))
    {
        return mnav_errorBusy;
    }
    // The box is checked though no end may need it, so that a bad one is
    // refused whatever the state of the corridor's polygons.
    if (query == nullptr || navmesh == nullptr || pathOut == nullptr || !WellFormed(corridor) ||
        !mnavGoodHalfExtents(halfExtents))
    {
        return mnav_errorInvalid;
    }
    if (!InReach(navmesh, corridor))
    {
        return mnav_errorLimit;
    }
    mnavPos3 start = corridor->position;
    mnavPos3 end = corridor->target;
    mnavNearest from;
    mnavNearest to;
    mnavResult result =
        EndPolygon(navmesh, filter, corridor->polygons[0], &start, halfExtents, &from);
    if (result == mnav_success)
    {
        result = EndPolygon(navmesh, filter, corridor->polygons[corridor->polygonCount - 1], &end,
                            halfExtents, &to);
    }
    if (result != mnav_success)
    {
        return result;
    }
    if (from.polygon.slot == 0 || to.polygon.slot == 0)
    {
        bool unloaded =
            (from.polygon.slot == 0 && from.incomplete) || (to.polygon.slot == 0 && to.incomplete);
        *pathOut = (mnavPath){unloaded ? mnav_pathNotLoaded : mnav_pathNone,
                              0.0,
                              0.0,
                              nullptr,
                              0,
                              nullptr,
                              0,
                              nullptr,
                              0,
                              nullptr,
                              0};
        mnavKeepPath(query, pathOut);
        return mnav_success;
    }
    result =
        mnavFindPathWork(query, navmesh, filter, from.polygon, start, to.polygon, end, pathOut);
    return result == mnav_success ? mnavSetCorridor(corridor, pathOut) : result;
}

static MNAV_NOINLINE mnavResult ReplanCorridorCounted(mnavQuery* query, const mnavNavmesh* navmesh,
                                                      const mnavQueryFilter* filter,
                                                      mnavCorridor* corridor, mnavVec3 halfExtents,
                                                      mnavPath* pathOut)
{
    mnavPath pathMade = {0};
    mnavResult result =
        mnavHostCountInvalid(mnavHostFirst(MNAV_HOST(query), MNAV_HOST(navmesh)),
                             ReplanCorridorWork(query, navmesh, filter, corridor, halfExtents,
                                                pathOut != nullptr ? &pathMade : nullptr));
    mnavGiveOutput(result, false, pathOut, &pathMade, sizeof(pathMade));
    return result;
}

mnavResult mnavQuery_ReplanCorridor(mnavQuery* query, mnavCorridor* corridor,
                                    const mnavNavmesh* navmesh, mnavVec3 halfExtents,
                                    const mnavQueryFilter* filter, mnavPath* pathOut)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavResult result =
        ReplanCorridorCounted(query, navmesh, filter, corridor, halfExtents, pathOut);
    mnavFloatEnvLeave(env);
    return result;
}
