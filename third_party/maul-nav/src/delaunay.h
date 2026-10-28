// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A small triangulation kept Delaunay by edge flips, with exact integer
// predicates, for the detail mesh.

#ifndef MAUL_NAV_SRC_DELAUNAY_H
#define MAUL_NAV_SRC_DELAUNAY_H

#include <stdint.h>

// x and z in sixteenths of a cell of the tile without its border, y in
// offset cell heights; x and z from 0 to 2^14.
typedef struct mnavDetailVertex
{
    int32_t x;
    int32_t y;
    int32_t z;
} mnavDetailVertex;

// Three corners, indices into the polygon's part of the vertices, wound
// like the polygons.
typedef struct mnavDetailTriangle
{
    uint8_t corners[3];
} mnavDetailTriangle;

// The triangles over a set of vertices, with room for capacity triangles
// and a stack of stackCapacity edges for the flips. Each triangle written
// is marked in changed, which holds capacity entries.
typedef struct mnavTriangulation
{
    const mnavDetailVertex* vertices;
    mnavDetailTriangle* triangles;
    int32_t count;
    int32_t capacity;
    int32_t* stack;
    int32_t stackCapacity;
    uint8_t* changed;
} mnavTriangulation;

// Twice the signed area of (a, b, c) on the ground; the triangles are
// negative.
int64_t mnavDetailArea2(const mnavDetailVertex* a, const mnavDetailVertex* b,
                        const mnavDetailVertex* c);

// Flips edges, triangle by triangle and edge by edge in order, until no
// edge between two triangles has the far corner of one strictly inside
// the other's circumcircle; such an edge always lies across a strictly
// convex quadrilateral, so every flip keeps the triangles wound alike.
void mnavMakeDelaunay(mnavTriangulation* triangulation);

// The first triangle holding (x, z) inside or on an edge, which one of
// them does; edge receives the corner starting the edge it lies on, or -1
// when inside.
int32_t mnavLocate(const mnavTriangulation* triangulation, int32_t x, int32_t z, int32_t* edge);

// Inserts vertex v, which lies in triangle t (inside, or on the edge from
// corner edge when edge is at least 0), and restores the Delaunay
// property round it. v lies inside the triangles' outline, never on it,
// so an edge it lies on has a triangle across; the triangles have room
// for two more and the stack for four.
void mnavInsertVertex(mnavTriangulation* triangulation, int32_t v, int32_t t, int32_t edge);

#endif // MAUL_NAV_SRC_DELAUNAY_H
