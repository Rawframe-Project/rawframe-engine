// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Transitions in the player pool: a
// start from the target's current value, the kind's mix made at once,
// and the transition it replaces ended. The mixes, a spring's settling
// time and a decay's stop compute, so starting runs in the library's
// floating-point environment.

#include "maul-tween/transition.h"

#include "bounds_internal.h"
#include "coast.h"
#include "context_internal.h"
#include "css_easing.h"
#include "easing_internal.h"
#include "elementary.h"
#include "fpenv.h"
#include "valid.h"
#include "work.h"

#include <math.h>

static mtwnColor ToColor(mtwnVec4 value)
{
    return (mtwnColor){value.x, value.y, value.z, value.w};
}

static mtwnQuat ToQuat(mtwnVec4 value)
{
    return (mtwnQuat){value.x, value.y, value.z, value.w};
}

static bool IsTime(double seconds)
{
    return isfinite(seconds) && seconds >= 0.0;
}

static bool IsTolerance(double value)
{
    return isfinite(value) && value > 0.0;
}

// Whether the def's transition kind suits the value kind, and its
// spring, decay and tolerances are valid.
static bool SuitsKind(const mtwnTransitionDef* def, mtwnValueKind kind)
{
    if (!mtwnValidEasing(&def->easing))
    {
        return false;
    }
    switch (def->kind)
    {
    case mtwn_transitionEasing:
        return true;
    case mtwn_transitionSpring:
        return kind != mtwn_valueDiscrete && mtwnValidSpring(&def->spring) &&
               IsTolerance(def->restOffset) && IsTolerance(def->restSpeed);
    case mtwn_transitionDecay:
        // A scale decays in its logarithm; colors and rotations have no
        // line to coast along.
        return kind != mtwn_valueDiscrete && (!mtwnIsPathKind(kind) || kind == mtwn_valueScale) &&
               mtwnValidDecay(&def->decay) && IsTolerance(def->restSpeed);
    default:
        return false;
    }
}

// The start velocity per channel, or for a path, in progress: the channel
// velocity projected on the end minus the start. inherited is the
// target's.
static bool StartVelocity(const mtwnTransitionDef* def, const mtwnTargetSlot* target,
                          mtwnVec4 inherited, mtwnPlayerSlot* out)
{
    bool given = def->velocitySource == mtwn_velocityGiven;
    const float* v = given ? &def->velocity.x : &inherited.x;
    uint8_t channels = mtwnKindChannels(target->kind);
    for (uint8_t i = 0; i < channels; ++i)
    {
        if (!isfinite(v[i]))
        {
            return false;
        }
        out->velocity[i] = (double)v[i];
    }
    if (target->kind == mtwn_valueScale)
    {
        // An accepted scale is above 0, so its logarithm is finite.
        MTWN_ASSERT(out->from.x > 0.0f && out->to.x > 0.0f);
        // The rate in the logarithm, v / value; a spring's or easing's
        // progress takes it over the logarithm's distance.
        double rate = out->velocity[0] / (double)out->from.x;
        double distance = mtwnLog((double)out->to.x) - mtwnLog((double)out->from.x);
        out->velocity[0] = def->kind == mtwn_transitionDecay ? rate
                           : distance != 0.0                 ? rate / distance
                                                             : 0.0;
        return isfinite(out->velocity[0]);
    }
    if (mtwnIsPathKind(target->kind))
    {
        const float* from = &out->from.x;
        const float* to = &out->to.x;
        double along = 0.0;
        double length = 0.0;
        for (uint8_t i = 0; i < channels; ++i)
        {
            double d = (double)to[i] - (double)from[i];
            along += out->velocity[i] * d;
            length += d * d;
        }
        out->velocity[0] = length > 0.0 ? along / length : 0.0;
        out->velocity[1] = out->velocity[2] = out->velocity[3] = 0.0;
    }
    return true;
}

