// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Path corridors (mnav-0005): a caller's buffer of polygons from an agent
// to its target, checked and pulled tight.

#include "funnel.h"
#include "navmesh.h"
#include "offmesh.h"
#include "polymesh.h"
#include "query.h"
#include "query_filter.h"

#include "maul-nav/base.h"
#include "maul-nav/navmesh.h"
#include "maul-nav/query.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static bool Finite(mnavPos3 p)
{
    return isfinite(p.x) && isfinite(p.y) && isfinite(p.z);
}

static bool SameId(mnavPolygonId a, mnavPolygonId b)
{
    return a.slot == b.slot && a.generation == b.generation && a.polygon == b.polygon;
}

mnavResult mnavResetCorridor(mnavCorridor* corridor, mnavPolygonId* buffer, int32_t capacity,
                             mnavPolygonId polygon, mnavPos3 position)
{
    if (corridor == nullptr || buffer == nullptr || capacity < 1 || !Finite(position))
    {
        return mnav_errorInvalid;
    }
    buffer[0] = polygon;
    *corridor = (mnavCorridor){position, position, buffer, 1, capacity};
    return mnav_success;
}

mnavResult mnavSetCorridor(mnavCorridor* corridor, const mnavPath* path)
{
    if (corridor == nullptr || path == nullptr || corridor->polygons == nullptr ||
        path->polygonCount < 1 || path->pointCount < 1)
    {
        return mnav_errorInvalid;
    }
    if (path->polygonCount > corridor->capacity)
    {
        return mnav_errorCapacity;
    }
    memcpy(corridor->polygons, path->polygons, (size_t)path->polygonCount * sizeof(mnavPolygonId));
    corridor->count = path->polygonCount;
    corridor->position = path->points[0];
    corridor->target = path->points[path->pointCount - 1];
    return mnav_success;
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
static bool SidePortal(const mnavTile* tile, mnavPolygonId p, int32_t j, mnavPolygonId q,
                       const mnavMeshVertex* va, const mnavMeshVertex* vb, mnavPos3 a, mnavPos3 b,
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
static bool EdgePortal(const mnavNavmesh* navmesh, mnavPolygonId p, mnavPolygonId q,
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
static bool LinkPortals(const mnavNavmesh* navmesh, const mnavQueryFilter* filter, mnavPolygonId p,
                        mnavPolygonId q, mnavPortal* out)
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
    for (int32_t i = 0; i < corridor->count; ++i)
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
    return corridor->count;
}

mnavResult mnavCheckCorridor(const mnavNavmesh* navmesh, const mnavQueryFilter* filter,
                             const mnavCorridor* corridor, int32_t* validOut)
{
    if (navmesh == nullptr || corridor == nullptr || validOut == nullptr ||
        corridor->polygons == nullptr)
    {
        return mnav_errorInvalid;
    }
    const mnavQueryFilter* usable = nullptr;
    mnavResult result = mnavCheckFilter(filter, &usable);
    if (result != mnav_success)
    {
        return result;
    }
    bool stale = false;
    *validOut = Valid(navmesh, usable, corridor, &stale);
    return mnav_success;
}

mnavResult mnavCorridorCorners(mnavQuery* query, const mnavNavmesh* navmesh,
                               const mnavCorridor* corridor, mnavCorners* cornersOut)
{
    if (query == nullptr || navmesh == nullptr || corridor == nullptr || cornersOut == nullptr ||
        corridor->polygons == nullptr || corridor->count < 1)
    {
        return mnav_errorInvalid;
    }
    if (corridor->count > query->limits.nodes)
    {
        return mnav_errorLimit;
    }
    // Every link kind: the corridor came from a search that chose them.
    const mnavQueryFilter* all = nullptr;
    (void)mnavCheckFilter(nullptr, &all);
    bool stale = false;
    if (Valid(navmesh, all, corridor, &stale) < corridor->count)
    {
        return stale ? mnav_errorStale : mnav_errorInvalid;
    }
    // A portal per join, two per off-mesh link, and the two points: at
    // most twice the node limit and one, the context's room.
    int32_t count = 0;
    query->portals[count++] = (mnavPortal){corridor->position, corridor->position, -1};
    for (int32_t i = 1; i < corridor->count; ++i)
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

mnavResult mnavCorridorAreas(mnavQuery* query, const mnavNavmesh* navmesh,
                             const mnavCorridor* corridor, mnavCorners* cornersOut,
                             mnavAreaRun* runsOut, int32_t capacity, int32_t* countOut)
{
    if (countOut == nullptr || capacity < 0 || (capacity > 0 && runsOut == nullptr))
    {
        return mnav_errorInvalid;
    }
    mnavCorners corners;
    mnavResult result = mnavCorridorCorners(query, navmesh, corridor, &corners);
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
    for (int32_t i = 1; i < corridor->count; ++i)
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
    int32_t farthest = -1;
    int32_t at = -1;
    for (int32_t i = corridor->count - 1; i >= 0 && farthest < 0; --i)
    {
        int32_t j = IndexOf(walked, move->polygonCount, corridor->polygons[i]);
        farthest = j >= 0 ? i : -1;
        at = j;
    }
    // The walk began on the first polygon, so some polygon is shared.
    int32_t lead = move->polygonCount - at;
    int32_t rest = corridor->count - farthest - 1;
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
    corridor->count = lead + rest;
    return mnav_success;
}

// Merges a walk from the corridor's last polygon into its end: the
// corridor up to the first polygon the walk passed, then the walk on.
static mnavResult MergeEnd(mnavCorridor* corridor, const mnavMove* move)
{
    const mnavPolygonId* walked = move->polygons;
    int32_t first = -1;
    int32_t at = -1;
    for (int32_t i = 0; i < corridor->count && first < 0; ++i)
    {
        at = IndexOf(walked, move->polygonCount, corridor->polygons[i]);
        first = at >= 0 ? i : -1;
    }
    int32_t tail = move->polygonCount - at - 1;
    if (first + 1 + tail > corridor->capacity)
    {
        return mnav_errorCapacity;
    }
    memcpy(corridor->polygons + first + 1, walked + at + 1, (size_t)tail * sizeof(mnavPolygonId));
    corridor->count = first + 1 + tail;
    return mnav_success;
}

mnavResult mnavMoveCorridor(mnavQuery* query, const mnavNavmesh* navmesh,
                            const mnavQueryFilter* filter, mnavCorridor* corridor, mnavPos3 wanted,
                            mnavMove* moveOut)
{
    if (corridor == nullptr || corridor->polygons == nullptr || corridor->count < 1)
    {
        return mnav_errorInvalid;
    }
    mnavMove move;
    mnavResult result = mnavMoveAlongSurface(query, navmesh, filter, corridor->polygons[0],
                                             corridor->position, wanted, &move);
    result = result == mnav_success ? MergeStart(corridor, &move) : result;
    if (result == mnav_success)
    {
        corridor->position = move.point;
    }
    if (result == mnav_success && moveOut != nullptr)
    {
        *moveOut = move;
    }
    return result;
}

mnavResult mnavMoveCorridorTarget(mnavQuery* query, const mnavNavmesh* navmesh,
                                  const mnavQueryFilter* filter, mnavCorridor* corridor,
                                  mnavPos3 wanted, mnavMove* moveOut)
{
    if (corridor == nullptr || corridor->polygons == nullptr || corridor->count < 1)
    {
        return mnav_errorInvalid;
    }
    mnavMove move;
    mnavResult result =
        mnavMoveAlongSurface(query, navmesh, filter, corridor->polygons[corridor->count - 1],
                             corridor->target, wanted, &move);
    result = result == mnav_success ? MergeEnd(corridor, &move) : result;
    if (result == mnav_success)
    {
        corridor->target = move.point;
    }
    if (result == mnav_success && moveOut != nullptr)
    {
        *moveOut = move;
    }
    return result;
}

mnavResult mnavShortcutCorridor(mnavQuery* query, const mnavNavmesh* navmesh,
                                const mnavQueryFilter* filter, mnavCorridor* corridor,
                                mnavPos3 toward, bool* shortenedOut)
{
    if (corridor == nullptr || corridor->polygons == nullptr || corridor->count < 1 ||
        shortenedOut == nullptr)
    {
        return mnav_errorInvalid;
    }
    *shortenedOut = false;
    mnavRay ray;
    mnavResult result = mnavRaycast(query, navmesh, filter, corridor->polygons[0],
                                    corridor->position, toward, &ray);
    if (result != mnav_success || ray.end != mnav_rayReached)
    {
        return result;
    }
    // The last corridor polygon the ray passed, and where it passed it.
    int32_t last = -1;
    int32_t at = -1;
    for (int32_t i = corridor->count - 1; i >= 0 && last < 0; --i)
    {
        at = IndexOf(ray.polygons, ray.polygonCount, corridor->polygons[i]);
        last = at >= 0 ? i : -1;
    }
    if (at >= last)
    {
        return mnav_success;
    }
    int32_t rest = corridor->count - last - 1;
    memmove(corridor->polygons + at + 1, corridor->polygons + last + 1,
            (size_t)rest * sizeof(mnavPolygonId));
    memcpy(corridor->polygons, ray.polygons, (size_t)(at + 1) * sizeof(mnavPolygonId));
    corridor->count = at + 1 + rest;
    *shortenedOut = true;
    return mnav_success;
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
    mnavResult result = mnavFindNearest(navmesh, filter, *point, halfExtents, nearest);
    *point = nearest->polygon.slot != 0 ? nearest->point : *point;
    return result;
}

mnavResult mnavReplanCorridor(mnavQuery* query, const mnavNavmesh* navmesh,
                              const mnavQueryFilter* filter, mnavCorridor* corridor,
                              mnavVec3 halfExtents, mnavPath* pathOut)
{
    if (query == nullptr || navmesh == nullptr || corridor == nullptr || pathOut == nullptr ||
        corridor->polygons == nullptr || corridor->count < 1)
    {
        return mnav_errorInvalid;
    }
    mnavPos3 start = corridor->position;
    mnavPos3 end = corridor->target;
    mnavNearest from;
    mnavNearest to;
    mnavResult result =
        EndPolygon(navmesh, filter, corridor->polygons[0], &start, halfExtents, &from);
    if (result == mnav_success)
    {
        result = EndPolygon(navmesh, filter, corridor->polygons[corridor->count - 1], &end,
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
                              0};
        return mnav_success;
    }
    result = mnavFindPath(query, navmesh, filter, from.polygon, start, to.polygon, end, pathOut);
    return result == mnav_success ? mnavSetCorridor(corridor, pathOut) : result;
}
