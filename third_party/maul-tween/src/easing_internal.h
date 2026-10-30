// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The easings' commonest case inline, for the update's hot loop:
// an integer power already normal, by the general path's
// arithmetic, so the same bits.

#ifndef MAUL_TWEEN_SRC_EASING_INTERNAL_H
#define MAUL_TWEEN_SRC_EASING_INTERNAL_H

#include "work.h"

#include "maul-tween/easing.h"

#include <stdbool.h>
#include <stdint.h>

// Where the normalization's numbers sit in parameters[].
enum
{
    MTWN_EASING_START = 3,
    MTWN_EASING_RANGE = 4
};

// x^n for n >= 1 by binary powering.
static inline double mtwnIntegerPower(double x, uint32_t n)
{
    double result = 1.0;
    double square = x;
    while (n > 0)
    {
        if (n & 1u)
        {
            result *= square;
        }
        square *= square;
        n >>= 1;
    }
    return result;
}

// Whether an easing is an integer power whose raw ends are 0 and 1.
static inline bool mtwnIsNormalPower(const mtwnEasing* easing)
{
    return easing->kind == mtwn_easingPower && easing->count > 0 &&
           easing->parameters[MTWN_EASING_START] == 0.0 &&
           easing->parameters[MTWN_EASING_RANGE] == 1.0;
}

// An integer power n already normal at a progress, clamped, by a mode.
static inline double mtwnEasePower(uint32_t n, mtwnEaseMode mode, double progress)
{
    double p = progress < 0.0 ? 0.0 : (progress > 1.0 ? 1.0 : progress);
    switch (mode)
    {
    case mtwn_easeIn:
        return mtwnIntegerPower(p, n);
    case mtwn_easeOut:
        return 1.0 - mtwnIntegerPower(1.0 - p, n);
    case mtwn_easeInOut:
        return p < 0.5 ? 0.5 * mtwnIntegerPower(2.0 * p, n)
                       : 0.5 + 0.5 * (1.0 - mtwnIntegerPower(1.0 - (2.0 * p - 1.0), n));
    default:
        return p < 0.5 ? 0.5 * (1.0 - mtwnIntegerPower(1.0 - 2.0 * p, n))
                       : 0.5 + 0.5 * mtwnIntegerPower(2.0 * p - 1.0, n);
    }
}

// Such a power easing at a progress.
static inline double mtwnEaseNormalPower(const mtwnEasing* easing, double progress)
{
    return mtwnEasePower(easing->count, easing->mode, progress);
}

// An easing at a progress: the inline case, or the general path.
static inline double mtwnEaseFast(const mtwnEasing* easing, double progress)
{
    return mtwnIsNormalPower(easing) ? mtwnEaseNormalPower(easing, progress)
                                     : mtwnSampleEasingWork(easing, progress, false);
}

#endif