// Whether two accepted values are equal, as CSS compares computed
// values: channel by channel, so 0 and -0 are; NaN was refused.
static bool Same(mtwnVec4 a, mtwnVec4 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

// A channel's coordinate as a decay runs it: a scale's logarithm.
static double Line(mtwnValueKind kind, float x)
{
    return kind == mtwn_valueScale ? mtwnLog((double)x) : (double)x;
}

// The time after the delay a decay's channel stops: a landing's by its
// own rate, a bounded one's once its bounce settles, or the decay's.
static MTWN_NOINLINE double CoastTime(const mtwnTransitionDef* def, const mtwnPlayerSlot* out,
                                      uint8_t i)
{
    double v = out->velocity[i];
    if (out->lands)
    {
        mtwnDecay own = {out->rates[i]};
        return mtwnComputeDecayStopTimeWork(&own, v, def->restSpeed);
    }
    if (((out->crosses | out->past) & (1u << i)) != 0)
    {
        mtwnBoundedDecay bounded = mtwnCoastBounds(out, i, Line(out->kind, (&out->to.x)[i]), v);
        return mtwnComputeBoundedDecayStopTimeWork(&bounded, Line(out->kind, (&out->from.x)[i]), v,
                                                   def->restOffset, def->restSpeed);
    }
    return mtwnComputeDecayStopTimeWork(&out->decay, v, def->restSpeed);
}

// The time after the delay a spring or decay ends, and a decay's end.
static void EndOf(const mtwnTransitionDef* def, mtwnPlayerSlot* out)
{
    out->endTime = out->duration;
    if (def->kind == mtwn_transitionEasing)
    {
        return;
    }
    uint8_t channels = mtwnKindChannels(out->kind);
    if (def->kind == mtwn_transitionSpring && mtwnIsPathKind(out->kind))
    {
        out->endTime = mtwnComputeSpringSettlingTimeWork(&out->spring, -1.0, out->velocity[0],
                                                         def->restOffset, def->restSpeed);
        return;
    }
    double end = 0.0;
    float* to = &out->to.x;
    const float* from = &out->from.x;
    for (uint8_t i = 0; i < channels; ++i)
    {
        double time = def->kind == mtwn_transitionSpring
                          ? mtwnComputeSpringSettlingTimeWork(
                                &out->spring, (double)from[i] - (double)to[i], out->velocity[i],
                                def->restOffset, def->restSpeed)
                          : CoastTime(def, out, i);
        end = time > end ? time : end;
    }
    out->endTime = end;
    if (def->kind != mtwn_transitionDecay || out->lands)
    {
        return;
    }
    // A coast's end is where it stopped; a channel at a bound ends on it.
    bool scale = out->kind == mtwn_valueScale;
    for (uint8_t i = 0; i < channels; ++i)
    {
        if (((out->crosses | out->past) & (1u << i)) != 0)
        {
            continue;
        }
        double position = 0.0;
        mtwnSampleDecayWork(&out->decay, Line(out->kind, from[i]), out->velocity[i], end, &position,
                            NULL);
        position = scale ? mtwnExp(position) : position;
        position = out->kind == mtwn_valueInteger ? round(position) : position;
        // A number held within the floats as every sample is,
        // a fast fling stopping past them; a scale's stop past
        // them is refused instead.
        to[i] = (float)(scale ? position : mtwnNumberHeld(position));
    }
}

// A landing's channel: its rate from its start velocity to its value;
// false when the velocity does not move it there.
static MTWN_NOINLINE bool Land(const mtwnTransitionDef* def, mtwnPlayerSlot* out, uint8_t i)
{
    double start = Line(out->kind, (&out->from.x)[i]);
    double goal = Line(out->kind, (&out->to.x)[i]);
    double v = out->velocity[i];
    // At its value with no velocity a channel stays: any rate.
    out->rates[i] = goal == start && v == 0.0 ? def->decay.rate : v / (goal - start);
    return out->rates[i] > 0.0 && isfinite(out->rates[i]);
}

// A bounded decay's channel: whether it starts past a bound or reaches
// one, that bound its end; false for bounds refused.
static MTWN_NOINLINE bool Bound(const mtwnTransitionDef* def, mtwnPlayerSlot* out, uint8_t i)
{
    float lower = (&def->lower.x)[i];
    float upper = (&def->upper.x)[i];
    if (!(lower <= upper) || lower == INFINITY || upper == -INFINITY ||
        (out->kind == mtwn_valueScale && !(lower > 0.0f)))
    {
        return false;
    }
    double lo = Line(out->kind, lower);
    double hi = Line(out->kind, upper);
    double start = Line(out->kind, (&out->from.x)[i]);
    double v = out->velocity[i];
    double rest = start + v / def->decay.rate;
    bool past = start < lo || start > hi;
    bool below = past ? start < lo : rest < lo;
    if (!past && !((rest < lo || rest > hi) && isfinite(mtwnComputeDecayCrossingTimeWork(
                                                   &def->decay, start, v, below ? lo : hi))))
    {
        return true;
    }
    uint8_t bit = (uint8_t)(1u << i);
    out->past |= past ? bit : 0u;
    out->crosses |= past ? 0u : bit;
    (&out->to.x)[i] = below ? lower : upper;
    return true;
}

// A decay's landing or bounds: the landing's rate per
// channel and its value as the end; the channels a bounded decay hands
// over to its bounce, their bound as the end. False when the def's
// landing or bounds are refused.
static bool Coast(const mtwnTransitionDef* def, mtwnPlayerSlot* out)
{
    uint8_t channels = mtwnKindChannels(out->kind);
    out->lands = def->lands;
    if (def->lands)
    {
        if (def->bounded || !mtwnAcceptValue(out->kind, def->value, &out->to))
        {
            return false;
        }
        if (out->kind == mtwn_valueAngle)
        {
            // Landing on the value moved by the method's turns.
            out->to.x = (float)mtwnMixAngleWork((double)out->from.x, (double)out->to.x,
                                                def->angleMethod, 1.0);
        }
    }
    else if (!def->bounded)
    {
        return true;
    }
    else if (!mtwnValidSpring(&def->spring) || !IsTolerance(def->restOffset))
    {
        return false;
    }
    else
    {
        out->coast.bounce = def->spring;
    }
    bool valid = true;
    for (uint8_t i = 0; i < channels && valid; ++i)
    {
        valid = def->lands ? Land(def, out, i) : Bound(def, out, i);
    }
    return valid;
}

// The transition as the def asks it, before it takes a slot.
static mtwnResult Prepare(const mtwnTransitionDef* def, const mtwnTargetSlot* target,
                          mtwnVec4 inherited, mtwnPlayerSlot* out)
{
    out->kind = target->kind;
    out->transitionKind = def->kind;
    out->essential = def->essential;
    out->from = target->value;
    mtwnVec4 end = def->kind == mtwn_transitionDecay ? target->value : def->value;
    if (!mtwnAcceptValue(target->kind, end, &out->to) || !IsTime(def->delay) ||
        !IsTime(def->duration) || !SuitsKind(def, target->kind) ||
        (target->kind == mtwn_valueAngle && !mtwnIsAngleMethod(def->angleMethod)) ||
        def->velocitySource > mtwn_velocityGiven || !StartVelocity(def, target, inherited, out))
    {
        return mtwn_errorInvalid;
    }
    out->delay = def->delay;
    out->duration = def->duration;
    // The one curve its kind uses.
    switch (def->kind)
    {
    case mtwn_transitionEasing:
        out->easing = def->easing;
        if (mtwnIsNormalPower(&def->easing))
        {
            out->power = (uint8_t)def->easing.count;
            out->powerMode = def->easing.mode;
        }
        break;
    case mtwn_transitionSpring:
        out->spring = def->spring;
        break;
    default:
        out->decay = def->decay;
        break;
    }
    // A decay's end, computed after, replaces this.
    if (target->kind == mtwn_valueAngle)
    {
        // The end moved by the method's turns, once.
        out->to.x =
            (float)mtwnMixAngleWork((double)out->from.x, (double)out->to.x, def->angleMethod, 1.0);
    }
    out->crosses = 0;
    out->past = 0;
    out->lands = false;
    if (def->kind == mtwn_transitionDecay && !Coast(def, out))
    {
        return mtwn_errorInvalid;
    }
    EndOf(def, out);
    // A delay, duration, settling or stopping time: never below 0 or NaN,
    // so the update ends the transition once its time is reached.
    MTWN_ASSERT(out->endTime >= 0.0);
    switch (target->kind)
    {
    case mtwn_valueColor:
        return mtwnMakeColorMixWork(ToColor(out->from), ToColor(out->to), def->colorSpace,
                                    def->hueMethod, &out->mix.color);
    case mtwn_valueRotation:
        return mtwnMakeRotationMixWork(ToQuat(out->from), ToQuat(out->to), def->rotationPath,
                                       &out->mix.rotation);
    case mtwn_valueScale:
        // A decay's end rounded to float may leave a scale's range.
        return mtwnMakeScaleMixWork((double)out->from.x, (double)out->to.x, &out->mix.scale);
    default:
        return mtwn_success;
    }
}

// CSS Transitions' reversal: an eased transition starts where the one it
// replaces would return to, unless it reverses that one, ending where it
// began: then its duration shrinks by the share of the way that one
// covered, eased.
static void Reverse(const mtwnContext* context, const mtwnTransitionDef* def,
                    const mtwnTargetSlot* target, mtwnPlayerSlot* out)
{
    if (out->transitionKind != mtwn_transitionEasing)
    {
        return;
    }
    out->reversingStart = out->from;
    out->shortening = 1.0;
    if (!def->reverses || target->header.link == 0)
    {
        return;
    }
    const mtwnPlayerSlot* running = mtwnPlayerAt(context, target->header.link);
    // A target's link is the transition writing it, which links back.
    MTWN_ASSERT(running->type == MTWN_PLAYER_TRANSITION &&
                running->header.link == def->target.index1);
    mtwnVec4 end = {0};
    if (running->transitionKind != mtwn_transitionEasing ||
        !mtwnAcceptValue(target->kind, def->value, &end) || !Same(end, running->reversingStart))
    {
        return;
    }
    // An eased transition's shortening is the share left of its way.
    MTWN_ASSERT(running->shortening >= 0.0 && running->shortening <= 1.0);
    double share = mtwnEasedProgress(context, target->header.link) * running->shortening +
                   (1.0 - running->shortening);
    double factor = fabs(share) < 1.0 ? fabs(share) : 1.0;
    out->reversingStart = running->to;
    out->shortening = factor;
    out->duration *= factor;
    out->endTime = out->duration;
}

static MTWN_NOINLINE mtwnResult StartWorker(mtwnContext* context, const mtwnTransitionDef* def,
                                            mtwnPlayerId* playerOut)
{
    mtwnTargetSlot* target = (mtwnTargetSlot*)mtwnPoolFind(&context->targets, def->target.index1,
                                                           def->target.generation);
    if (target == NULL)
    {
        mtwnCountMisuse(context);
        return mtwn_errorStale;
    }
    mtwnPlayerSlot prepared = {0};
    mtwnResult result =
        Prepare(def, target, mtwnTargetVelocity(context, def->target.index1), &prepared);
    if (result != mtwn_success)
    {
        mtwnCountMisuse(context);
        return result;
    }
    // A linear() easing's points are copied into the slot's own, so the
    // host's may change once it starts: refused here, changing nothing, if
    // the host changed them from what its maker left or they do not fit.
    bool copies = prepared.transitionKind == mtwn_transitionEasing &&
                  prepared.easing.kind == mtwn_easingLinearPoints;
    if (copies && !mtwnCanonicalLinearPoints(prepared.easing.points, prepared.easing.count))
    {
        mtwnCountMisuse(context);
        return mtwn_errorInvalid;
    }
    if (copies && prepared.easing.count > context->def.pointsPerTransition)
    {
        return mtwn_errorCapacity;
    }
    Reverse(context, def, target, &prepared);
    // The transition it replaces frees its slot first.
    if (target->header.link != 0)
    {
        mtwnPlayerSlot* replaced = mtwnPlayerAt(context, target->header.link);
        double now = (double)context->lastTimeNs / 1e9;
        double since =
            replaced->started ? now - (double)replaced->startNs / 1e9 - replaced->delay : 0.0;
        mtwnPlayerEvent(context, target->header.link, mtwn_eventCancel, now, since);
        mtwnEndTransition(context, target->header.link);
    }
    MTWN_ASSERT(target->header.link == 0);
    uint32_t index1 = mtwnTakePlayer(context, MTWN_PLAYER_TRANSITION);
    if (index1 == 0)
    {
        return mtwn_errorCapacity;
    }
    mtwnPlayerSlot* slot = mtwnPlayerAt(context, index1);
    MTWN_ASSERT(slot->type == MTWN_PLAYER_TRANSITION);
    if (copies)
    {
        mtwnCopyLinearPoints(&prepared.easing, mtwnTransitionPointsAt(context, index1));
    }
    prepared.header = slot->header;
    prepared.type = slot->type;

    prepared.earlier = slot->earlier;
    prepared.later = slot->later;
    prepared.header.link = def->target.index1;
    prepared.target = def->target;
    *slot = prepared;
    mtwnLinkTarget(context, target, index1);
    *playerOut = (mtwnPlayerId){index1, slot->header.generation};
    return mtwn_success;
}

static mtwnResult RunStartTransition(mtwnContext* context, const mtwnTransitionDef* def,
                                     mtwnPlayerId* playerOut)
{
    if (context == NULL)
    {
        return mtwn_errorInvalid;
    }
    if (playerOut != NULL)
    {
        *playerOut = (mtwnPlayerId){0, 0};
    }
    if (def == NULL || playerOut == NULL || def->cookie != MTWN_TRANSITION_DEF_COOKIE)
    {
        mtwnCountMisuse(context);
        return mtwn_errorInvalid;
    }
    mtwnResult result = StartWorker(context, def, playerOut);
    return result;
}

static MTWN_NOINLINE mtwnResult mtwnStartTransitionWork(mtwnContext* context,
                                                        const mtwnTransitionDef* def,
                                                        mtwnPlayerId* playerOut)
{
    // Cleared first, so a busy or missing context's failure clears it too.
    if (playerOut != NULL)
    {
        *playerOut = (mtwnPlayerId){0};
    }
    if (!mtwnEnter(context))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunStartTransition(context, def, playerOut);
    mtwnLeave(context);
    return result;
}

mtwnResult mtwnStartTransition(mtwnContext* context, const mtwnTransitionDef* def,
                               mtwnPlayerId* playerOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnStartTransitionWork(context, def, playerOut);
    mtwnFloatEnvLeave(env);
    return result;
}

// The distance from a value to an end, both accepted, in the kind's own
// measure.
static mtwnResult Distance(const mtwnTransitionDef* def, mtwnValueKind kind, mtwnVec4 from,
                           mtwnVec4 to, double* distanceOut)
{
    double a[4] = {(double)from.x, (double)from.y, (double)from.z, (double)from.w};
    double b[4] = {(double)to.x, (double)to.y, (double)to.z, (double)to.w};
    switch (kind)
    {
    case mtwn_valueDiscrete:
        return mtwn_errorInvalid;
    case mtwn_valueAngle:
        if (!mtwnIsAngleMethod(def->angleMethod))
        {
            return mtwn_errorInvalid;
        }
        *distanceOut = fabs(mtwnMixAngleWork(a[0], b[0], def->angleMethod, 1.0) - a[0]);
        return mtwn_success;
    case mtwn_valueScale:
        *distanceOut = fabs(mtwnLog(b[0]) - mtwnLog(a[0]));
        return mtwn_success;
    case mtwn_valueRotation:
    {
        // The mix's angle is between the quaternions, half the turn.
        mtwnRotationMix mix;
        mtwnResult made =
            mtwnMakeRotationMixWork(ToQuat(from), ToQuat(to), def->rotationPath, &mix);
        *distanceOut = made == mtwn_success ? 2.0 * mix.angle : 0.0;
        return made;
    }
    case mtwn_valueColor:
        if (mtwnConvertColorToSpaceWork(ToColor(from), mtwn_colorOklab, a) != mtwn_success ||
            mtwnConvertColorToSpaceWork(ToColor(to), mtwn_colorOklab, b) != mtwn_success)
        {
            return mtwn_errorInvalid;
        }
        break;
    default:
        break;
    }
    double sum = 0.0;
    for (uint8_t i = 0; i < mtwnKindChannels(kind); ++i)
    {
        sum += (b[i] - a[i]) * (b[i] - a[i]);
    }
    *distanceOut = sqrt(sum);
    return mtwn_success;
}

static mtwnResult RunContextMeasureTransition(mtwnContext* context, const mtwnTransitionDef* def,
                                              double* distanceOut)
{
    if (distanceOut != NULL)
    {
        *distanceOut = 0.0;
    }
    if (context == NULL)
    {
        return mtwn_errorInvalid;
    }
    if (def == NULL || distanceOut == NULL || def->cookie != MTWN_TRANSITION_DEF_COOKIE)
    {
        mtwnCountMisuse(context);
        return mtwn_errorInvalid;
    }
    if (!mtwnValidEasing(&def->easing))
    {
        mtwnCountMisuse(context);
        return mtwn_errorInvalid;
    }
    const mtwnTargetSlot* target = (const mtwnTargetSlot*)mtwnPoolFind(
        &context->targets, def->target.index1, def->target.generation);
    if (target == NULL)
    {
        mtwnCountMisuse(context);
        return mtwn_errorStale;
    }
    mtwnVec4 end = {0};
    mtwnResult result = mtwnAcceptValue(target->kind, def->value, &end)
                            ? Distance(def, target->kind, target->value, end, distanceOut)
                            : mtwn_errorInvalid;
    if (result != mtwn_success)
    {
        *distanceOut = 0.0;
        mtwnCountMisuse(context);
    }
    return result;
}

static MTWN_NOINLINE mtwnResult mtwnContext_MeasureTransitionWork(mtwnContext* context,
                                                                  const mtwnTransitionDef* def,
                                                                  double* distanceOut)
{
    if (!mtwnEnter(context))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunContextMeasureTransition(context, def, distanceOut);
    mtwnLeave(context);
    return result;
}

mtwnResult mtwnContext_MeasureTransition(mtwnContext* context, const mtwnTransitionDef* def,
                                         double* distanceOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnContext_MeasureTransitionWork(context, def, distanceOut);
    mtwnFloatEnvLeave(env);
    return result;
}

void mtwnEndTransition(mtwnContext* context, uint32_t player)
{
    uint32_t target = mtwnPlayerAt(context, player)->header.link;
    // A transition and its target link to each other.
    MTWN_ASSERT(mtwnPlayerAt(context, player)->type == MTWN_PLAYER_TRANSITION && target != 0 &&
                mtwnTargetAt(context, target)->header.link == player);
    mtwnSettleVelocity(context, target);
    mtwnLinkTarget(context, mtwnTargetAt(context, target), 0);
    mtwnFreePlayer(context, player);
}
