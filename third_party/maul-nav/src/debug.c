// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Debug geometry of navmeshes, links, paths and corridors (mnav-0010).

#include "maul-nav/debug.h"

#include "draw.h"
#include "navmesh.h"
#include "nearest.h"

#include "maul-nav/base.h"
#include "maul-nav/draw.h"
#include "maul-nav/navmesh.h"
#include "maul-nav/query.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

// The lines an off-mesh link's arc is drawn with.
#define ARC_LINES 8

// Marks a link drawn detached or disabled.
#define LINK_OFF 256

// A polygon's detail triangles, of a kind and value.
static void Fill(mnavDebugBuffer* buffer, const mnavNavmesh* navmesh, int32_t slot, int32_t p,
                 mnavDebugKind kind, uint16_t value)
{
    const mnavTile* tile = navmesh->slots[slot].tile;
    const mnavDetailPart* part = &tile->detail.parts[p];
    const mnavDetailVertex* vertices = &tile->detail.vertices[part->firstVertex];
    for (int32_t t = 0; t < part->triangleCount; ++t)
    {
        const mnavDetailTriangle* triangle = &tile->detail.triangles[part->firstTriangle + t];
        mnavDrawTriangle(buffer, mnavDetailWorld(navmesh, slot, &vertices[triangle->corners[0]]),
                         mnavDetailWorld(navmesh, slot, &vertices[triangle->corners[1]]),
                         mnavDetailWorld(navmesh, slot, &vertices[triangle->corners[2]]), kind,
                         value);
    }
}

// Whether tile links join edge j of polygon p.
static bool Linked(const mnavTile* tile, int32_t p, int32_t j)
{
    for (int32_t l = tile->firstLink[p]; l < tile->firstLink[p + 1]; ++l)
    {
        if (tile->links[l].edge == j)
        {
            return true;
        }
    }
    return false;
}

// A polygon's edges as lines: an inner edge from its lower polygon only.
static void Edges(mnavDebugBuffer* buffer, const mnavNavmesh* navmesh, int32_t slot, int32_t p)
{
    const mnavSlot* s = &navmesh->slots[slot];
    const mnavTile* tile = s->tile;
    const mnavPolygon* polygon = &tile->mesh.polygons[p];
    mnavFrame f = mnavFrameOf(navmesh, s->x, s->z);
    for (int32_t j = 0; j < polygon->count; ++j)
    {
        mnavPos3 a = mnavVertexWorld(&f, &tile->mesh.vertices[polygon->vertices[j]]);
        mnavPos3 b =
            mnavVertexWorld(&f, &tile->mesh.vertices[polygon->vertices[(j + 1) % polygon->count]]);
        int32_t next = polygon->neighbors[j];
        if (next != MNAV_NO_INDEX)
        {
            if (next > p)
            {
                mnavDrawLine(buffer, a, b, mnav_debugInnerEdge, 0);
            }
        }
        else if (polygon->sides[j] != 0)
        {
            mnavDrawLine(buffer, a, b, mnav_debugTileSide, Linked(tile, p, j) ? 1 : 0);
        }
        else
        {
            mnavDrawLine(buffer, a, b, mnav_debugWall, 0);
        }
    }
}

// A tile's bounds on the ground at its lowest detail height.
static void Bounds(mnavDebugBuffer* buffer, const mnavNavmesh* navmesh, int32_t slot)
{
    const mnavSlot* s = &navmesh->slots[slot];
    const mnavDetailMesh* detail = &s->tile->detail;
    double low = (double)INFINITY;
    for (int32_t v = 0; v < detail->vertexCount; ++v)
    {
        double y = mnavDetailWorld(navmesh, slot, &detail->vertices[v]).y;
        low = y < low ? y : low;
    }
    low = isfinite(low) ? low : 0.0;
    mnavFrame f = mnavFrameOf(navmesh, s->x, s->z);
    double side = (double)navmesh->def.tileCells * f.cell;
    mnavPos3 c[4] = {{f.x0, low, f.z0},
                     {f.x0 + side, low, f.z0},
                     {f.x0 + side, low, f.z0 + side},
                     {f.x0, low, f.z0 + side}};
    for (int32_t k = 0; k < 4; ++k)
    {
        mnavDrawLine(buffer, c[k], c[(k + 1) % 4], mnav_debugTileBounds, 0);
    }
}

