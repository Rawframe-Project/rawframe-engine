// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Decay and the rubber band. A decay
// moves velocity (1 - e^(-y)) / k from its start at y = k t, computed as
// velocity t times (1 - e^(-y)) / y, by its series below y = 1/2, where
// 1 - e^(-y) would lose the digits of a small y. Its crossing time
// -ln(1 - f) / k is 2 atanh(f / (2 - f)) / k by the atanh series for
// f <= 1/2, for the same reason.

#include "elementary.h"
#include "fpenv.h"
#include "invariant.h"
#include "valid.h"
#include "work.h"

#include "maul-tween/spring.h"

#include <float.h>
#include <math.h>

static const double s_seriesBelow = 0.5;

MTWN_NOINLINE mtwnResult mtwnMakeDecayWork(double rate, mtwnDecay* decayOut)
{
    mtwnDecay decay = {rate};
    if (decayOut == NULL || !mtwnValidDecay(&decay))
    {
        return mtwn_errorInvalid;
    }
    *decayOut = decay;
    return mtwn_success;
}

bool mtwnValidDecay(const mtwnDecay* decay)
{
    return decay != NULL && isfinite(decay->rate) && decay->rate > 0.0;
}

mtwnResult mtwnMakeDecay(double rate, mtwnDecay* decayOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnMakeDecayWork(rate, decayOut);
    mtwnFloatEnvLeave(env);
    return result;
}

static MTWN_NOINLINE mtwnResult mtwnMakeDecayFromDecelerationWork(double perMillisecond,
                                                                  mtwnDecay* decayOut)
{
    // A share outside (0, 1) gives a rate that is NaN, infinite or not above
    // 0, which mtwnMakeDecay refuses.
    return mtwnMakeDecay(-1000.0 * mtwnLog(perMillisecond), decayOut);
}

mtwnResult mtwnMakeDecayFromDeceleration(double perMillisecond, mtwnDecay* decayOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnMakeDecayFromDecelerationWork(perMillisecond, decayOut);
    mtwnFloatEnvLeave(env);
    return result;
}

// (1 - e^(-y)) / y for y >= 0.
static double TravelShare(double y)
{
    if (y >= s_seriesBelow)
    {
        return (1.0 - mtwnExp(-y)) / y;
    }
    // The sum of (-y)^n / (n + 1)! for n from 0 to 17, nested as 1 + (-y)
    // / 2 (1 + (-y) / 3 (1 + ...)); the next term is under 1e-20 at
    // y = 1/2. y is a rate times a time, both at least 0.
    MTWN_ASSERT(y >= 0.0);
    double sum = 1.0;
    for (int k = 18; k >= 2; --k)
    {
        sum = 1.0 + -y / (double)k * sum;
    }
    return sum;
}

// The travel at a velocity over seconds, y = k t of them, kept within the
// doubles on the way wherever it is itself.
MTWN_NOINLINE static double Travel(double rate, double velocity, double seconds, double y)
{
    if (isinf(seconds))
    {
        return velocity / rate;
    }
    double distance = velocity * seconds;
    if (isfinite(distance) && isfinite(y))
    {
        return distance * TravelShare(y);
    }
    if (!(y < s_seriesBelow))
    {
        // v t or k t past the doubles, the travel from its closed form,
        // whose v (1 - e^(-y)) cannot overflow.
        return velocity * (1.0 - mtwnExp(-y)) / rate;
    }
    // v t past the doubles at a small y: from half of it, which overflows
    // only for a travel past the doubles too.
    return 2.0 * (0.5 * velocity * seconds * TravelShare(y));
}

MTWN_NOINLINE void mtwnSampleDecayWork(const mtwnDecay* decay, double position, double velocity,
                                       double seconds, double* positionOut, double* velocityOut)
{
    double rate = decay->rate;
    double y = rate * seconds;
    if (positionOut != NULL)
    {
        // A travel past the doubles from a start on the other side may end
        // within them: then from half of each.
        double moved = position + Travel(rate, velocity, seconds, y);
        *positionOut = isfinite(moved)
                           ? moved
                           : 2.0 * (0.5 * position + Travel(rate, 0.5 * velocity, seconds, y));
    }
    if (velocityOut != NULL)
    {
        double kept = 0.0;
        if (!isinf(seconds))
        {
            // e^(-y) falls below the normal doubles past y = 708, rounding
            // to few digits or 0 where the velocity need not; its halves do
            // not.
            double decayed = mtwnExp(-y);
            double half = decayed < DBL_MIN ? mtwnExp(-0.5 * y) : 0.0;
            kept = decayed < DBL_MIN ? velocity * half * half : velocity * decayed;
        }
        *velocityOut = kept;
    }
}

