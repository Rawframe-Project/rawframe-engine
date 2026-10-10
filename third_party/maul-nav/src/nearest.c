// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The nearest point on the navmesh (mnav-0005).

#include "nearest.h"

#include "detail.h"
#include "navmesh.h"
#include "polymesh.h"
#include "query_filter.h"
#include "raster.h"

#include "maul-nav/bake.h"
#include "maul-nav/base.h"
#include "maul-nav/navmesh.h"
#include "maul-nav/query.h"

#include <math.h>
#include <stdint.h>

#define SUBCELLS 16.0

// A point on the ground in a tile's cells.
typedef struct Flat
{
    double x;
    double z;
} Flat;

static double Cross(Flat a, Flat b, Flat p)
{
    return (b.x - a.x) * (p.z - a.z) - (p.x - a.x) * (b.z - a.z);
}

// The point of segment ab nearest p.
static Flat OnSegment(Flat a, Flat b, Flat p)
{
    double dx = b.x - a.x;
    double dz = b.z - a.z;
    double length2 = dx * dx + dz * dz;
    double t = length2 > 0.0 ? ((p.x - a.x) * dx + (p.z - a.z) * dz) / length2 : 0.0;
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    return (Flat){a.x + t * dx, a.z + t * dz};
}

static double Distance2(Flat a, Flat b)
{
    return (a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z);
}

// The point of a convex ring nearest p, which is p itself inside it; the
// first edge's on ties. Rings are wound like the polygons, their inside
// to the negative side of every edge.
static Flat NearestOnRing(const Flat* ring, int32_t count, Flat p, bool* inside)
{
    *inside = true;
    for (int32_t k = 0; k < count && *inside; ++k)
    {
        *inside = Cross(ring[k], ring[(k + 1) % count], p) <= 0.0;
    }
    if (*inside)
    {
        return p;
    }
    Flat best = ring[0];
    double bestDistance = (double)INFINITY;
    for (int32_t k = 0; k < count; ++k)
    {
        Flat q = OnSegment(ring[k], ring[(k + 1) % count], p);
        double d = Distance2(q, p);
        if (d < bestDistance)
        {
            best = q;
            bestDistance = d;
        }
    }
    return best;
}

// The height at q of a triangle of nonzero area: its corners' heights
// weighted by where q lies.
static double TriangleHeight(const mnavDetailVertex* const v[3], const Flat ring[3], double area,
                             Flat q)
{
    double wa = Cross(q, ring[1], ring[2]) / area;
    double wb = Cross(ring[0], q, ring[2]) / area;
    double wc = 1.0 - wa - wb;
    return wa * v[0]->y + wb * v[1]->y + wc * v[2]->y;
}

static void Corners(const mnavTile* tile, const mnavDetailPart* part, int32_t t,
                    const mnavDetailVertex* v[3], Flat ring[3])
{
    const mnavDetailVertex* vertices = &tile->detail.vertices[part->firstVertex];
    const mnavDetailTriangle* triangle = &tile->detail.triangles[part->firstTriangle + t];
    for (int32_t c = 0; c < 3; ++c)
    {
        v[c] = &vertices[triangle->corners[c]];
        ring[c] = (Flat){v[c]->x, v[c]->z};
    }
}

// The height of the first triangle holding q, as the full scan finds it
// when no triangle before it comes within rounding of q: only such a one
// could reach q at distance 0 first. False when the scan must decide.
static bool HoldingHeight(const mnavTile* tile, const mnavDetailPart* part, Flat q,
                          double* heightOut)
{
    for (int32_t t = 0; t < part->triangleCount; ++t)
    {
        const mnavDetailVertex* v[3];
        Flat ring[3];
        Corners(tile, part, t, v, ring);
        double area = Cross(ring[0], ring[1], ring[2]);
        bool inside = true;
        bool near = false;
        for (int32_t k = 0; k < 3; ++k)
        {
            Flat a = ring[k];
            Flat b = ring[(k + 1) % 3];
            double c = Cross(a, b, q);
            inside = inside && c <= 0.0;
            near = near || c * c <= 1e-12 * Distance2(a, b) * Distance2(a, b);
        }
        if (inside && area != 0.0)
        {
            *heightOut = TriangleHeight(v, ring, area, q);
            return true;
        }
        if (near)
        {
            return false;
        }
    }
    return false;
}

