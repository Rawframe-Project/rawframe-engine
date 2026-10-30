// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A rotation mix's check and its checked sample, apart from the slerp
// itself (rotation.c), so a program of transitions, which slerps through
// the worker, links neither.

#include "fpenv.h"
#include "valid.h"
#include "work.h"

#include "generated/math_constants.h"
#include "maul-tween/interpolate.h"

#include <math.h>
#include <stdbool.h>

// Half a turn, pi, in radians.
static const double s_halfTurn = 2.0 * MTWN_PIO2_HI;
// How far a normalized end's squared length, as mtwnValidRotationMix sums
// it, can be from 1, in units of u = 2^-53. rotation.c's Normalize squares
// a float's components exactly, sums them with three roundings (within
// 3u), takes the root (u, so the norm is within 2.5u) and divides each
// component (u each): the true squared length of what it stores is within
// (1 + u)^2 / ((1 - 3u) (1 - u)^2), about 7u, of 1. The check's own four
// products and three sums add 4u: 11u and terms in u^2, so 12u.
static const double s_unitTolerance = 12.0 * 0x1.0p-53;

// An end of unit length to Normalize's rounding, stored as its floats.
static MTWN_NOINLINE bool IsUnitEnd(const double end[4], mtwnQuat rotation)
{
    double squared = end[0] * end[0] + end[1] * end[1] + end[2] * end[2] + end[3] * end[3];
    return fabs(squared - 1.0) <= s_unitTolerance && rotation.x == (float)end[0] &&
           rotation.y == (float)end[1] && rotation.z == (float)end[2] &&
           rotation.w == (float)end[3];
}

MTWN_NOINLINE bool mtwnValidRotationMix(const mtwnRotationMix* mix)
{
    return mix != NULL && IsUnitEnd(mix->from, mix->fromRotation) &&
           IsUnitEnd(mix->to, mix->toRotation) && mix->angle >= 0.0 && mix->angle <= s_halfTurn;
}

static MTWN_NOINLINE mtwnResult mtwnMixRotationChecked(const mtwnRotationMix* mix, double progress,
                                                       mtwnQuat* rotationOut)
{
    if (rotationOut == NULL || !mtwnValidRotationMix(mix) || !isfinite(progress))
    {
        return mtwn_errorInvalid;
    }
    *rotationOut = mtwnMixRotationWork(mix, progress);
    return mtwn_success;
}

mtwnResult mtwnMixRotation(const mtwnRotationMix* mix, double progress, mtwnQuat* rotationOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnMixRotationChecked(mix, progress, rotationOut);
    mtwnFloatEnvLeave(env);
    return result;
}
