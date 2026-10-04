// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Exact tests on contour vertices on the ground plane (x, z): orientation,
// segment intersection and cones, in 64-bit integers.

#ifndef MAUL_NAV_SRC_PLANAR_H
#define MAUL_NAV_SRC_PLANAR_H

#include "contour.h"

#include <stdint.h>

// Twice the signed area of triangle (a, b, c).
int64_t mnavArea2(const mnavContourVertex* a, const mnavContourVertex* b,
                  const mnavContourVertex* c);

// Whether c lies strictly left of the directed line a to b (negative
// area), or left or on it.
bool mnavLeft(const mnavContourVertex* a, const mnavContourVertex* b, const mnavContourVertex* c);
bool mnavLeftOn(const mnavContourVertex* a, const mnavContourVertex* b, const mnavContourVertex* c);

bool mnavSameGround(const mnavContourVertex* a, const mnavContourVertex* b);

// Whether segments ab and cd cross at a point inside both.
bool mnavIntersectProper(const mnavContourVertex* a, const mnavContourVertex* b,
                         const mnavContourVertex* c, const mnavContourVertex* d);

// Whether segments ab and cd meet at all, an end or a collinear overlap
// included.
bool mnavIntersect(const mnavContourVertex* a, const mnavContourVertex* b,
                   const mnavContourVertex* c, const mnavContourVertex* d);

// Whether p lies in the cone at vertex at between prev and next; loose
// counts the cone's edges as inside at a convex vertex.
bool mnavInCone(const mnavContourVertex* prev, const mnavContourVertex* at,
                const mnavContourVertex* next, const mnavContourVertex* p, bool loose);

#endif // MAUL_NAV_SRC_PLANAR_H
