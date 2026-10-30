// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Values as the floats a target holds, past the floats' range the nearest
// one. A scale within the positive
// floats, the largest for one past them and the smallest for one below,
// so that no extrapolation, overshoot or repeat leaves it infinite or 0;
// a number within the finite floats, as CSS converts a value past its
// range to the closest it supports; a velocity likewise, taken from the
// values held, so a value held at a bound is still.

#ifndef MAUL_TWEEN_SRC_BOUNDS_INTERNAL_H
#define MAUL_TWEEN_SRC_BOUNDS_INTERNAL_H

#include "maul-tween/base.h"

static inline float mtwnScaleFloat(double scale)
{
    return scale >= 0x1.fffffep127 ? 0x1.fffffep127f
                                   : (scale <= 0x1p-149 ? 0x1p-149f : (float)scale);
}

// A scale as the target holds it, still in double: within the floats'
// range, its bounds past it. In range it is unchanged, bit for bit.
static inline double mtwnScaleHeld(double scale)
{
    return scale >= 0x1.fffffep127 ? 0x1.fffffep127 : (scale <= 0x1p-149 ? 0x1p-149 : scale);
}

// A number as the target holds it, still in double: within the finite
// floats, the nearest past them; in range unchanged, bit for bit. Written
// as a minimum then a maximum whose operands' order makes each one
// instruction on x86 (minsd, maxsd) and other targets alike, so a NaN,
// which no finite input makes, is held at the largest.
static inline double mtwnNumberHeld(double number)
{
    double below = number < 0x1.fffffep127 ? number : 0x1.fffffep127;
    return below > -0x1.fffffep127 ? below : -0x1.fffffep127;
}

// A number's channels, converted to floats already, held within the finite
// floats: the same as holding the doubles first, a double past the largest
// float having converted to an infinity. Four channels alike, which a
// compiler holds in two vector instructions after a packed conversion.
static inline mtwnVec4 mtwnNumbersHeld(mtwnVec4 v)
{
    float c[4] = {v.x, v.y, v.z, v.w};
    for (int i = 0; i < 4; ++i)
    {
        c[i] = c[i] < 0x1.fffffep127f ? c[i] : 0x1.fffffep127f;
        c[i] = c[i] > -0x1.fffffep127f ? c[i] : -0x1.fffffep127f;
    }
    return (mtwnVec4){c[0], c[1], c[2], c[3]};
}

static inline float mtwnVelocityFloat(double velocity)
{
    return velocity >= 0x1.fffffep127
               ? 0x1.fffffep127f
               : (velocity <= -0x1.fffffep127 ? -0x1.fffffep127f : (float)velocity);
}

#endif
