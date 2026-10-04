// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Debug geometry (mnav-0010): the buffer debug functions append plain
// vertex and index data to, for any renderer; the library never draws.

#ifndef MAUL_NAV_DRAW_H
#define MAUL_NAV_DRAW_H

#include "maul-nav/base.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // What a debug vertex shows; its value says more.
    typedef uint16_t mnavDebugKind;

    enum
    {
        // A navmesh polygon's fill; the value is its area type.
        mnav_debugPolygon = 0,
        // An edge between two polygons of a tile.
        mnav_debugInnerEdge = 1,
        // An edge with nothing across: a wall or a ledge.
        mnav_debugWall = 2,
        // An edge on a tile side; the value is 1 where tile links join it.
        mnav_debugTileSide = 3,
        // A tile's bounds on the ground.
        mnav_debugTileBounds = 4,
        // An off-mesh link one way, and one both ways; the value is its
        // kind, plus 256 when it is detached or disabled.
        mnav_debugLink = 5,
        mnav_debugLinkBothWays = 6,
        // A path's straight line.
        mnav_debugPath = 7,
        // A corridor polygon's fill; the value is its place in the
        // corridor, modulo 65536.
        mnav_debugCorridor = 8,
        // A flow field's arrow.
        mnav_debugFlow = 9,
        // An avoidance agent's outline, and the line to a neighbour.
        mnav_debugAgent = 10,
        mnav_debugNeighbor = 11,
    };

    // A vertex: its place relative to the buffer's origin, what it shows
    // and a value.
    typedef struct mnavDebugVertex
    {
        float x;
        float y;
        float z;
        mnavDebugKind kind;
        uint16_t value;
    } mnavDebugVertex;

    // A caller's debug buffer: vertices, and triangle and line lists of
    // indices into them. Debug calls append, counting past a capacity
    // without writing there, so the counts say what a full draw needs.
    typedef struct mnavDebugBuffer
    {
        // Subtracted from every position before it becomes a float.
        mnavPos3 origin;
        mnavDebugVertex* vertices;
        int32_t vertexCapacity;
        int32_t vertexCount;
        // Three indices a triangle.
        uint32_t* triangles;
        int32_t triangleCapacity;
        int32_t triangleCount;
        // Two indices a line.
        uint32_t* lines;
        int32_t lineCapacity;
        int32_t lineCount;
    } mnavDebugBuffer;

#ifdef __cplusplus
}
#endif

#endif // MAUL_NAV_DRAW_H