mnavResult mnavDebugNavmesh(const mnavNavmesh* navmesh, int32_t tileX0, int32_t tileZ0,
                            int32_t tileX1, int32_t tileZ1, mnavDebugBuffer* buffer)
{
    if (navmesh == nullptr || !mnavGoodBuffer(buffer) || tileX1 < tileX0 || tileZ1 < tileZ0)
    {
        return mnav_errorInvalid;
    }
    for (int32_t i = 0; i < navmesh->placeCount; ++i)
    {
        const mnavPlace* place = &navmesh->places[i];
        if (place->x < tileX0 || place->x > tileX1 || place->z < tileZ0 || place->z > tileZ1)
        {
            continue;
        }
        const mnavTile* tile = navmesh->slots[place->slot].tile;
        for (int32_t p = 0; p < tile->mesh.polygonCount; ++p)
        {
            Fill(buffer, navmesh, place->slot, p, mnav_debugPolygon, tile->mesh.polygons[p].area);
            Edges(buffer, navmesh, place->slot, p);
        }
        Bounds(buffer, navmesh, place->slot);
    }
    return mnavDrawResult(buffer);
}

mnavResult mnavDebugLinks(const mnavNavmesh* navmesh, mnavDebugBuffer* buffer)
{
    if (navmesh == nullptr || !mnavGoodBuffer(buffer))
    {
        return mnav_errorInvalid;
    }
    for (int32_t i = 0; i < navmesh->linkSlots; ++i)
    {
        const mnavOffLink* link = &navmesh->links[i];
        if (link->phase != MNAV_LINK_LIVE && link->phase != MNAV_LINK_REMOVING)
        {
            continue;
        }
        const mnavLinkState* state = &link->state;
        mnavPos3 a = state->attached ? state->start : link->def.start;
        mnavPos3 b = state->attached ? state->end : link->def.end;
        double dx = b.x - a.x;
        double dy = b.y - a.y;
        double dz = b.z - a.z;
        double rise = sqrt(dx * dx + dy * dy + dz * dz) * 0.25;
        mnavDebugKind kind = link->def.twoWay ? mnav_debugLinkBothWays : mnav_debugLink;
        uint16_t value =
            (uint16_t)(link->def.kind + (state->attached && state->enabled ? 0 : LINK_OFF));
        mnavPos3 last = a;
        for (int32_t k = 1; k <= ARC_LINES; ++k)
        {
            double t = (double)k / ARC_LINES;
            mnavPos3 p = {a.x + t * dx, a.y + t * dy + 4.0 * rise * t * (1.0 - t), a.z + t * dz};
            mnavDrawLine(buffer, last, p, kind, value);
            last = p;
        }
    }
    return mnavDrawResult(buffer);
}

mnavResult mnavDebugPath(const mnavPath* path, mnavDebugBuffer* buffer)
{
    if (path == nullptr || !mnavGoodBuffer(buffer) || path->pointCount < 0 ||
        (path->pointCount > 0 && path->points == nullptr))
    {
        return mnav_errorInvalid;
    }
    for (int32_t i = 1; i < path->pointCount; ++i)
    {
        mnavDrawLine(buffer, path->points[i - 1], path->points[i], mnav_debugPath, 0);
    }
    return mnavDrawResult(buffer);
}

mnavResult mnavDebugCorridor(const mnavNavmesh* navmesh, const mnavPolygonId* polygons,
                             int32_t count, mnavDebugBuffer* buffer)
{
    if (navmesh == nullptr || !mnavGoodBuffer(buffer) || count < 0 ||
        (count > 0 && polygons == nullptr))
    {
        return mnav_errorInvalid;
    }
    for (int32_t i = 0; i < count; ++i)
    {
        mnavResult result = mnavCheckPolygon(navmesh, polygons[i]);
        if (result == mnav_errorInvalid)
        {
            return result;
        }
        if (result == mnav_success)
        {
            Fill(buffer, navmesh, (int32_t)polygons[i].slot - 1, (int32_t)polygons[i].polygon,
                 mnav_debugCorridor, (uint16_t)(i & 0xFFFF));
        }
    }
    return mnavDrawResult(buffer);
}
