// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The library's own scalar functions. Results must be the same bits on
// every platform, so nothing here calls a libm function other than the
// exactly rounded ones (floor, ceil, fabs, sqrt).

#ifndef MAUL_NAV_SRC_SCALAR_H
#define MAUL_NAV_SRC_SCALAR_H

#include <stdbool.h>
#include <stdint.h>

// The cosine of an angle in degrees, from 0 to 90, to within a few units
// in the last place.
float mnavCosDegrees(float degrees);

// Converts meters to whole cells (mnav-0002): a quotient within 2^-10 of an
// integer counts as that integer; otherwise it rounds up or down. Returns
// false when the quotient exceeds max. meters and cell must be finite,
// meters at least 0 and cell positive.
bool mnavToCells(float meters, float cell, bool roundUp, int32_t max, int32_t* cellsOut);

#endif // MAUL_NAV_SRC_SCALAR_H
