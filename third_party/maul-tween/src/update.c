// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The update: every player
// advanced to the host's time in the library's floating-point
// environment, then the changed targets reported. A transition samples
// its easing, spring or decay, with the spring's or decay's own velocity
// or else its curve's over the last millisecond, and at its end commits
// its end value as the target's base.

#include "bounds_internal.h"
#include "coast.h"
#include "context_internal.h"
#include "easing_internal.h"
#include "elementary.h"
#include "fpenv.h"
#include "mix_internal.h"
#include "work.h"

#include <math.h>
#include <string.h>

// The span the velocity is measured over, as Compose measures a tween's.
static const double s_velocitySpan = 0.001;

// The value along the transition's path at a progress, before rounding:
// a color's or rotation's mix, a scale's, or a discrete value's step.
// Numbers never come here: eased ones are WriteEased's, and Sample
// computes their springs and decays itself.
static void PathAt(const mtwnPlayerSlot* slot, double progress, double out[4])
{
    const float* from = &slot->from.x;
    const float* to = &slot->to.x;
    out[0] = out[1] = out[2] = out[3] = 0.0;
    switch (slot->kind)
    {
    case mtwn_valueColor:
    {
        mtwnColor color = mtwnMixColorWork(&slot->mix.color, progress);
        out[0] = (double)color.red;
        out[1] = (double)color.green;
        out[2] = (double)color.blue;
        out[3] = (double)color.alpha;
        return;
    }
    case mtwn_valueRotation:
    {
        mtwnQuat q = mtwnMixRotationWork(&slot->mix.rotation, progress);
        out[0] = (double)q.x;
        out[1] = (double)q.y;
        out[2] = (double)q.z;
        out[3] = (double)q.w;
        return;
    }
    case mtwn_valueScale:
        out[0] = mtwnMixScaleWork(&slot->mix.scale, progress);
        return;
    default:
        // A discrete value, the only kind left.
        out[0] = (double)(progress < 0.5 ? from[0] : to[0]);
        return;
    }
}

// The transition at t seconds after its delay, before rounding. A spring
// or decay on channels gives its own velocity and returns true; the
// others leave the velocity to the caller.
static bool Sample(const mtwnPlayerSlot* slot, double t, double value[4], double velocity[4])
{
    if (slot->transitionKind == mtwn_transitionEasing)
    {
        PathAt(slot, mtwnSampleEasingWork(&slot->easing, t / slot->duration, false), value);
        return false;
    }
    if (slot->transitionKind == mtwn_transitionSpring && mtwnIsPathKind(slot->kind))
    {
        double offset = 0.0;
        mtwnSampleSpringWork(&slot->spring, -1.0, slot->velocity[0], t, &offset, NULL);
        PathAt(slot, 1.0 + offset, value);
        return false;
    }
    const float* from = &slot->from.x;
    const float* to = &slot->to.x;
    if (slot->kind == mtwn_valueScale)
    {
        // A decay in the logarithm: the value its exponential, the
        // velocity the value times the logarithm's rate.
        double position = 0.0;
        double rate = 0.0;
        mtwnCoastAt(slot, 0, mtwnLog((double)from[0]), mtwnLog((double)to[0]), slot->velocity[0], t,
                    &position, &rate);
        value[1] = value[2] = value[3] = velocity[1] = velocity[2] = velocity[3] = 0.0;
        value[0] = mtwnExp(position);
        // The velocity of the value the target holds, at most the largest
        // float: finite, and 0 at rest wherever the logarithm lies.
        velocity[0] = (double)mtwnScaleFloat(value[0]) * rate;
        return true;
    }
    mtwnSpringMotion motion = {0};
    if (slot->transitionKind == mtwn_transitionSpring)
    {
        motion = mtwnComputeSpringMotionWork(&slot->spring, t);
    }
    for (uint8_t i = 0; i < 4; ++i)
    {
        value[i] = velocity[i] = 0.0;
        if (i >= mtwnKindChannels(slot->kind))
        {
            continue;
        }
        double v = slot->velocity[i];
        if (slot->transitionKind == mtwn_transitionSpring)
        {
            double offset = (double)from[i] - (double)to[i];
            value[i] =
                (double)to[i] + motion.offsetFromOffset * offset + motion.offsetFromVelocity * v;
            velocity[i] = motion.velocityFromOffset * offset + motion.velocityFromVelocity * v;
        }
        else
        {
            mtwnCoastAt(slot, i, (double)from[i], (double)to[i], v, t, &value[i], &velocity[i]);
        }
    }
    return true;
}

