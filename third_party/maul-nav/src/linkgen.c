// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Link generation (mnav-0009), after Unity's drop-down and jump-across
// links: samples along the edges with nothing across, a landing found
// below or across from each, kept unless the host's clearance test
// refuses it, the navmesh already walks there nearly as directly, or an
// earlier link has both ends near its own.

#include "maul-nav/linkgen.h"

#include "navmesh.h"
#include "query.h"
#include "query_filter.h"

#include "maul-nav/base.h"
#include "maul-nav/navmesh.h"
#include "maul-nav/query.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

// Marks a def built by mnavDefaultLinkGenDef.
#define LINKGEN_DEF_COOKIE 0x4E41564Cu

// The most polygons a landing box is searched for.
#define CANDIDATES 32

mnavLinkGenDef mnavDefaultLinkGenDef(void)
{
    return (mnavLinkGenDef){
        LINKGEN_DEF_COOKIE, 1.0f,          3.0f,    2.0f,   0.5f, 3.0f, 1.0f, 0.5f, 2.0f, 4.0f,
        mnav_linkDrop,      mnav_linkJump, nullptr, nullptr};
}

static bool Within(float v, float low, float high)
{
    return isfinite(v) && v >= low && v <= high;
}

static bool GoodDef(const mnavLinkGenDef* d)
{
    return Within(d->spacing, 0.0f, MNAV_MAX_LINK_REACH) && d->spacing > 0.0f &&
           Within(d->dropMax, 0.0f, MNAV_MAX_LINK_REACH) &&
           Within(d->jumpMax, 0.0f, MNAV_MAX_LINK_REACH) &&
           Within(d->climbMax, 0.0f, MNAV_MAX_LINK_REACH) &&
           (d->detour == 0.0f || Within(d->detour, 1.0f, 1.0e6f)) &&
           Within(d->filterDistance, 0.0f, MNAV_MAX_LINK_REACH) &&
           Within(d->radius, 0.0f, MNAV_MAX_LINK_RADIUS) &&
           Within(d->dropCost, 0.0f, MNAV_MAX_LINK_COST) &&
           Within(d->jumpCost, 0.0f, MNAV_MAX_LINK_COST) && d->dropKind < MNAV_LINK_KINDS &&
           d->jumpKind < MNAV_LINK_KINDS;
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
// one nearest the height; whether one was found.
static bool Landing(const Gen* g, double x, double z, double low, double high, double height,
                    bool highest, Spot* out)
{
    mnavPolygonId ids[CANDIDATES];
    mnavFound found;
    mnavPos3 center = {x, (low + high) * 0.5, z};
    mnavVec3 half = {(float)g->cell, (float)((high - low) * 0.5 + g->cellHeight), (float)g->cell};
    // A box a cell wide holding more polygons than this lands nowhere. No
    // navmesh has come near: floors stand an agent's height apart, and a
    // cell-wide box meets a few polygons on each.
    if (mnavFindPolygons(g->navmesh, g->filter, center, half, ids, CANDIDATES, &found) !=
        mnav_success)
    {
        return false;
    }
    bool any = false;
    for (int32_t i = 0; i < found.count; ++i)
    {
        double h = 0.0;
        if (mnavGetHeight(g->navmesh, ids[i], x, z, &h) != mnav_success || h < low || h > high)
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
    if (mnavFindPath(g->query, g->navmesh, g->filter, from.polygon, from.point, to.polygon,
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

static void Keep(Gen* g, Spot from, Spot to, bool drop)
{
    const mnavLinkGenDef* d = g->def;
    if ((d->clear != nullptr && !d->clear(d->context, from.point, to.point)) ||
        Near(g, from.point, to.point) || Walks(g, from, to))
    {
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
    for (double out = 2.0 * g->radius; d->jumpMax > 0.0f && out <= (double)d->jumpMax;
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
    int32_t samples = (int32_t)floor(length / (double)g->def->spacing);
    samples = samples < 1 ? 1 : samples;
    for (int32_t i = 0; i < samples; ++i)
    {
        double t = ((double)i + 0.5) / (double)samples;
        mnavPos3 edge = {a.x + t * ex, a.y + t * (b.y - a.y), a.z + t * ez};
        // The takeoff a little inside the edge, on the surface.
        double x = edge.x - nx * 2.0 * g->cell;
        double z = edge.z - nz * 2.0 * g->cell;
        double h = edge.y;
        if (mnavGetHeight(g->navmesh, polygon, x, z, &h) != mnav_success)
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
    for (int32_t p = 0; p < tile->mesh.polygonCount; ++p)
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

mnavResult mnavGenerateLinks(mnavQuery* query, const mnavNavmesh* navmesh,
                             const mnavQueryFilter* filter, const mnavLinkGenDef* def,
                             int32_t tileX0, int32_t tileZ0, int32_t tileX1, int32_t tileZ1,
                             mnavLinkDef* linksOut, int32_t capacity, int32_t* countOut)
{
    if (query == nullptr || navmesh == nullptr || def == nullptr || countOut == nullptr ||
        capacity < 0 || (capacity > 0 && linksOut == nullptr) ||
        def->cookie != LINKGEN_DEF_COOKIE || tileX1 < tileX0 || tileZ1 < tileZ0)
    {
        return mnav_errorInvalid;
    }
    const mnavQueryFilter* usable = nullptr;
    mnavResult result = mnavCheckFilter(filter, &usable);
    if (result != mnav_success)
    {
        return result;
    }
    if (!GoodDef(def))
    {
        return mnav_errorRange;
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
             0};
    for (int32_t i = 0; i < navmesh->placeCount; ++i)
    {
        const mnavPlace* p = &navmesh->places[i];
        if (p->x >= tileX0 && p->x <= tileX1 && p->z >= tileZ0 && p->z <= tileZ1)
        {
            SampleTile(&g, p->slot);
        }
    }
    *countOut = g.count;
    return g.count > capacity ? mnav_errorCapacity : mnav_success;
}
