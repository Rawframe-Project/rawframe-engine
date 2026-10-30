// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Angles and rotations. An angle's far
// end moves by whole turns as CSS's hue methods move a hue. A rotation
// mix holds its ends normalized in double and Kahan's angle between
// them, 2 atan2(|q0 - q1|, |q0 + q1|), accurate at every angle where acos
// of the dot product loses half its digits near 0; slerp's weights are
// written with sinc, sin(x) / x, which keeps its digits at any x but 0,
// as the sine does, so a tiny angle keeps its digits too. The weights of
// unit ends give a unit result to an ulp of double, far below a float's,
// so it is not normalized again.

#include "bounds_internal.h"
#include "elementary.h"
#include "fpenv.h"
#include "mix_internal.h"
#include "work.h"

#include "generated/math_constants.h"
#include "maul-tween/interpolate.h"

#include <math.h>
#include <stdbool.h>

static const double s_turn = 4.0 * MTWN_PIO2_HI;
static const double s_halfTurn = 2.0 * MTWN_PIO2_HI;
// Below this |q0 + q1| the longest way has no defined axis.
static const double s_degenerate = 1e-6;
// A phase from 2^52 radians on has an ulp of a radian or more, so it holds
// no angle: a slerp extrapolated that far is taken at its phase reduced by
// whole turns exactly (fmod), an infinite one's at 0, so it stays a unit
// rotation.
static const double s_phaseLost = 0x1.0p52;

// The difference to - from moved by whole turns as the method asks.
static double TurnedDifference(double difference, mtwnAngleMethod method)
{
    switch (method)
    {
    case mtwn_angleShorter:
    case mtwn_angleLonger:
    {
        double shorter = difference;
        if (shorter > s_halfTurn || shorter < -s_halfTurn)
        {
            shorter -= s_turn * floor(shorter / s_turn + 0.5);
        }
        if (method == mtwn_angleShorter)
        {
            return shorter;
        }
        // CSS's longer: from (0, pi) back a turn, from (-pi, 0] on one.
        if (shorter > 0.0 && shorter < s_halfTurn)
        {
            return shorter - s_turn;
        }
        if (shorter > -s_halfTurn && shorter <= 0.0)
        {
            return shorter + s_turn;
        }
        return shorter;
    }
    case mtwn_angleIncreasing:
        return difference - s_turn * floor(difference / s_turn);
    case mtwn_angleDecreasing:
        return difference - s_turn * ceil(difference / s_turn);
    default:
        return difference;
    }
}

MTWN_NOINLINE double mtwnMixAngleWork(double from, double to, mtwnAngleMethod method,
                                      double progress)
{
    double difference = to - from;
    double turned = TurnedDifference(difference, method);
    // The end moved by the whole turns the method added, exact when none.
    double turns = floor((turned - difference) / s_turn + 0.5);
    double end = to + turns * s_turn;
    return mtwnMix(from, end, progress);
}

static MTWN_NOINLINE mtwnResult mtwnMixAngleChecked(double from, double to, mtwnAngleMethod method,
                                                    double progress, double* angleOut)
{
    if (angleOut == NULL || !mtwnIsAngleMethod(method) || !isfinite(from) || !isfinite(to) ||
        !isfinite(progress))
    {
        return mtwn_errorInvalid;
    }
    *angleOut = mtwnMixAngleWork(from, to, method, progress);
    return mtwn_success;
}

mtwnResult mtwnMixAngle(double from, double to, mtwnAngleMethod method, double progress,
                        double* angleOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnMixAngleChecked(from, to, method, progress, angleOut);
    mtwnFloatEnvLeave(env);
    return result;
}

// sin(x) / x.
static double Sinc(double x)
{
    if (x == 0.0)
    {
        return 1.0;
    }
    double sine = 0.0;
    double cosine = 0.0;
    mtwnSinCos(x, &sine, &cosine);
    return sine / x;
}

// Normalizes q into out in double: false for a q not finite or zero.
static bool Normalize(mtwnQuat q, double out[4])
{
    double v[4] = {(double)q.x, (double)q.y, (double)q.z, (double)q.w};
    double norm = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3]);
    if (!isfinite(norm) || norm == 0.0)
    {
        return false;
    }
    for (int i = 0; i < 4; ++i)
    {
        out[i] = v[i] / norm;
    }
    return true;
}

MTWN_NOINLINE mtwnResult mtwnMakeRotationMixWork(mtwnQuat from, mtwnQuat to, mtwnRotationPath path,
                                                 mtwnRotationMix* mixOut)
{
    mtwnRotationMix mix;
    if (mixOut == NULL || path > mtwn_rotationLongest || !Normalize(from, mix.from) ||
        !Normalize(to, mix.to))
    {
        return mtwn_errorInvalid;
    }
    double dot = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        dot += mix.from[i] * mix.to[i];
    }
    // The shortest way has a dot product of at least 0, the longest below.
    bool negate = path == mtwn_rotationShortest ? dot < 0.0 : dot >= 0.0;
    if (negate)
    {
        for (int i = 0; i < 4; ++i)
        {
            mix.to[i] = -mix.to[i];
        }
    }
    // The ends as they come out: normalized, the second negated if the
    // path did.
    mix.fromRotation =
        (mtwnQuat){(float)mix.from[0], (float)mix.from[1], (float)mix.from[2], (float)mix.from[3]};
    mix.toRotation =
        (mtwnQuat){(float)mix.to[0], (float)mix.to[1], (float)mix.to[2], (float)mix.to[3]};
    double difference = 0.0;
    double sum = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        double d = mix.from[i] - mix.to[i];
        double s = mix.from[i] + mix.to[i];
        difference += d * d;
        sum += s * s;
    }
    sum = sqrt(sum);
    if (sum < s_degenerate)
    {
        return mtwn_errorInvalid;
    }
    mix.angle = 2.0 * mtwnAtan2(sqrt(difference), sum);
    *mixOut = mix;
    return mtwn_success;
}

mtwnResult mtwnMakeRotationMix(mtwnQuat from, mtwnQuat to, mtwnRotationPath path,
                               mtwnRotationMix* mixOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnMakeRotationMixWork(from, to, path, mixOut);
    mtwnFloatEnvLeave(env);
    return result;
}

MTWN_NOINLINE mtwnQuat mtwnMixRotationWork(const mtwnRotationMix* mix, double progress)
{
    if (progress == 0.0)
    {
        return mix->fromRotation;
    }
    if (progress == 1.0)
    {
        return mix->toRotation;
    }
    // sin(k theta) / sin(theta) = k sinc(k theta) / sinc(theta).
    double theta = mix->angle;
    double phase = progress * theta;
    if (fabs(phase) >= s_phaseLost)
    {
        // Whole turns of a phase this far out hold no angle: the rotation
        // at the progress less them, once (a NaN goes on as before).
        return mtwnMixRotationWork(mix, (isinf(phase) ? 0.0 : fmod(phase, s_turn)) / theta);
    }
    double keep = 1.0 - progress;
    double denominator = Sinc(theta);
    double a = keep * Sinc(keep * theta) / denominator;
    double b = progress * Sinc(progress * theta) / denominator;
    double q[4];
    for (int i = 0; i < 4; ++i)
    {
        q[i] = a * mix->from[i] + b * mix->to[i];
    }
    // Held within the floats as a target holds a value, which changes no
    // bit in range: an angle not its ends' can weigh them past it.
    mtwnVec4 held = mtwnNumbersHeld((mtwnVec4){(float)q[0], (float)q[1], (float)q[2], (float)q[3]});
    return (mtwnQuat){held.x, held.y, held.z, held.w};
}
