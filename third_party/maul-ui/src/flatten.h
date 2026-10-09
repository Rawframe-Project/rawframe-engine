// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// FreeType outlines as line segments (record mui-0006): curves are cut
// into as many segments as Wang's formula asks for them to stay within
// MUI_FLATTEN_TOLERANCE of the curve.

#ifndef MAUL_UI_SRC_FLATTEN_H
#define MAUL_UI_SRC_FLATTEN_H

#include "distance_field.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <stdbool.h>
#include <stdint.h>

// How far a segment may stray from its curve, in pixels.
#define MUI_FLATTEN_TOLERANCE (1.0f / 32.0f)

// Writes the segments of an outline in 26.6 pixels, as pixels, up to
// capacity of them (segments may be NULL when capacity is 0), and gives
// how many there are; false for an outline FreeType cannot walk. curves,
// when not NULL, receives for each segment the index of the line or curve
// of the outline it is part of, counted from 0.
bool muiFlattenOutline(const FT_Outline* outline, muiSegment* segments, uint32_t* curves,
                       uint32_t capacity, uint32_t* countOut);

#endif // MAUL_UI_SRC_FLATTEN_H
