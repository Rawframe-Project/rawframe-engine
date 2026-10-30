// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A color mix's check and its checked sample, apart from the conversions
// and the mix itself (color.c), so a program of transitions, which mixes
// colors through the worker, links neither.

#include "color_internal.h"
#include "fpenv.h"
#include "valid.h"
#include "work.h"

#include "generated/math_constants.h"
#include "maul-tween/interpolate.h"

#include <math.h>
#include <stdbool.h>

// A turn, in radians.
static const double s_turn = 4.0 * MTWN_PIO2_HI;

// A mix's end in its space: finite coordinates, premultiplied or not,
// its alpha its color's, and an OkLCh hue within the two turns the
// fix-ups reach from a hue of 0 to a turn.
static MTWN_NOINLINE bool IsEnd(const double end[4], mtwnColor color, mtwnColorSpace space)
{
    return isfinite(end[0]) && isfinite(end[1]) && isfinite(end[2]) &&
           end[3] == (double)color.alpha &&
           (space != mtwn_colorOklch || (end[2] >= 0.0 && end[2] <= 2.0 * s_turn));
}

MTWN_NOINLINE bool mtwnValidColorMix(const mtwnColorMix* mix)
{
    return mix != NULL && mtwnIsColorSpace(mix->space) && mtwnIsColor(mix->fromColor) &&
           mtwnIsColor(mix->toColor) && IsEnd(mix->from, mix->fromColor, mix->space) &&
           IsEnd(mix->to, mix->toColor, mix->space);
}

static MTWN_NOINLINE mtwnResult mtwnMixColorChecked(const mtwnColorMix* mix, double progress,
                                                    mtwnColor* colorOut)
{
    if (colorOut == NULL || !mtwnValidColorMix(mix) || !isfinite(progress))
    {
        return mtwn_errorInvalid;
    }
    *colorOut = mtwnMixColorWork(mix, progress);
    return mtwn_success;
}

mtwnResult mtwnMixColor(const mtwnColorMix* mix, double progress, mtwnColor* colorOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnMixColorChecked(mix, progress, colorOut);
    mtwnFloatEnvLeave(env);
    return result;
}