// The detail surface's height, in cell heights from the offset, at the
// point of the polygon's detail nearest p (in cells): the triangle nearest
// p, the first on ties, interpolated there.
static double DetailHeight(const mnavTile* tile, int32_t p, Flat at)
{
    const mnavDetailPart* part = &tile->detail.parts[p];
    Flat q = {at.x * SUBCELLS, at.z * SUBCELLS};
    double height = tile->detail.vertices[part->firstVertex].y;
    if (HoldingHeight(tile, part, q, &height))
    {
        return height;
    }
    double best = (double)INFINITY;
    for (int32_t t = 0; t < part->triangleCount; ++t)
    {
        const mnavDetailVertex* v[3];
        Flat ring[3];
        Corners(tile, part, t, v, ring);
        double area = Cross(ring[0], ring[1], ring[2]);
        bool inside = false;
        Flat on = NearestOnRing(ring, 3, q, &inside);
        double d = Distance2(on, q);
        if (area == 0.0 || d >= best)
        {
            continue;
        }
        height = TriangleHeight(v, ring, area, on);
        best = d;
        if (best == 0.0)
        {
            // On this triangle: no later one can be nearer, and a tie
            // keeps the first.
            break;
        }
    }
    return height;
}

double mnavSurfaceHeight(const mnavNavmesh* navmesh, int32_t slot, int32_t polygon, double x,
                         double z)
{
    const mnavSlot* s = &navmesh->slots[slot];
    mnavFrame f = mnavFrameOf(navmesh, s->x, s->z);
    Flat at = {(x - f.x0) / f.cell, (z - f.z0) / f.cell};
    return f.y0 + (DetailHeight(s->tile, polygon, at) - MNAV_HEIGHT_OFFSET) * f.height;
}

mnavPos3 mnavDetailWorld(const mnavNavmesh* navmesh, int32_t slot, const mnavDetailVertex* v)
{
    const mnavSlot* s = &navmesh->slots[slot];
    mnavFrame f = mnavFrameOf(navmesh, s->x, s->z);
    return (mnavPos3){f.x0 + (double)v->x / SUBCELLS * f.cell,
                      f.y0 + ((double)v->y - MNAV_HEIGHT_OFFSET) * f.height,
                      f.z0 + (double)v->z / SUBCELLS * f.cell};
}

// The search: the query, the box in world meters, and the best so far.
typedef struct Search
{
    const mnavNavmesh* navmesh;
    mnavPos3 point;
    mnavPos3 half;
    double step;
    double bestScore;
    double bestDistance;
    mnavNearest* result;
} Search;

// Whether polygon p of a tile can lie in the box: its vertices' bounds on
// the ground and its detail's heights.
static bool InBox(const Search* s, const mnavFrame* f, const mnavTile* tile, int32_t p)
{
    const mnavPolygon* polygon = &tile->mesh.polygons[p];
    double minX = (double)INFINITY;
    double maxX = -(double)INFINITY;
    double minZ = (double)INFINITY;
    double maxZ = -(double)INFINITY;
    for (int32_t k = 0; k < polygon->count; ++k)
    {
        const mnavMeshVertex* v = &tile->mesh.vertices[polygon->vertices[k]];
        minX = v->x < minX ? v->x : minX;
        maxX = v->x > maxX ? v->x : maxX;
        minZ = v->z < minZ ? v->z : minZ;
        maxZ = v->z > maxZ ? v->z : maxZ;
    }
    const mnavDetailPart* part = &tile->detail.parts[p];
    double minY = (double)INFINITY;
    double maxY = -(double)INFINITY;
    for (int32_t v = 0; v < part->vertexCount; ++v)
    {
        double y = tile->detail.vertices[part->firstVertex + v].y;
        minY = y < minY ? y : minY;
        maxY = y > maxY ? y : maxY;
    }
    double x0 = f->x0 + minX * f->cell;
    double x1 = f->x0 + maxX * f->cell;
    double z0 = f->z0 + minZ * f->cell;
    double z1 = f->z0 + maxZ * f->cell;
    double y0 = f->y0 + (minY - MNAV_HEIGHT_OFFSET) * f->height;
    double y1 = f->y0 + (maxY - MNAV_HEIGHT_OFFSET) * f->height;
    return x1 >= s->point.x - s->half.x && x0 <= s->point.x + s->half.x &&
           z1 >= s->point.z - s->half.z && z0 <= s->point.z + s->half.z &&
           y1 >= s->point.y - s->half.y && y0 <= s->point.y + s->half.y;
}