static MTWN_NOINLINE mtwnResult mtwnSampleDecayChecked(const mtwnDecay* decay, double position,
                                                       double velocity, double seconds,
                                                       double* positionOut, double* velocityOut)
{
    if (!mtwnValidDecay(decay) || !isfinite(position) || !isfinite(velocity) || !(seconds >= 0.0))
    {
        return mtwn_errorInvalid;
    }
    mtwnSampleDecayWork(decay, position, velocity, seconds, positionOut, velocityOut);
    return mtwn_success;
}

mtwnResult mtwnSampleDecay(const mtwnDecay* decay, double position, double velocity, double seconds,
                           double* positionOut, double* velocityOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result =
        mtwnSampleDecayChecked(decay, position, velocity, seconds, positionOut, velocityOut);
    mtwnFloatEnvLeave(env);
    return result;
}

MTWN_NOINLINE double mtwnComputeDecayStopTimeWork(const mtwnDecay* decay, double velocity,
                                                  double stopSpeed)
{
    if (!(stopSpeed > 0.0))
    {
        return HUGE_VAL;
    }
    double speed = fabs(velocity);
    if (speed <= stopSpeed)
    {
        return 0.0;
    }
    // A ratio past the doubles, as the logarithms' difference.
    double ratio = speed / stopSpeed;
    return (isfinite(ratio) ? mtwnLog(ratio) : mtwnLog(speed) - mtwnLog(stopSpeed)) / decay->rate;
}

static MTWN_NOINLINE mtwnResult mtwnComputeDecayStopTimeChecked(const mtwnDecay* decay,
                                                                double velocity, double stopSpeed,
                                                                double* secondsOut)
{
    if (!mtwnValidDecay(decay) || !isfinite(velocity) || !(stopSpeed > 0.0) || secondsOut == NULL)
    {
        return mtwn_errorInvalid;
    }
    *secondsOut = mtwnComputeDecayStopTimeWork(decay, velocity, stopSpeed);
    return mtwn_success;
}

mtwnResult mtwnComputeDecayStopTime(const mtwnDecay* decay, double velocity, double stopSpeed,
                                    double* secondsOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnComputeDecayStopTimeChecked(decay, velocity, stopSpeed, secondsOut);
    mtwnFloatEnvLeave(env);
    return result;
}

// d 2^doubled k / v from the operands' mantissas and exponents, which no
// product on the way leaves the doubles from.
static double ScaledShare(double distance, int doubled, double rate, double velocity)
{
    int distanceExponent = 0;
    int rateExponent = 0;
    int velocityExponent = 0;
    double distanceMantissa = frexp(distance, &distanceExponent);
    double rateMantissa = frexp(rate, &rateExponent);
    double velocityMantissa = frexp(velocity, &velocityExponent);
    return ldexp(distanceMantissa * rateMantissa / velocityMantissa,
                 distanceExponent + doubled + rateExponent - velocityExponent);
}

MTWN_NOINLINE double mtwnComputeDecayCrossingTimeWork(const mtwnDecay* decay, double position,
                                                      double velocity, double target)
{
    double distance = target - position;
    if (distance == 0.0)
    {
        return 0.0;
    }
    // A distance past the doubles, halved: both ends are then even.
    int doubled = 0;
    if (isinf(distance))
    {
        distance = 0.5 * target - 0.5 * position;
        doubled = 1;
    }
    // The share of the whole travel, velocity / k, the target lies at.
    // Where d k or the share falls below the normal doubles, or d was
    // halved, it comes from the operands' mantissas and exponents
    // instead.
    double product = distance * decay->rate;
    double share = product / velocity;
    if (doubled != 0 || fabs(product) < DBL_MIN || fabs(share) < DBL_MIN)
    {
        share = ScaledShare(distance, doubled, decay->rate, velocity);
    }
    // Toward the target by the signs, as a share below the least
    // subnormal rounds to 0.
    if (velocity == 0.0 || (distance > 0.0) != (velocity > 0.0) || !(share < 1.0))
    {
        return HUGE_VAL;
    }
    if (share > s_seriesBelow)
    {
        // 1 - share is exact here (Sterbenz).
        return -mtwnLog(1.0 - share) / decay->rate;
    }
    // 2 atanh(s) with s = share / (2 - share) <= 1/3: s^2 <= 1/9, so 17
    // terms leave under 1e-17.
    double s = share / (2.0 - share);
    // A target ahead with a share at most 1/2; 0 for a share below the
    // least subnormal.
    MTWN_ASSERT(s >= 0.0 && s <= 1.0 / 3.0);
    double z = s * s;
    double sum = 0.0;
    for (int n = 16; n >= 0; --n)
    {
        sum = sum * z + 1.0 / (double)(2 * n + 1);
    }
    if (s < DBL_MIN)
    {
        // An s below the normal doubles keeps few digits or none: 2 s / k
        // is 2 (d / v) / (2 - share), with d doubled back if it was halved,
        // scaling a small d before the quotient can fall below the normal
        // doubles and a large one after.
        double twice = doubled != 0 ? 4.0 : 2.0;
        double lead =
            fabs(distance) < 1.0 ? twice * distance / velocity : distance / velocity * twice;
        return lead / (2.0 - share) * sum;
    }
    return 2.0 * s * sum / decay->rate;
}

