// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Timed records follow their easing from where the property was to its
// target; spring records follow the closed-form spring from their start
// offset and velocity. A record whose node is gone is freed the next time
// transitions advance.

#include "animation.h"

#include "property.h"

#include <math.h>
#include <string.h>

#define NANOSECONDS 1e9
#define TWO_PI      6.28318530717958647692

// A spring still moving after this long is put at its target.
#define LONGEST_SPRING 60.0

const muiTransitionSpec* muiFindSpec(const muiAnimationStore* store, muiTransitionId id)
{
    uint32_t slot = muiPoolResolve(&store->specPool, id.index1, id.generation);
    return slot != 0 ? &store->specs[slot - 1] : nullptr;
}

uint32_t muiFindAnimation(const muiMotion* motion, uint32_t node, muiProperty property)
{
    for (uint32_t at = motion->styles[node - 1].firstAnimation; at != 0;
         at = motion->store->records[at - 1].next)
    {
        if (motion->store->records[at - 1].property == property)
        {
            return at;
        }
    }
    return 0;
}

static muiValuesRef NodeValues(const muiMotion* motion, uint32_t node)
{
    return (muiValuesRef){&motion->nodes[node - 1].style, &motion->visuals[node - 1],
                          &motion->texts[node - 1], &motion->interactions[node - 1]};
}

// A moved layout property lays the node out again; a visual one only
// paints it again; a text one recomputes the node's text, which reaches
// its inheriting children and what host content reads.
static void MarkMoved(const muiMotion* motion, uint32_t node, muiProperty property)
{
    switch (MUI_PROPERTY_GROUP(property))
    {
    case mui_groupLayout:
        muiTreeMarkLayout(motion->tree, node);
        break;
    case mui_groupVisual:
        muiTreeMark(motion->tree, node, mui_stagePaint);
        break;
    default:
    {
        const muiTextNodes text = {motion->tree, motion->nodes, motion->texts, motion->textRecords};
        muiInheritText(&text, node);
        break;
    }
    }
}

static void Unlink(const muiMotion* motion, uint32_t node, uint32_t record)
{
    uint32_t* link = &motion->styles[node - 1].firstAnimation;
    while (*link != record)
    {
        link = &motion->store->records[*link - 1].next;
    }
    *link = motion->store->records[record - 1].next;
}

// Frees a record; node is its live node, or 0 when the node is gone.
static void Release(const muiMotion* motion, uint32_t node, uint32_t record)
{
    if (node != 0)
    {
        Unlink(motion, node, record);
    }
    muiPoolGive(&motion->store->pool, record);
    motion->store->running--;
}

static double Seconds(uint64_t later, uint64_t earlier)
{
    return later > earlier ? (double)(later - earlier) / NANOSECONDS : 0.0;
}

// Where a timed record is at elapsed seconds into its motion, as the
// share of the way its easing gives.
static double TimedProgress(const muiAnimation* animation, double elapsed)
{
    if (animation->seconds <= 0.0 || elapsed >= animation->seconds)
    {
        return 1.0;
    }
    const muiTransitionSpec* spec = &animation->spec;
    double x = elapsed / animation->seconds;
    return spec->def.easing == mui_easingLinear ? x : muiEase(&spec->curve, x);
}

// A record's channels and velocity at nowNs; whether it has arrived.
static bool Sample(const muiAnimation* animation, uint64_t nowNs, float value[MUI_MAX_CHANNELS],
                   double velocity[MUI_MAX_CHANNELS])
{
    for (uint32_t i = 0; i < MUI_MAX_CHANNELS; i++)
    {
        velocity[i] = 0.0;
    }
    double elapsed = Seconds(nowNs, animation->startNs);
    if (animation->spec.def.kind == mui_transitionTimed)
    {
        double progress = nowNs < animation->startNs ? 0.0 : TimedProgress(animation, elapsed);
        for (uint32_t i = 0; i < animation->channels; i++)
        {
            double span = (double)animation->to[i] - (double)animation->from[i];
            value[i] = (float)((double)animation->from[i] + span * progress);
        }
        return nowNs >= animation->startNs && elapsed >= animation->seconds;
    }
    bool resting = true;
    double toSpeed = TWO_PI * (double)animation->spec.def.frequency;
    muiSpringTime time = muiSpringTimeAt(&animation->shape, elapsed);
    for (uint32_t i = 0; i < animation->channels; i++)
    {
        double offset = (double)animation->from[i] - (double)animation->to[i];
        if (nowNs >= animation->startNs)
        {
            muiSpringAt(&animation->shape, &time, animation->starts[i], &offset, &velocity[i]);
        }
        value[i] = (float)((double)animation->to[i] + offset);
        double rest = (double)animation->rest[i];
        resting = resting && fabs(offset) <= rest && fabs(velocity[i]) <= rest * toSpeed;
    }
    return nowNs >= animation->startNs && (resting || elapsed >= LONGEST_SPRING);
}

// CSS Transitions' shortening of a reversal: the share of the way the
// old transition covered, in value, not time.
static double Shortening(const muiAnimation* old, uint64_t nowNs)
{
    double eased = nowNs < old->startNs ? 0.0 : TimedProgress(old, Seconds(nowNs, old->startNs));
    double share = eased * old->shortening + (1.0 - old->shortening);
    return fmin(fabs(share), 1.0);
}

static bool IsSame(const float a[MUI_MAX_CHANNELS], const float b[MUI_MAX_CHANNELS],
                   uint32_t channels)
{
    for (uint32_t i = 0; i < channels; i++)
    {
        if (a[i] != b[i])
        {
            return false;
        }
    }
    return true;
}

