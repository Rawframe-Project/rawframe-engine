// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a color's maker and its mix's check both take: a known space, and a
// color finite with its alpha from 0 to 1.

#ifndef MAUL_TWEEN_SRC_COLOR_INTERNAL_H
#define MAUL_TWEEN_SRC_COLOR_INTERNAL_H

#include "maul-tween/interpolate.h"

#include <math.h>
#include <stdbool.h>

static inline bool mtwnIsColorSpace(mtwnColorSpace space)
{
    return space <= mtwn_colorSrgb;
}

static inline bool mtwnIsColor(mtwnColor color)
{
    return isfinite(color.red) && isfinite(color.green) && isfinite(color.blue) &&
           color.alpha >= 0.0f && color.alpha <= 1.0f;
}

#endif
