// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Clipping a polygon on the ground to a circle, for the random point
// near a center.

#ifndef MAUL_NAV_SRC_CLIP_H
#define MAUL_NAV_SRC_CLIP_H

#include "polymesh.h"

#include "maul-nav/base.h"
#include "maul-nav/query.h"

#include <stdint.h>

// The corners a polygon clipped to the 32-gon has in exact arithmetic:
// each of the 32 cuts adds at most one. Rounding can add a second at a
// cut, so the clip checks its room at every write.
#define MNAV_CLIP_CORNERS MNAV_MAX_CLIP_CORNERS
static_assert(MNAV_CLIP_CORNERS == MNAV_POLYGON_VERTICES + 32, "a cut adds a corner");

// Clips the convex polygon of count corners, at most
// MNAV_POLYGON_VERTICES, to the 32-gon inscribed in the circle on the
// ground, Sutherland and Hodgman's way, into out, which holds capacity
// corners. Returns the corners left, 0 for a circle too small to hold
// area, or -1 when they would pass capacity or MNAV_CLIP_CORNERS, with
// out then unspecified.
int32_t mnavClipToCircle(const mnavPos3* corners, int32_t count, mnavPos3 center, double radius,
                         mnavPos3* out, int32_t capacity);

#endif // MAUL_NAV_SRC_CLIP_H