// Takes a record for a node's property: its running one, or a new one
// linked first. 0 when none is free.
static uint32_t RecordFor(const muiMotion* motion, uint32_t node, muiProperty property)
{
    uint32_t record = muiFindAnimation(motion, node, property);
    if (record != 0)
    {
        return record;
    }
    record = muiPoolTake(&motion->store->pool);
    if (record != 0)
    {
        motion->store->running++;
        motion->store->records[record - 1] = (muiAnimation){
            .next = motion->styles[node - 1].firstAnimation,
            .property = property,
        };
        motion->styles[node - 1].firstAnimation = record;
    }
    return record;
}

// Where a record starts from: the current channels and, when one runs,
// its velocity, and for a reversal of a timed one the start it returns
// to and its shortening.
typedef struct Origin
{
    float current[MUI_MAX_CHANNELS];
    float reversingStart[MUI_MAX_CHANNELS];
    double velocity[MUI_MAX_CHANNELS];
    double shortening;
} Origin;

static Origin OriginOf(const muiMotion* motion, uint32_t node, muiProperty property,
                       const float target[MUI_MAX_CHANNELS], const muiTransitionSpec* spec,
                       uint64_t nowNs)
{
    Origin origin = {.shortening = 1.0};
    uint32_t channels =
        muiPropertyChannels(muiConstRef(NodeValues(motion, node)), property, origin.current);
    memcpy(origin.reversingStart, origin.current, sizeof origin.current);
    uint32_t running = muiFindAnimation(motion, node, property);
    if (running == 0)
    {
        return origin;
    }
    const muiAnimation* old = &motion->store->records[running - 1];
    float sampled[MUI_MAX_CHANNELS] = {0};
    (void)Sample(old, nowNs, sampled, origin.velocity);
    if (old->spec.def.kind == mui_transitionTimed && spec->def.kind == mui_transitionTimed &&
        IsSame(target, old->reversingStart, channels))
    {
        origin.shortening = Shortening(old, nowNs);
        memcpy(origin.reversingStart, old->to, sizeof old->to);
    }
    return origin;
}

bool muiStartAnimation(const muiMotion* motion, uint32_t node, muiProperty property,
                       muiConstValuesRef target, const muiTransitionSpec* spec, uint64_t nowNs)
{
    float to[MUI_MAX_CHANNELS] = {0};
    uint32_t channels = muiPropertyChannels(target, property, to);
    const Origin origin = OriginOf(motion, node, property, to, spec, nowNs);
    uint32_t record = RecordFor(motion, node, property);
    if (record == 0)
    {
        return false;
    }
    muiAnimation* animation = &motion->store->records[record - 1];
    animation->node = muiTreeIdOf(motion->tree, node);
    animation->channels = channels;
    animation->spec = *spec;
    animation->changedNs = nowNs;
    animation->startNs = nowNs + spec->def.delayNs;
    animation->seconds = (double)spec->def.durationNs / NANOSECONDS * origin.shortening;
    animation->shortening = origin.shortening;
    muiReadPropertyValue(target, property, &animation->target);
    animation->shape =
        muiMakeSpringShape((double)spec->def.frequency, (double)spec->def.dampingRatio);
    double w0 = TWO_PI * (double)spec->def.frequency;
    for (uint32_t i = 0; i < MUI_MAX_CHANNELS; i++)
    {
        animation->from[i] = origin.current[i];
        animation->to[i] = to[i];
        animation->reversingStart[i] = origin.reversingStart[i];
        double offset = (double)origin.current[i] - (double)to[i];
        animation->starts[i] = muiStartSpring(&animation->shape, offset, origin.velocity[i]);
        // At rest within a thousandth of the channel's way: its offset, or
        // as far as its speed alone would carry it in a radian of its
        // motion.
        animation->rest[i] = (float)(fmax(fabs(offset), fabs(origin.velocity[i]) / w0) * 1e-3);
    }
    return true;
}

void muiStopAnimation(const muiMotion* motion, uint32_t node, muiProperty property)
{
    uint32_t record = muiFindAnimation(motion, node, property);
    if (record != 0)
    {
        Release(motion, node, record);
    }
}

void muiAdvanceAnimations(const muiMotion* motion, uint64_t nowNs, bool finish)
{
    muiAnimationStore* store = motion->store;
    for (uint32_t record = 1; store->running != 0 && record <= store->pool.used; record++)
    {
        if (!store->pool.slots[record - 1].live)
        {
            continue;
        }
        const muiAnimation* animation = &store->records[record - 1];
        uint32_t node = muiTreeResolve(motion->tree, animation->node);
        if (node == 0)
        {
            Release(motion, 0, record);
            continue;
        }
        float value[MUI_MAX_CHANNELS] = {0};
        double velocity[MUI_MAX_CHANNELS] = {0};
        bool arrived = Sample(animation, nowNs, value, velocity) || finish;
        if (arrived)
        {
            muiWritePropertyValue(NodeValues(motion, node), animation->property,
                                  &animation->target);
        }
        else
        {
            muiSetPropertyChannels(NodeValues(motion, node), animation->property, value);
        }
        MarkMoved(motion, node, animation->property);
        if (arrived)
        {
            Release(motion, node, record);
        }
    }
}

bool muiIsAnimatingUnder(const muiAnimationStore* store, const muiTree* tree, uint32_t root)
{
    for (uint32_t record = 1; store->running != 0 && record <= store->pool.used; record++)
    {
        if (!store->pool.slots[record - 1].live)
        {
            continue;
        }
        uint32_t node = muiTreeResolve(tree, store->records[record - 1].node);
        if (node != 0 && muiTreeIsAncestor(tree, root, node))
        {
            return true;
        }
    }
    return false;
}