static MTWN_NOINLINE mtwnResult mtwnComputeDecayCrossingTimeChecked(const mtwnDecay* decay,
                                                                    double position,
                                                                    double velocity, double target,
                                                                    double* secondsOut)
{
    if (!mtwnValidDecay(decay) || !isfinite(position) || !isfinite(velocity) || !isfinite(target) ||
        secondsOut == NULL)
    {
        return mtwn_errorInvalid;
    }
    *secondsOut = mtwnComputeDecayCrossingTimeWork(decay, position, velocity, target);
    return mtwn_success;
}

mtwnResult mtwnComputeDecayCrossingTime(const mtwnDecay* decay, double position, double velocity,
                                        double target, double* secondsOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result =
        mtwnComputeDecayCrossingTimeChecked(decay, position, velocity, target, secondsOut);
    mtwnFloatEnvLeave(env);
    return result;
}

// Whether a rubber band takes a view's dimension and a coefficient: above
// 0, an endless one too.
static bool Bands(double dimension, double coefficient)
{
    return dimension > 0.0 && coefficient > 0.0;
}

static double RubberBand(double distance, double dimension, double coefficient)
{
    double past = fabs(distance);
    // Nothing past the limit moves nothing, for any coefficient: an endless
    // one's c x would be NaN.
    if (past == 0.0)
    {
        return distance;
    }
    // s d / (s + d) with s = c x, as m / (1 + m / M) for the smaller m and
    // larger M of the two, which neither overflows nor underflows, and is d
    // for an infinite drag; m when both are infinite, as the form tends to.
    double scaled = coefficient * past;
    double smaller = scaled < dimension ? scaled : dimension;
    double larger = scaled < dimension ? dimension : scaled;
    return copysign(isinf(smaller) ? smaller : smaller / (1.0 + smaller / larger), distance);
}

static MTWN_NOINLINE mtwnResult mtwnApplyRubberBandChecked(double distance, double dimension,
                                                           double coefficient, double* offsetOut)
{
    if (offsetOut == NULL || !Bands(dimension, coefficient) || isnan(distance))
    {
        return mtwn_errorInvalid;
    }
    *offsetOut = RubberBand(distance, dimension, coefficient);
    return mtwn_success;
}

mtwnResult mtwnApplyRubberBand(double distance, double dimension, double coefficient,
                               double* offsetOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnApplyRubberBandChecked(distance, dimension, coefficient, offsetOut);
    mtwnFloatEnvLeave(env);
    return result;
}

static double RubberBandInverse(double offset, double dimension, double coefficient)
{
    double moved = fabs(offset);
    if (moved >= dimension)
    {
        return copysign(HUGE_VAL, offset);
    }
    // An endless view resists nothing: d / (d - o) tends to 1.
    double resisted = isinf(dimension) ? 1.0 : dimension / (dimension - moved);
    return copysign(resisted * moved / coefficient, offset);
}

static MTWN_NOINLINE mtwnResult mtwnInvertRubberBandChecked(double offset, double dimension,
                                                            double coefficient, double* dragOut)
{
    if (dragOut == NULL || !Bands(dimension, coefficient) || isnan(offset))
    {
        return mtwn_errorInvalid;
    }
    *dragOut = RubberBandInverse(offset, dimension, coefficient);
    return mtwn_success;
}

mtwnResult mtwnInvertRubberBand(double offset, double dimension, double coefficient,
                                double* dragOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnInvertRubberBandChecked(offset, dimension, coefficient, dragOut);
    mtwnFloatEnvLeave(env);
    return result;
}