// Scores polygon p of the tile in slot when its nearest point lies in the
// box, and keeps it when it beats the best: by score, then distance; the
// first visited keeps ties.
static void Consider(Search* s, const mnavFrame* f, int32_t slot, const mnavTile* tile, int32_t p)
{
    const mnavPolygon* polygon = &tile->mesh.polygons[p];
    Flat ring[MNAV_POLYGON_VERTICES];
    for (int32_t k = 0; k < polygon->count; ++k)
    {
        const mnavMeshVertex* v = &tile->mesh.vertices[polygon->vertices[k]];
        ring[k] = (Flat){v->x, v->z};
    }
    Flat local = {(s->point.x - f->x0) / f->cell, (s->point.z - f->z0) / f->cell};
    bool over = false;
    Flat at = NearestOnRing(ring, polygon->count, local, &over);
    mnavPos3 point = {f->x0 + at.x * f->cell, 0.0, f->z0 + at.z * f->cell};
    double dx = s->point.x - point.x;
    double dz = s->point.z - point.z;
    // The height only adds to the distance, and rounding is monotone: a
    // point beside the polygon whose ground distance alone loses to the
    // best cannot win, so its detail height is not looked up.
    if (fabs(dx) > s->half.x || fabs(dz) > s->half.z || (!over && dx * dx + dz * dz > s->bestScore))
    {
        return;
    }
    point.y = f->y0 + (DetailHeight(tile, p, at) - MNAV_HEIGHT_OFFSET) * f->height;
    double dy = s->point.y - point.y;
    if (fabs(dy) > s->half.y)
    {
        return;
    }
    double distance = dx * dx + dy * dy + dz * dz;
    double beyond = fabs(dy) - s->step;
    double score = over ? (beyond > 0.0 ? beyond * beyond : 0.0) : distance;
    bool better = score < s->bestScore || (score == s->bestScore && distance < s->bestDistance);
    if (!better)
    {
        return;
    }
    s->bestScore = score;
    s->bestDistance = distance;
    s->result->polygon =
        (mnavPolygonId){(uint32_t)slot + 1, s->navmesh->slots[slot].generation, (uint32_t)p};
    s->result->point = point;
    s->result->over = over;
}

// The range of tile places the box covers on one axis, kept within the
// extent tiles can have.
static void TileRange(double low, double high, double origin, double size, int64_t reach,
                      int64_t* first, int64_t* last)
{
    double a = floor((low - origin) / size);
    double b = floor((high - origin) / size);
    a = a < (double)-reach ? (double)-reach : (a > (double)reach ? (double)reach : a);
    b = b < (double)-reach ? (double)-reach : (b > (double)reach ? (double)reach : b);
    *first = (int64_t)a;
    *last = (int64_t)b;
}

