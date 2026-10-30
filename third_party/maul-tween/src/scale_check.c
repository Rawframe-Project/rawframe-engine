// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A scale mix's check and its checked sample, apart from the mix itself
// (scale.c), so a program of transitions, which scales through the
// worker, links neither.

#include "fpenv.h"
#include "valid.h"
#include "work.h"

#include "maul-tween/interpolate.h"

#include <math.h>
#include <stdbool.h>

static bool IsScale(double scale)
{
    return isfinite(scale) && scale > 0.0;
}

MTWN_NOINLINE bool mtwnValidScaleMix(const mtwnScaleMix* mix)
{
    return mix != NULL && IsScale(mix->from) && IsScale(mix->to) && isfinite(mix->logFrom) &&
           isfinite(mix->logRatio);
}

static MTWN_NOINLINE mtwnResult mtwnMixScaleChecked(const mtwnScaleMix* mix, double progress,
                                                    double* scaleOut)
{
    if (scaleOut == NULL || !mtwnValidScaleMix(mix) || !isfinite(progress))
    {
        return mtwn_errorInvalid;
    }
    *scaleOut = mtwnMixScaleWork(mix, progress);
    return mtwn_success;
}

mtwnResult mtwnMixScale(const mtwnScaleMix* mix, double progress, double* scaleOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnMixScaleChecked(mix, progress, scaleOut);
    mtwnFloatEnvLeave(env);
    return result;
}
