// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Scales. A scale mix moves in the logarithm: from
// exp(p log(to / from)), the ratio taken as a difference of logarithms so
// it cannot overflow, and the whole as exp(log from + p r) where the
// product would leave the doubles' range on the way. Apart from the zoom
// (zoom.c), so a program scaling links none of it.

#include "elementary.h"
#include "fpenv.h"
#include "work.h"

#include "maul-tween/interpolate.h"

#include <math.h>

// Where exp(x) is still finite and above the smallest normal.
static const double s_expRange = 700.0;

MTWN_NOINLINE mtwnResult mtwnMakeScaleMixWork(double from, double to, mtwnScaleMix* mixOut)
{
    if (mixOut == NULL || !isfinite(from) || !isfinite(to) || !(from > 0.0) || !(to > 0.0))
    {
        return mtwn_errorInvalid;
    }
    double logFrom = mtwnLog(from);
    *mixOut = (mtwnScaleMix){from, to, logFrom, mtwnLog(to) - logFrom};
    return mtwn_success;
}

mtwnResult mtwnMakeScaleMix(double from, double to, mtwnScaleMix* mixOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnMakeScaleMixWork(from, to, mixOut);
    mtwnFloatEnvLeave(env);
    return result;
}

MTWN_NOINLINE double mtwnMixScaleWork(const mtwnScaleMix* mix, double progress)
{
    if (progress == 0.0)
    {
        return mix->from;
    }
    if (progress == 1.0)
    {
        return mix->to;
    }
    double exponent = progress * mix->logRatio;
    // The product keeps from's digits; the sum of logarithms only where the
    // factor alone would overflow or underflow.
    return fabs(exponent) < s_expRange ? mix->from * mtwnExp(exponent)
                                       : mtwnExp(mix->logFrom + exponent);
}
