// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A path's sample with its work counted, for its tests; its segments as
// cubics, for its direction.

#ifndef MAUL_TWEEN_SRC_PATH_INTERNAL_H
#define MAUL_TWEEN_SRC_PATH_INTERNAL_H

#include "path_length.h"

#include "maul-tween/path.h"

#include <stdint.h>

// The segment a progress falls in, its parameter there and its cubic, as
// the path's sample finds them.
void mtwnLocatePath(const mtwnPath* path, double progress, uint32_t* segmentOut, double* uOut,
                    mtwnCubic* cubic);

// Segment i's cubic.
void mtwnPathSegmentCubic(const mtwnPath* path, uint32_t i, mtwnCubic* cubic);

// Whether a sample of a valid path at a progress reads what a maker gives:
// the progress finite, and the segment it falls in of a known kind and,
// timed by its keyframes, eased by a maker's easing.
bool mtwnPathReadsValid(const mtwnPath* path, double progress);

#endif // MAUL_TWEEN_SRC_PATH_INTERNAL_H
