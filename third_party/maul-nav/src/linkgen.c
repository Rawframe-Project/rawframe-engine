// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Link generation, after Unity's drop-down and jump-across
// links: samples along the edges with nothing across, a landing found
// below or across from each, kept unless the host's clearance test
// refuses it, the navmesh already walks there nearly as directly, or an
// earlier link has both ends near its own.

#include "linkgen.h"

#include "fpenv.h"
#include "navmesh.h"
#include "nearest.h"
#include "query.h"
#include "query_filter.h"

#include "maul-nav/bake.h"
#include "maul-nav/base.h"
#include "maul-nav/linkgen.h"
#include "maul-nav/navmesh.h"
#include "maul-nav/query.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

static bool Within(float v, float low, float high)
{
    return isfinite(v) && v >= low && v <= high;
}

// The def's distances, costs and kinds: mnav_errorInvalid for a NaN or a
// value below 0, a spacing of 0, a detour from 0 to 1 or past 10^6, or
// a kind past the kinds; mnav_errorLimit past an MNAV_MAX_ value, an
// infinity included.
static mnavResult CheckDef(const mnavLinkGenDef* d)
{
    const float values[8] = {d->spacing,        d->dropMax, d->jumpMax,  d->climbMax,
                             d->filterDistance, d->radius,  d->dropCost, d->jumpCost};
    const float most[8] = {MNAV_MAX_LINK_REACH, MNAV_MAX_LINK_REACH, MNAV_MAX_LINK_REACH,
                           MNAV_MAX_LINK_REACH, MNAV_MAX_LINK_REACH, MNAV_MAX_LINK_RADIUS,
                           MNAV_MAX_LINK_COST,  MNAV_MAX_LINK_COST};
    bool past = false;
    for (int32_t k = 0; k < 8; ++k)
    {
        if (!(values[k] >= 0.0f))
        {
            return mnav_errorInvalid;
        }
        past = past || values[k] > most[k];
    }
    if (d->spacing == 0.0f || (d->detour != 0.0f && !Within(d->detour, 1.0f, 1.0e6f)) ||
        d->dropKind >= MNAV_LINK_KINDS || d->jumpKind >= MNAV_LINK_KINDS)
    {
        return mnav_errorInvalid;
    }
    return past ? mnav_errorLimit : mnav_success;
}

// A generation: its inputs, the agent's sizes in meters, and the links so
// far.
typedef struct Gen
{
    mnavQuery* query;
    const mnavNavmesh* navmesh;
    const mnavQueryFilter* filter;
    const mnavLinkGenDef* def;
    double radius;
    double step;
    double cell;
    double cellHeight;
    mnavLinkDef* links;
    int32_t capacity;
    int32_t count;
    // The links found before generation stops, the polygons a landing box
    // may meet, and mnav_errorLimit once either is passed.
    int32_t total;
    int32_t landings;
    mnavResult result;
} Gen;

// A place on the navmesh: a polygon and a point on it.
typedef struct Spot
{
    mnavPolygonId polygon;
    mnavPos3 point;
} Spot;

static double Distance(mnavPos3 a, mnavPos3 b)
{
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    double dz = a.z - b.z;
    return sqrt(dx * dx + dy * dy + dz * dz);
}

// The surface nearest a height at a ground point, among the polygons in a
// box there: the highest from low to high when highest is set, else the
// one nearest the height; whether one was found. A box meeting more
// polygons than the landings stops generation.
static bool Landing(Gen* g, double x, double z, double low, double high, double height,
                    bool highest, Spot* out)
{
    mnavPolygonId ids[MNAV_MAX_LINK_LANDINGS];
    mnavFound found;
    mnavPos3 center = {x, (low + high) * 0.5, z};
    mnavVec3 half = {(float)g->cell, (float)((high - low) * 0.5 + g->cellHeight), (float)g->cell};
    mnavResult result =
        mnavFindPolygonsWork(g->navmesh, g->filter, center, half, ids, g->landings, &found);
    if (result != mnav_success)
    {
        g->result = result == mnav_errorCapacity ? mnav_errorLimit : g->result;
        return false;
    }
    bool any = false;
    for (int32_t i = 0; i < found.count; ++i)
    {
        double h = 0.0;
        if (mnavGetHeightWork(g->navmesh, ids[i], x, z, &h) != mnav_success || h < low || h > high)
        {
            continue;
        }
        bool better = highest ? h > out->point.y : fabs(h - height) < fabs(out->point.y - height);
        if (!any || better)
        {
            *out = (Spot){ids[i], {x, h, z}};
            any = true;
        }
    }
    return any;
}

