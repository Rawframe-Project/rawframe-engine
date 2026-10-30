// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Inertia: a decay landing on a chosen end, and a
// decay held between bounds that hands over to a spring at the bound it
// crosses, at that crossing time and with the velocity it has there. Kept
// apart from the decay, whose sampling it calls rather than copies.

#include "fpenv.h"
#include "valid.h"
#include "work.h"

#include "maul-tween/spring.h"

#include <math.h>
#include <stddef.h>

static MTWN_NOINLINE mtwnResult mtwnMakeDecayToRestWork(double position, double velocity,
                                                        double rest, mtwnDecay* decayOut)
{
    if (!isfinite(position) || !isfinite(velocity) || !isfinite(rest))
    {
        return mtwn_errorInvalid;
    }
    // An end behind the start or at it, a velocity of 0 or a distance that
    // overflows gives a rate not finite or not above 0, refused there.
    return mtwnMakeDecayWork(velocity / (rest - position), decayOut);
}

mtwnResult mtwnMakeDecayToRest(double position, double velocity, double rest, mtwnDecay* decayOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnMakeDecayToRestWork(position, velocity, rest, decayOut);
    mtwnFloatEnvLeave(env);
    return result;
}

static MTWN_NOINLINE mtwnResult mtwnMakeBoundedDecayWork(const mtwnDecay* decay,
                                                         const mtwnSpring* spring, double lower,
                                                         double upper, mtwnBoundedDecay* boundedOut)
{
    if (decay == NULL || spring == NULL || boundedOut == NULL)
    {
        return mtwn_errorInvalid;
    }
    mtwnBoundedDecay bounded = {*decay, *spring, lower, upper};
    if (!mtwnValidBoundedDecay(&bounded))
    {
        return mtwn_errorInvalid;
    }
    *boundedOut = bounded;
    return mtwn_success;
}

MTWN_NOINLINE bool mtwnValidBoundedDecay(const mtwnBoundedDecay* bounded)
{
    // A lower bound of infinity or an upper of -infinity leaves nowhere
    // finite to rest: a start short of it would bounce toward it forever.
    return bounded != NULL && mtwnValidDecay(&bounded->decay) &&
           mtwnValidSpring(&bounded->spring) && bounded->lower <= bounded->upper &&
           bounded->lower != HUGE_VAL && bounded->upper != -HUGE_VAL;
}

mtwnResult mtwnMakeBoundedDecay(const mtwnDecay* decay, const mtwnSpring* spring, double lower,
                                double upper, mtwnBoundedDecay* boundedOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnMakeBoundedDecayWork(decay, spring, lower, upper, boundedOut);
    mtwnFloatEnvLeave(env);
    return result;
}

// Where a bounded decay from a start goes over to its spring: the bound,
// the time, and the offset from the bound and velocity then. False when
// it coasts to rest within the bounds.
typedef struct Handoff
{
    double bound;
    double time;
    double offset;
    double velocity;
} Handoff;

static MTWN_NOINLINE bool HandoffOf(const mtwnBoundedDecay* bounded, double position,
                                    double velocity, Handoff* out)
{
    if (position < bounded->lower || position > bounded->upper)
    {
        double bound = position < bounded->lower ? bounded->lower : bounded->upper;
        *out = (Handoff){bound, 0.0, position - bound, velocity};
        return true;
    }
    double rest = position + velocity / bounded->decay.rate;
    if (!(rest < bounded->lower || rest > bounded->upper))
    {
        return false;
    }
    out->bound = rest < bounded->lower ? bounded->lower : bounded->upper;
    out->time = mtwnComputeDecayCrossingTimeWork(&bounded->decay, position, velocity, out->bound);
    // A rest past the bound by rounding alone never reaches it.
    if (isinf(out->time))
    {
        return false;
    }
    out->offset = 0.0;
    mtwnSampleDecayWork(&bounded->decay, position, velocity, out->time, NULL, &out->velocity);
    return true;
}

MTWN_NOINLINE void mtwnSampleBoundedDecayWork(const mtwnBoundedDecay* bounded, double position,
                                              double velocity, double seconds, double* positionOut,
                                              double* velocityOut)
{
    Handoff handoff;
    if (!HandoffOf(bounded, position, velocity, &handoff) || seconds < handoff.time)
    {
        mtwnSampleDecayWork(&bounded->decay, position, velocity, seconds, positionOut, velocityOut);
        return;
    }
    double offset = 0.0;
    mtwnSampleSpringWork(&bounded->spring, handoff.offset, handoff.velocity, seconds - handoff.time,
                         &offset, velocityOut);
    if (positionOut != NULL)
    {
        *positionOut = handoff.bound + offset;
    }
}

static MTWN_NOINLINE mtwnResult mtwnSampleBoundedDecayChecked(const mtwnBoundedDecay* bounded,
                                                              double position, double velocity,
                                                              double seconds, double* positionOut,
                                                              double* velocityOut)
{
    if (!mtwnValidBoundedDecay(bounded) || !isfinite(position) || !isfinite(velocity) ||
        !(seconds >= 0.0))
    {
        return mtwn_errorInvalid;
    }
    mtwnSampleBoundedDecayWork(bounded, position, velocity, seconds, positionOut, velocityOut);
    return mtwn_success;
}

mtwnResult mtwnSampleBoundedDecay(const mtwnBoundedDecay* bounded, double position, double velocity,
                                  double seconds, double* positionOut, double* velocityOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnSampleBoundedDecayChecked(bounded, position, velocity, seconds,
                                                      positionOut, velocityOut);
    mtwnFloatEnvLeave(env);
    return result;
}

MTWN_NOINLINE double mtwnComputeBoundedDecayStopTimeWork(const mtwnBoundedDecay* bounded,
                                                         double position, double velocity,
                                                         double offsetTolerance, double stopSpeed)
{
    if (!(offsetTolerance > 0.0) || !(stopSpeed > 0.0))
    {
        return HUGE_VAL;
    }
    Handoff handoff;
    if (!HandoffOf(bounded, position, velocity, &handoff))
    {
        return mtwnComputeDecayStopTimeWork(&bounded->decay, velocity, stopSpeed);
    }
    return handoff.time + mtwnComputeSpringSettlingTimeWork(&bounded->spring, handoff.offset,
                                                            handoff.velocity, offsetTolerance,
                                                            stopSpeed);
}

static MTWN_NOINLINE mtwnResult mtwnComputeBoundedDecayStopTimeChecked(
    const mtwnBoundedDecay* bounded, double position, double velocity, double offsetTolerance,
    double stopSpeed, double* secondsOut)
{
    if (!mtwnValidBoundedDecay(bounded) || !isfinite(position) || !isfinite(velocity) ||
        !(offsetTolerance > 0.0) || !(stopSpeed > 0.0) || secondsOut == NULL)
    {
        return mtwn_errorInvalid;
    }
    *secondsOut = mtwnComputeBoundedDecayStopTimeWork(bounded, position, velocity, offsetTolerance,
                                                      stopSpeed);
    return mtwn_success;
}

mtwnResult mtwnComputeBoundedDecayStopTime(const mtwnBoundedDecay* bounded, double position,
                                           double velocity, double offsetTolerance,
                                           double stopSpeed, double* secondsOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnComputeBoundedDecayStopTimeChecked(
        bounded, position, velocity, offsetTolerance, stopSpeed, secondsOut);
    mtwnFloatEnvLeave(env);
    return result;
}