// Whether two values have the same bits, -0 and 0 apart.
static bool SameBits(mtwnVec4 a, mtwnVec4 b)
{
    uint32_t x[4];
    uint32_t y[4];
    memcpy(x, &a, sizeof x);
    memcpy(y, &b, sizeof y);
    return x[0] == y[0] && x[1] == y[1] && x[2] == y[2] && x[3] == y[3];
}

// Writes a target's value, marking it changed when its bits changed.
static inline void WriteValue(mtwnContext* context, uint32_t index1, mtwnVec4 value)
{
    mtwnTargetSlot* target = mtwnTargetAt(context, index1);
    if (!SameBits(target->value, value))
    {
        target->value = value;
        mtwnMarkChanged(context, index1);
    }
}

static inline void WriteTarget(mtwnContext* context, uint32_t index1, mtwnVec4 value,
                               mtwnVec4 velocity)
{
    mtwnTargetSlot* target = mtwnTargetAt(context, index1);
    target->header.flags &=
        (uint8_t)~(MTWN_TARGET_UNWRITTEN | MTWN_TARGET_LAZY | MTWN_TARGET_CLIP_LAZY);
    WriteValue(context, index1, value);
    target->velocity = velocity;
}

// Whether a transition is eased numbers: its velocity is then computed
// only when read.
static bool IsEasedNumbers(const mtwnPlayerSlot* slot)
{
    return slot->transitionKind == mtwn_transitionEasing && !mtwnIsPathKind(slot->kind) &&
           slot->kind != mtwn_valueDiscrete;
}

// A transition's easing at a progress: a power from the slot's first
// line, or the easing.
static inline double Eased(const mtwnPlayerSlot* slot, double progress)
{
    return slot->power != 0 ? mtwnEasePower(slot->power, slot->powerMode, progress)
                            : mtwnEaseFast(&slot->easing, progress);
}

// An eased transition of numbers at t seconds after its delay, before
// rounding: Sample's arithmetic for the commonest case, straight through.
static void EasedValue(const mtwnPlayerSlot* slot, double t, double value[4])
{
    double p = Eased(slot, t / slot->duration);
    const float* from = &slot->from.x;
    const float* to = &slot->to.x;
    uint8_t channels = mtwnKindChannels(slot->kind);
    value[0] = value[1] = value[2] = value[3] = 0.0;
    for (uint8_t i = 0; i < channels; ++i)
    {
        value[i] = mtwnMix((double)from[i], (double)to[i], p);
    }
}