// Whether the navmesh walks from one spot to the other within the def's
// detour times the straight distance: the straight path's length, since
// the search's own runs through portal midpoints and may be far longer.
static bool Walks(const Gen* g, Spot from, Spot to)
{
    if (g->def->detour == 0.0f)
    {
        return false;
    }
    mnavPath path;
    if (mnavFindPathWork(g->query, g->navmesh, g->filter, from.polygon, from.point, to.polygon,
                         to.point, &path) != mnav_success ||
        path.end != mnav_pathFound)
    {
        return false;
    }
    double length = 0.0;
    for (int32_t i = 1; i < path.pointCount; ++i)
    {
        length += Distance(path.points[i - 1], path.points[i]);
    }
    return length <= (double)g->def->detour * Distance(from.point, to.point);
}

// Whether both ends of a link lie near an earlier link's.
static bool Near(const Gen* g, mnavPos3 start, mnavPos3 end)
{
    double d = (double)g->def->filterDistance;
    int32_t held = g->count < g->capacity ? g->count : g->capacity;
    for (int32_t i = 0; i < held; ++i)
    {
        if (Distance(g->links[i].start, start) <= d && Distance(g->links[i].end, end) <= d)
        {
            return true;
        }
    }
    return false;
}

// The host's clearance test between two points, true without one. The
// navmesh and the context are marked while it runs, so that a call back
// into either that would change what generation reads is refused; the
// navmesh is only read, and its mark is the one field this writes
// through it.
static bool Clear(const Gen* g, mnavPos3 from, mnavPos3 to)
{
    const mnavLinkGenDef* d = g->def;
    if (d->clear == nullptr)
    {
        return true;
    }
    mnavHostCode* navmesh = (mnavHostCode*)&g->navmesh->host;
    mnavHostEnterReader(navmesh);
    mnavHostEnterReader(&g->query->host);
    bool clear = d->clear(d->context, from, to);
    mnavHostLeaveReader(&g->query->host);
    mnavHostLeaveReader(navmesh);
    return clear;
}

static void Keep(Gen* g, Spot from, Spot to, bool drop)
{
    const mnavLinkGenDef* d = g->def;
    if (!Clear(g, from.point, to.point) || Near(g, from.point, to.point) || Walks(g, from, to))
    {
        return;
    }
    if (g->count == g->total)
    {
        g->result = mnav_errorLimit;
        return;
    }
    bool twoWay = drop && from.point.y - to.point.y <= (double)d->climbMax;
    if (g->count < g->capacity)
    {
        g->links[g->count] = (mnavLinkDef){from.point,
                                           to.point,
                                           d->radius,
                                           drop ? d->dropCost : d->jumpCost,
                                           drop ? d->dropKind : d->jumpKind,
                                           twoWay,
                                           0.0f};
    }
    g->count += 1;
}

// Tries a drop and a jump from a sample: the takeoff spot, the edge point
// and the outward direction on the ground.
static void Sample(Gen* g, Spot from, mnavPos3 edge, double nx, double nz)
{
    const mnavLinkGenDef* d = g->def;
    double top = from.point.y;
    if (d->dropMax > 0.0f && (double)d->dropMax > g->step)
    {
        double out = 2.0 * g->radius + 4.0 * g->cell;
        Spot to = {{0, 0, 0}, {0.0, -(double)INFINITY, 0.0}};
        if (Landing(g, edge.x + nx * out, edge.z + nz * out, top - (double)d->dropMax,
                    top - g->step, top, true, &to))
        {
            Keep(g, from, to, true);
        }
    }
    for (double out = 2.0 * g->radius;
         d->jumpMax > 0.0f && out <= (double)d->jumpMax && g->result == mnav_success;
         out += g->cell)
    {
        Spot to = {{0, 0, 0}, {0.0, (double)INFINITY, 0.0}};
        if (Landing(g, edge.x + nx * out, edge.z + nz * out, top - g->step, top + g->step, top,
                    false, &to))
        {
            Keep(g, from, to, false);
            break;
        }
    }
}