// The first place, sorted by x then z, with x at least x0.
static int32_t FirstColumn(const mnavNavmesh* navmesh, int64_t x0)
{
    int32_t low = 0;
    int32_t high = navmesh->placeCount;
    while (low < high)
    {
        int32_t middle = low + (high - low) / 2;
        if (navmesh->places[middle].x < x0)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low;
}

static bool Finite(double v)
{
    return isfinite(v);
}

// Whether a box is well formed: finite, with half sizes at least 0.
static bool GoodBox(mnavPos3 point, mnavVec3 half)
{
    return Finite(point.x) && Finite(point.y) && Finite(point.z) && half.x >= 0.0f &&
           half.y >= 0.0f && half.z >= 0.0f && isfinite(half.x) && isfinite(half.y) &&
           isfinite(half.z);
}

mnavCover mnavCoverOf(const mnavNavmesh* navmesh, mnavPos3 point, mnavPos3 half)
{
    const mnavBakeDef* def = &navmesh->def;
    double size = (double)def->tileCells * (double)def->cellSize;
    int64_t reach = MNAV_MAX_EXTENT_CELLS / def->tileCells + 1;
    mnavCover c = {0};
    TileRange(point.x - half.x, point.x + half.x, def->origin.x, size, reach, &c.x0, &c.x1);
    TileRange(point.z - half.z, point.z + half.z, def->origin.z, size, reach, &c.z0, &c.z1);
    return c;
}

mnavResult mnavFindPolygons(const mnavNavmesh* navmesh, const mnavQueryFilter* filter,
                            mnavPos3 center, mnavVec3 halfExtents, mnavPolygonId* polygonsOut,
                            int32_t capacity, mnavFound* foundOut)
{
    if (navmesh == nullptr || foundOut == nullptr || capacity < 0 ||
        (polygonsOut == nullptr && capacity > 0) || !GoodBox(center, halfExtents))
    {
        return mnav_errorInvalid;
    }
    const mnavQueryFilter* usable = nullptr;
    mnavResult checked = mnavCheckFilter(filter, &usable);
    if (checked != mnav_success)
    {
        return checked;
    }
    mnavPos3 half = {(double)halfExtents.x, (double)halfExtents.y, (double)halfExtents.z};
    Search s = {navmesh, center, half, 0.0, 0.0, 0.0, nullptr};
    mnavCover c = mnavCoverOf(navmesh, center, half);
    int64_t loaded = 0;
    int32_t count = 0;
    for (int32_t i = FirstColumn(navmesh, c.x0);
         i < navmesh->placeCount && navmesh->places[i].x <= c.x1; ++i)
    {
        const mnavPlace* place = &navmesh->places[i];
        if (place->z < c.z0 || place->z > c.z1)
        {
            continue;
        }
        loaded += 1;
        const mnavTile* tile = navmesh->slots[place->slot].tile;
        mnavFrame f = mnavFrameOf(navmesh, place->x, place->z);
        for (int32_t p = 0; p < tile->mesh.polygonCount; ++p)
        {
            if (!mnavIncludes(usable, tile->mesh.polygons[p].area) || !InBox(&s, &f, tile, p))
            {
                continue;
            }
            if (count < capacity)
            {
                polygonsOut[count] = (mnavPolygonId){
                    (uint32_t)place->slot + 1, navmesh->slots[place->slot].generation, (uint32_t)p};
            }
            count += 1;
        }
    }
    *foundOut = (mnavFound){count, loaded < (c.x1 - c.x0 + 1) * (c.z1 - c.z0 + 1)};
    return count > capacity ? mnav_errorCapacity : mnav_success;
}

mnavResult mnavFindNearest(const mnavNavmesh* navmesh, const mnavQueryFilter* filter,
                           mnavPos3 point, mnavVec3 halfExtents, mnavNearest* nearestOut)
{
    if (navmesh == nullptr || nearestOut == nullptr || !GoodBox(point, halfExtents))
    {
        return mnav_errorInvalid;
    }
    const mnavQueryFilter* usable = nullptr;
    mnavResult checked = mnavCheckFilter(filter, &usable);
    if (checked != mnav_success)
    {
        return checked;
    }
    *nearestOut = (mnavNearest){0};
    const mnavBakeDef* def = &navmesh->def;
    mnavPos3 half = {(double)halfExtents.x, (double)halfExtents.y, (double)halfExtents.z};
    Search s = {navmesh,
                point,
                half,
                (double)navmesh->cells.agentStep * (double)def->cellHeight,
                (double)INFINITY,
                (double)INFINITY,
                nearestOut};
    mnavCover c = mnavCoverOf(navmesh, point, half);
    int64_t loaded = 0;
    for (int32_t i = FirstColumn(navmesh, c.x0);
         i < navmesh->placeCount && navmesh->places[i].x <= c.x1; ++i)
    {
        const mnavPlace* place = &navmesh->places[i];
        if (place->z < c.z0 || place->z > c.z1)
        {
            continue;
        }
        loaded += 1;
        const mnavTile* tile = navmesh->slots[place->slot].tile;
        mnavFrame f = mnavFrameOf(navmesh, place->x, place->z);
        for (int32_t p = 0; p < tile->mesh.polygonCount; ++p)
        {
            if (mnavIncludes(usable, tile->mesh.polygons[p].area) && InBox(&s, &f, tile, p))
            {
                Consider(&s, &f, place->slot, tile, p);
            }
        }
    }
    nearestOut->incomplete = loaded < (c.x1 - c.x0 + 1) * (c.z1 - c.z0 + 1);
    return mnav_success;
}