// Its change over the last millisecond to that value, as Advance measures
// the others'.
static mtwnVec4 EasedVelocity(const mtwnPlayerSlot* slot, double t, const double value[4])
{
    double earlier = t > s_velocitySpan ? t - s_velocitySpan : 0.0;
    double q = Eased(slot, earlier / slot->duration);
    const float* from = &slot->from.x;
    const float* to = &slot->to.x;
    uint8_t channels = mtwnKindChannels(slot->kind);
    float velocity[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    for (uint8_t i = 0; i < channels; ++i)
    {
        double before = mtwnMix((double)from[i], (double)to[i], q);
        velocity[i] =
            mtwnVelocityFloat((mtwnNumberHeld(value[i]) - mtwnNumberHeld(before)) / (t - earlier));
    }
    return (mtwnVec4){velocity[0], velocity[1], velocity[2], velocity[3]};
}

// The seconds after its delay a transition has run at the host's time.
static double Elapsed(const mtwnPlayerSlot* slot, uint64_t timeNs)
{
    // Its start is the time of the first update it saw, and updates never
    // go back.
    MTWN_ASSERT(timeNs >= slot->startNs);
    return (double)(timeNs - slot->startNs) / 1e9 - slot->delay;
}

static MTWN_NOINLINE mtwnVec4 LazyVelocity(const mtwnContext* context, uint32_t index1)
{
    const mtwnPlayerSlot* slot = mtwnPlayerAt(context, mtwnTargetAt(context, index1)->header.link);
    // Only WriteEased leaves a velocity lazy, and a transition's end
    // settles it before the link goes.
    MTWN_ASSERT(slot->type == MTWN_PLAYER_TRANSITION && IsEasedNumbers(slot));
    double t = Elapsed(slot, context->lastTimeNs);
    double value[4];
    EasedValue(slot, t, value);
    return EasedVelocity(slot, t, value);
}

mtwnVec4 mtwnTargetVelocity(const mtwnContext* context, uint32_t index1)
{
    const mtwnTargetSlot* target = mtwnTargetAt(context, index1);
    uint8_t lazy = target->header.flags & (MTWN_TARGET_LAZY | MTWN_TARGET_CLIP_LAZY);
    if (lazy == 0)
    {
        return target->velocity;
    }
    // One writer's lazy velocity at a time: every write clears both flags,
    // then WriteEased or a clip's lazy write sets its own.
    MTWN_ASSERT(lazy != (MTWN_TARGET_LAZY | MTWN_TARGET_CLIP_LAZY));
    // A clip's track's only once a clip's player exists, so its part is
    // there to compute it.
    MTWN_ASSERT(lazy == MTWN_TARGET_LAZY || context->clipUpdate != NULL);
    return lazy == MTWN_TARGET_LAZY ? LazyVelocity(context, index1)
                                    : context->clipUpdate->velocity(context, target);
}

double mtwnEasedProgress(const mtwnContext* context, uint32_t player)
{
    const mtwnPlayerSlot* slot = mtwnPlayerAt(context, player);
    double t = slot->started ? Elapsed(slot, context->lastTimeNs) : -1.0;
    return t < 0.0 ? 0.0 : Eased(slot, t / slot->duration);
}

void mtwnSettleVelocity(mtwnContext* context, uint32_t index1)
{
    mtwnTargetSlot* target = mtwnTargetAt(context, index1);
    if ((target->header.flags & (MTWN_TARGET_LAZY | MTWN_TARGET_CLIP_LAZY)) != 0)
    {
        target->velocity = mtwnTargetVelocity(context, index1);
        target->header.flags &= (uint8_t)~(MTWN_TARGET_LAZY | MTWN_TARGET_CLIP_LAZY);
    }
}

// The host time a transition's delay ends, in seconds.
static double Begin(const mtwnPlayerSlot* slot)
{
    return (double)slot->startNs / 1e9 + slot->delay;
}

void mtwnWriteTarget(mtwnContext* context, uint32_t index1, mtwnVec4 value, mtwnVec4 velocity)
{
    WriteTarget(context, index1, value, velocity);
}

// Writes an eased transition of numbers' value at t, its velocity left
// to be computed when read.
static void WriteEased(mtwnContext* context, const mtwnPlayerSlot* slot, uint32_t target, double t)
{
    MTWN_ASSERT(IsEasedNumbers(slot));
    double now[4];
    EasedValue(slot, t, now);
    if (slot->kind == mtwn_valueInteger)
    {
        now[0] = round(now[0]);
    }
    // Its velocity, not a clip's track's left lazy under it this update.
    mtwnTargetSlot* written = mtwnTargetAt(context, target);
    written->header.flags =
        (uint8_t)((written->header.flags & ~(MTWN_TARGET_UNWRITTEN | MTWN_TARGET_CLIP_LAZY)) |
                  MTWN_TARGET_LAZY);
    // Numbers only (IsEasedNumbers): a scale is a path kind, written by
    // Sample.
    mtwnVec4 value =
        mtwnNumbersHeld((mtwnVec4){(float)now[0], (float)now[1], (float)now[2], (float)now[3]});
    WriteValue(context, target, value);
}

// Records a transition's start once its delay ends, and ends it at its end:
// true when it ended. Under reduced motion one not essential ends now, its
// delay skipped, its records at this time.
static bool StartOrEnd(mtwnContext* context, uint32_t index1, double t, uint64_t timeNs)
{
    mtwnPlayerSlot* slot = mtwnPlayerAt(context, index1);
    bool reduced = context->reducedMotion && !slot->essential;
    if ((t >= 0.0 || reduced) && !slot->begun)
    {
        // Its delay has ended: its start, at that time.
        slot->begun = true;
        double begin = t >= 0.0 ? Begin(slot) : (double)timeNs / 1e9;
        mtwnPlayerEvent(context, index1, mtwn_eventStart, begin, 0.0);
    }
    if (t < slot->endTime && !reduced)
    {
        return false;
    }
    bool ran = t >= slot->endTime;
    double ended = ran ? Begin(slot) + slot->endTime : (double)timeNs / 1e9;
    double along = ran || isfinite(slot->endTime) ? slot->endTime : (t > 0.0 ? t : 0.0);
    mtwnPlayerEvent(context, index1, mtwn_eventEnd, ended, along);
    // The end: committed as the base, and the transition freed.
    uint32_t target = slot->header.link;
    mtwnTargetSlot* written = mtwnTargetAt(context, target);
    written->base = slot->to;
    WriteTarget(context, target, slot->to, (mtwnVec4){0});
    mtwnMarkChanged(context, target);
    mtwnEndTransition(context, index1);
    return true;
}

// A curve's own speed is the value's where the value is not held; held
// past the floats a number is still, as the speed the values held give.
static void HoldStill(const double now[4], double speed[4])
{
    for (int i = 0; i < 4; ++i)
    {
        speed[i] = fabs(now[i]) <= 0x1.fffffep127 ? speed[i] : 0.0;
    }
}

static void Advance(mtwnContext* context, uint32_t index1, uint64_t timeNs)
{
    mtwnPlayerSlot* slot = mtwnPlayerAt(context, index1);
    uint32_t target = slot->header.link;
    if (!slot->started)
    {
        slot->started = true;
        slot->startNs = timeNs;
    }
    double t = Elapsed(slot, timeNs);
    if (StartOrEnd(context, index1, t, timeNs))
    {
        return;
    }
    // The delay holds the start at rest. At its end a spring or decay of
    // numbers begins with its start velocity; the others have no last
    // millisecond yet, so no velocity.
    bool analytic = slot->transitionKind != mtwn_transitionEasing && !mtwnIsPathKind(slot->kind);
    if (t < 0.0 || (t == 0.0 && !analytic))
    {
        WriteTarget(context, target, slot->from, (mtwnVec4){0});
        return;
    }
    double now[4];
    double speed[4];
    if (IsEasedNumbers(slot))
    {
        WriteEased(context, slot, target, t);
        return;
    }
    bool sampledSpeed = Sample(slot, t, now, speed);
    if (!sampledSpeed)
    {
        // The change over the curve's last millisecond, from its start
        // before that; none for a discrete value.
        double earlier = t > s_velocitySpan ? t - s_velocitySpan : 0.0;
        double before[4];
        double unused[4];
        Sample(slot, earlier, before, unused);
        uint8_t channels = slot->kind == mtwn_valueDiscrete ? 0 : mtwnKindChannels(slot->kind);
        speed[0] = speed[1] = speed[2] = speed[3] = 0.0;
        for (uint8_t i = 0; i < channels; ++i)
        {
            speed[i] = (mtwnNumberHeld(now[i]) - mtwnNumberHeld(before[i])) / (t - earlier);
        }
        // A scale's speed is that of the value the target holds: past the
        // floats' range both ends are held at its bound, and still (two
        // infinite samples gave a NaN).
        if (slot->kind == mtwn_valueScale)
        {
            speed[0] = (mtwnScaleHeld(now[0]) - mtwnScaleHeld(before[0])) / (t - earlier);
        }
    }
    // An integer's value rounds to nearest, ties away from zero; its
    // velocity is the unrounded curve's.
    if (slot->kind == mtwn_valueInteger)
    {
        now[0] = round(now[0]);
    }
    float value[4];
    float velocity[4];
    if (sampledSpeed)
    {
        HoldStill(now, speed);
    }
    for (int i = 0; i < 4; ++i)
    {
        value[i] = (float)now[i];
        velocity[i] = mtwnVelocityFloat(speed[i]);
    }
    if (slot->kind == mtwn_valueScale)
    {
        // A scale's curve speed is a decay's, never past the floats;
        // a spring's is taken from the values held, above.
        value[0] = mtwnScaleFloat(now[0]);
        velocity[0] = mtwnVelocityFloat(speed[0]);
    }
    // Numbers held within the floats; a scale's value
    // is within them already.
    WriteTarget(context, target,
                mtwnNumbersHeld((mtwnVec4){value[0], value[1], value[2], value[3]}),
                (mtwnVec4){velocity[0], velocity[1], velocity[2], velocity[3]});
}

static MTWN_NOINLINE void UpdateWorker(mtwnContext* context, uint64_t beforeNs, uint64_t timeNs)
{
    double now = (double)timeNs / 1e9;
    double before = (double)beforeNs / 1e9;
    uint32_t queued = context->eventCount;
    // Set when the first clip's player is made.
    if (context->clipUpdate != NULL)
    {
        context->clipUpdate->begin(context, before, now);
    }
    // In creation order, the composite order; a player may free itself.
    for (uint32_t index1 = context->firstPlayer; index1 != 0;)
    {
        mtwnPlayerSlot* slot = mtwnPlayerAt(context, index1);
        uint32_t later = slot->later;
        MTWN_ASSERT(slot->type == MTWN_PLAYER_TRANSITION || context->clipUpdate != NULL);
        if (slot->type == MTWN_PLAYER_TRANSITION)
        {
            // A transition and its target link to each other.
            MTWN_ASSERT(slot->header.link != 0 &&
                        mtwnTargetAt(context, slot->header.link)->header.link == index1);
            Advance(context, index1, timeNs);
        }
        else if (context->clipUpdate != NULL)
        {
            // Set whenever a clip's player exists.
            context->clipUpdate->advance(context, index1, before, now);
        }
        index1 = later;
    }
    mtwnSortEvents(context, queued);
    if (context->clipUpdate != NULL)
    {
        context->clipUpdate->end(context);
    }
}

static mtwnResult RunUpdate(mtwnContext* context, uint64_t timeNs, mtwnTargetId* changedOut,
                            uint32_t changedCapacity, uint32_t* changedTotalOut)
{
    if (context == NULL)
    {
        return mtwn_errorInvalid;
    }
    // The first update may come at any time: none is below 0.
    if ((changedOut == NULL && changedCapacity != 0) || timeNs < context->lastTimeNs)
    {
        mtwnCountMisuse(context);
        return mtwn_errorInvalid;
    }
    uint64_t beforeNs = context->lastTimeNs;
    context->lastTimeNs = timeNs;
    UpdateWorker(context, beforeNs, timeNs);
    for (uint32_t i = 0; i < context->changedCount; ++i)
    {
        uint32_t index1 = context->changed[i];
        mtwnTargetSlot* target = mtwnTargetAt(context, index1);
        // On the list once: its flag is still set, cleared only here.
        MTWN_ASSERT((target->header.flags & MTWN_TARGET_CHANGED) != 0);
        target->header.flags &= (uint8_t)~MTWN_TARGET_CHANGED;
        if (i < changedCapacity)
        {
            changedOut[i] = (mtwnTargetId){index1, target->header.generation};
        }
    }
    if (changedTotalOut != NULL)
    {
        *changedTotalOut = context->changedCount;
    }
    context->changedCount = 0;
    return mtwn_success;
}

static MTWN_NOINLINE mtwnResult mtwnContext_UpdateWork(mtwnContext* context, uint64_t timeNs,
                                                       mtwnTargetId* changedOut,
                                                       uint32_t changedCapacity,
                                                       uint32_t* changedTotalOut)
{
    // Cleared first, so a busy or missing context's failure clears it too.
    if (changedTotalOut != NULL)
    {
        *changedTotalOut = 0;
    }
    if (!mtwnEnter(context))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunUpdate(context, timeNs, changedOut, changedCapacity, changedTotalOut);
    mtwnLeave(context);
    return result;
}

mtwnResult mtwnContext_Update(mtwnContext* context, uint64_t timeNs, mtwnTargetId* changedOut,
                              uint32_t changedCapacity, uint32_t* changedTotalOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result =
        mtwnContext_UpdateWork(context, timeNs, changedOut, changedCapacity, changedTotalOut);
    mtwnFloatEnvLeave(env);
    return result;
}