// Samples edge j of a polygon, from a to b, the polygon's center c.
static void SampleEdge(Gen* g, mnavPolygonId polygon, mnavPos3 a, mnavPos3 b, mnavPos3 c)
{
    double ex = b.x - a.x;
    double ez = b.z - a.z;
    double length = sqrt(ex * ex + ez * ez);
    // The loader refuses a polygon with an edge of no length on the
    // ground, so this guards only against that check changing.
    if (length == 0.0)
    {
        return;
    }
    // The ground normal away from the polygon's center.
    double nx = ez / length;
    double nz = -ex / length;
    if (nx * ((a.x + b.x) * 0.5 - c.x) + nz * ((a.z + b.z) * 0.5 - c.z) < 0.0)
    {
        nx = -nx;
        nz = -nz;
    }
    // The spacing is at least a cell and an edge lies within its tile, so
    // an edge has fewer than 2 * MNAV_MAX_TILE_CELLS samples; the count is
    // kept to that before it converts.
    double count = floor(length / (double)g->def->spacing);
    count =
        count < 1.0 ? 1.0 : (count > 2.0 * MNAV_MAX_TILE_CELLS ? 2.0 * MNAV_MAX_TILE_CELLS : count);
    int32_t samples = (int32_t)count;
    for (int32_t i = 0; i < samples && g->result == mnav_success; ++i)
    {
        double t = ((double)i + 0.5) / (double)samples;
        mnavPos3 edge = {a.x + t * ex, a.y + t * (b.y - a.y), a.z + t * ez};
        // The takeoff a little inside the edge, on the surface.
        double x = edge.x - nx * 2.0 * g->cell;
        double z = edge.z - nz * 2.0 * g->cell;
        double h = edge.y;
        if (mnavGetHeightWork(g->navmesh, polygon, x, z, &h) != mnav_success)
        {
            x = edge.x;
            z = edge.z;
            h = edge.y;
        }
        Sample(g, (Spot){polygon, {x, h, z}}, edge, nx, nz);
    }
}

// Samples the edges of the tile in slot with nothing across and no tile
// side.
static void SampleTile(Gen* g, int32_t slot)
{
    const mnavSlot* s = &g->navmesh->slots[slot];
    const mnavTile* tile = s->tile;
    mnavFrame f = mnavFrameOf(g->navmesh, s->x, s->z);
    for (int32_t p = 0; p < tile->mesh.polygonCount && g->result == mnav_success; ++p)
    {
        const mnavPolygon* polygon = &tile->mesh.polygons[p];
        if (!mnavIncludes(g->filter, polygon->area))
        {
            continue;
        }
        mnavPos3 corners[MNAV_POLYGON_VERTICES];
        mnavPos3 center = {0.0, 0.0, 0.0};
        for (int32_t k = 0; k < polygon->count; ++k)
        {
            corners[k] = mnavVertexWorld(&f, &tile->mesh.vertices[polygon->vertices[k]]);
            center.x += corners[k].x / (double)polygon->count;
            center.z += corners[k].z / (double)polygon->count;
        }
        mnavPolygonId id = {(uint32_t)slot + 1, s->generation, (uint32_t)p};
        for (int32_t j = 0; j < polygon->count; ++j)
        {
            if (polygon->neighbors[j] == MNAV_NO_INDEX && polygon->sides[j] == 0)
            {
                SampleEdge(g, id, corners[j], corners[(j + 1) % polygon->count], center);
            }
        }
    }
}

static mnavResult Generate(mnavQuery* query, const mnavNavmesh* navmesh,
                           const mnavQueryFilter* filter, const mnavLinkGenDef* def,
                           mnavTileRange range, mnavLinkDef* linksOut, int32_t capacity,
                           int32_t* countOut, mnavLinkGenLimits limits)
{
    if (query == nullptr || navmesh == nullptr || def == nullptr || countOut == nullptr ||
        capacity < 0 || (capacity > 0 && linksOut == nullptr) ||
        def->cookie != MNAV_LINK_GEN_DEF_COOKIE || range.x1 < range.x0 || range.z1 < range.z0)
    {
        return mnav_errorInvalid;
    }
    const mnavQueryFilter* usable = nullptr;
    mnavResult result = mnavCheckFilter(filter, &usable);
    if (result != mnav_success)
    {
        return result;
    }
    result = CheckDef(def);
    if (result != mnav_success)
    {
        return result;
    }
    // Samples closer than a cell tell nothing a cell's resolution can.
    if (def->spacing < navmesh->def.cellSize)
    {
        return mnav_errorInvalid;
    }
    // The filter is copied: the searches begun here replace the context's.
    mnavQueryFilter own = *usable;
    const mnavAgentProfile* agent = &navmesh->def.agent;
    Gen g = {query,
             navmesh,
             &own,
             def,
             (double)agent->radius,
             (double)agent->stepHeight,
             (double)navmesh->def.cellSize,
             (double)navmesh->def.cellHeight,
             linksOut,
             capacity,
             0,
             limits.links,
             limits.landings,
             mnav_success};
    for (int32_t i = 0; i < navmesh->placeCount && g.result == mnav_success; ++i)
    {
        const mnavPlace* p = &navmesh->places[i];
        if (p->x >= range.x0 && p->x <= range.x1 && p->z >= range.z0 && p->z <= range.z1)
        {
            SampleTile(&g, p->slot);
        }
    }
    *countOut = g.count;
    if (g.result != mnav_success)
    {
        return g.result;
    }
    return g.count > capacity ? mnav_errorCapacity : mnav_success;
}

static MNAV_NOINLINE mnavResult GenerateLinksWork(mnavQuery* query, const mnavNavmesh* navmesh,
                                                  const mnavQueryFilter* filter,
                                                  const mnavLinkGenDef* def, int32_t tileX0,
                                                  int32_t tileZ0, int32_t tileX1, int32_t tileZ1,
                                                  mnavLinkDef* linksOut, int32_t capacity,
                                                  int32_t* countOut)
{
    if (query != nullptr && mnavHostCalledBack(&query->host))
    {
        return mnav_errorBusy;
    }
    return Generate(query, navmesh, filter, def, (mnavTileRange){tileX0, tileZ0, tileX1, tileZ1},
                    linksOut, capacity, countOut,
                    (mnavLinkGenLimits){MNAV_MAX_LINKS, MNAV_MAX_LINK_LANDINGS});
}

static MNAV_NOINLINE mnavResult GenerateLinksCounted(mnavQuery* query, const mnavNavmesh* navmesh,
                                                     const mnavQueryFilter* filter,
                                                     const mnavLinkGenDef* def, int32_t tileX0,
                                                     int32_t tileZ0, int32_t tileX1, int32_t tileZ1,
                                                     mnavLinkDef* linksOut, int32_t capacity,
                                                     int32_t* countOut)
{
    return mnavHostCountInvalid(mnavHostFirst(MNAV_HOST(query), MNAV_HOST(navmesh)),
                                GenerateLinksWork(query, navmesh, filter, def, tileX0, tileZ0,
                                                  tileX1, tileZ1, linksOut, capacity, countOut));
}

mnavResult mnavGenerateLinks(mnavQuery* query, const mnavNavmesh* navmesh,
                             const mnavQueryFilter* filter, const mnavLinkGenDef* def,
                             int32_t tileX0, int32_t tileZ0, int32_t tileX1, int32_t tileZ1,
                             mnavLinkDef* linksOut, int32_t capacity, int32_t* countOut)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavResult result = GenerateLinksCounted(query, navmesh, filter, def, tileX0, tileZ0, tileX1,
                                             tileZ1, linksOut, capacity, countOut);
    mnavFloatEnvLeave(env);
    return result;
}

static MNAV_NOINLINE mnavResult
GenerateLinksWithinWork(mnavQuery* query, const mnavNavmesh* navmesh, const mnavQueryFilter* filter,
                        const mnavLinkGenDef* def, mnavTileRange range, mnavLinkDef* linksOut,
                        int32_t capacity, int32_t* countOut, mnavLinkGenLimits limits)
{
    if (query != nullptr && mnavHostCalledBack(&query->host))
    {
        return mnav_errorBusy;
    }
    return Generate(query, navmesh, filter, def, range, linksOut, capacity, countOut, limits);
}

mnavResult mnavGenerateLinksWithin(mnavQuery* query, const mnavNavmesh* navmesh,
                                   const mnavQueryFilter* filter, const mnavLinkGenDef* def,
                                   mnavTileRange range, mnavLinkDef* linksOut, int32_t capacity,
                                   int32_t* countOut, mnavLinkGenLimits limits)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavResult result = GenerateLinksWithinWork(query, navmesh, filter, def, range, linksOut,
                                                capacity, countOut, limits);
    mnavFloatEnvLeave(env);
    return result;
}
