// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A clip's player sampled into its targets.
// The nodes are walked in the order they were added, each
// after its parent: a timeline passes its children its transformed time
// (Web Animations Level 2), a track writes its value into the target bound
// to its slot, later tracks over earlier ones. A moving player's velocity
// is its values' change over the last millisecond, as a transition's is.

#include "bounds_internal.h"
#include "compose.h"
#include "context_internal.h"
#include "elementary.h"
#include "fpenv.h"
#include "work.h"

#include "maul-tween/path.h"

#include <assert.h>
#include <math.h>
#include <string.h>

// The span the velocity is measured over, as a transition's.
static const double s_velocitySpan = 0.001;

// The sampling loop's track result stays in it though the sharing calls
// it too: left out of line, as GCC 14 leaves it, the loop costs every
// player 2% more.
#if defined(_MSC_VER) && !defined(__clang__)
#define MTWN_ALWAYS_INLINE __forceinline
#else
#define MTWN_ALWAYS_INLINE inline __attribute__((always_inline))
#endif

uint32_t mtwnBoundTarget(const mtwnContext* context, const mtwnClipPlay* play, uint32_t slot)
{
    mtwnTargetId id = context->bindings.entries[play->bindingStart + slot].target;
    if (id.index1 == 0 || mtwnPoolFind(&context->targets, id.index1, id.generation) == NULL)
    {
        return 0;
    }
    return id.index1;
}

void mtwnMarkUnwritten(mtwnContext* context, const mtwnClipPlay* play)
{
    for (uint32_t s = 0; s < play->bindingCount; ++s)
    {
        uint32_t target = mtwnBoundTarget(context, play, s);
        if (target != 0)
        {
            mtwnTargetAt(context, target)->header.flags |= MTWN_TARGET_UNWRITTEN;
        }
    }
}

// Whether a binding's target is its alone: alive, named by no other
// binding and without a transition.
// mtwnPoolFind's test, inline: every binding of every grouped player
// passes here each update.
static bool IsAlone(const mtwnContext* context, mtwnTargetId id)
{
    if (id.index1 == 0 || id.index1 > context->targets.high)
    {
        return false;
    }
    const mtwnTargetSlot* slot = mtwnTargetAt(context, id.index1);
    return (slot->header.flags & MTWN_SLOT_ALIVE) != 0 &&
           slot->header.generation == id.generation && slot->clipWriters == 1u &&
           slot->header.link == 0;
}

uint32_t mtwnMarkUnwrittenOf(mtwnContext* context, const mtwnClipPlay* play, bool alone,
                             uint32_t* othersOut)
{
    uint32_t marked = 0;
    uint32_t others = 0;
    for (uint32_t s = 0; s < play->bindingCount; ++s)
    {
        mtwnBound* bound = &context->bindings.entries[play->bindingStart + s];
        if (alone && !context->aloneFresh)
        {
            // In the player's group, which alone writes its bindings.
            bound->alone = IsAlone(context, bound->target);
        }
        // Kept since the writers last changed, a binding's alone is still
        // what its target's writers give.
        MTWN_ASSERT(!alone || bound->alone == IsAlone(context, bound->target));
        bool taken = bound->alone == alone;
        others += !taken && bound->target.index1 != 0 ? 1u : 0u;
        uint32_t target =
            !taken ? 0 : (alone ? bound->target.index1 : mtwnBoundTarget(context, play, s));
        if (target != 0)
        {
            mtwnTargetAt(context, target)->header.flags |= MTWN_TARGET_UNWRITTEN;
            ++marked;
        }
    }
    if (othersOut != NULL)
    {
        *othersOut = others;
    }
    return marked;
}

void mtwnResetUnwritten(mtwnContext* context, const mtwnClipPlay* play)
{
    for (uint32_t s = 0; s < play->bindingCount; ++s)
    {
        uint32_t target = mtwnBoundTarget(context, play, s);
        if (target != 0 &&
            (mtwnTargetAt(context, target)->header.flags & MTWN_TARGET_UNWRITTEN) != 0)
        {
            mtwnWriteTarget(context, target, mtwnTargetAt(context, target)->base, (mtwnVec4){0});
        }
    }
}

// Puts a group's alone target on the group's changed targets, once.
static void MarkChangedIn(mtwnContext* context, mtwnWork* group, uint32_t index1)
{
    mtwnTargetSlot* slot = mtwnTargetAt(context, index1);
    if ((slot->header.flags & MTWN_TARGET_CHANGED) == 0)
    {
        // Room held for it: a player with an alone target runs in its
        // group only where its bindings' changes fit (Fits in
        // src/clip_tasks.c).
        MTWN_ASSERT(group->changeCount < group->changeCapacity);
        slot->header.flags |= MTWN_TARGET_CHANGED;
        group->changes[group->changeCount++] = index1;
    }
}

// Whether a target slot is alone: alive, with one clip binding as its
// writer and no transition. Only an assertion calls it.
[[maybe_unused]] static inline bool AloneSlot(const mtwnTargetSlot* slot)
{
    return (slot->header.flags & MTWN_SLOT_ALIVE) != 0 && slot->clipWriters == 1u &&
           slot->header.link == 0;
}

// Writes a target's value and velocity as mtwnWriteTarget does; for a
// group's alone target, its change staged in the group's work.
static MTWN_ALWAYS_INLINE void WriteTargetIn(mtwnContext* context, mtwnWork* group, uint32_t index1,
                                             mtwnVec4 value, mtwnVec4 velocity)
{
    if (group == NULL)
    {
        mtwnWriteTarget(context, index1, value, velocity);
        return;
    }
    mtwnTargetSlot* slot = mtwnTargetAt(context, index1);
    // A group writes in place only a target no other writer shares.
    MTWN_ASSERT(AloneSlot(slot));
    slot->header.flags &=
        (uint8_t)~(MTWN_TARGET_UNWRITTEN | MTWN_TARGET_LAZY | MTWN_TARGET_CLIP_LAZY);
    uint32_t was[4];
    uint32_t now[4];
    memcpy(was, &slot->value, sizeof was);
    memcpy(now, &value, sizeof now);
    if (was[0] != now[0] || was[1] != now[1] || was[2] != now[2] || was[3] != now[3])
    {
        slot->value = value;
        MarkChangedIn(context, group, index1);
    }
    slot->velocity = velocity;
}

void mtwnResetAlone(mtwnContext* context, const mtwnClipPlay* play, mtwnWork* group)
{
    for (uint32_t s = 0; s < play->bindingCount; ++s)
    {
        const mtwnBound* bound = &context->bindings.entries[play->bindingStart + s];
        uint32_t target = bound->alone ? bound->target.index1 : 0;
        if (target != 0 &&
            (mtwnTargetAt(context, target)->header.flags & MTWN_TARGET_UNWRITTEN) != 0)
        {
            WriteTargetIn(context, group, target, mtwnTargetAt(context, target)->base,
                          (mtwnVec4){0});
        }
    }
}

// The time a timeline gives its children: its transformed progress times
// its iteration duration; with an infinite duration, its active time at its
// rate going forwards, its end going backwards. NaN when it has no effect.
static double ChildTime(const mtwnTiming* timing, double local, bool backwards, bool* forwards)
{
    mtwnTimingResult result;
    mtwnComputeTimingWork(timing, local, backwards, &result);
    if (forwards != NULL)
    {
        *forwards = result.forwards;
    }
    if (isnan(result.transformedProgress))
    {
        return (double)NAN;
    }
    if (isinf(timing->duration))
    {
        return result.forwards ? result.activeTime * timing->playbackRate : HUGE_VAL;
    }
    return result.transformedProgress * timing->duration;
}

// A track's progress and iteration now and a moment ago (NaN when it has
// none), from its parent's times.
typedef struct TrackTimes
{
    double progress;
    double iteration;
    double earlier;
    double earlierIteration;
    // Whether earlier is worked out: only a velocity computed now needs it.
    bool earlierKnown;
} TrackTimes;

// A track's progress at a local time, NaN when it has no effect, and its
// current iteration.
static double TrackProgress(const mtwnClipNode* node, double local, bool backwards,
                            double* iterationOut)
{
    *iterationOut = 0.0;
    if (isnan(local))
    {
        return (double)NAN;
    }
    mtwnTimingResult result;
    mtwnComputeTimingWork(&node->timing, local, backwards, &result);
    *iterationOut = result.iteration;
    return result.transformedProgress;
}

// A unit rotation turned k times: its angle times k about its axis.
static mtwnVec4 Power(mtwnVec4 q, double k)
{
    double x = (double)q.x;
    double y = (double)q.y;
    double z = (double)q.z;
    double w = (double)q.w;
    double sine = sqrt(x * x + y * y + z * z);
    if (!(sine > 0.0))
    {
        return (mtwnVec4){0.0f, 0.0f, 0.0f, 1.0f};
    }
    double half = mtwnAtan2(sine, w) * k;
    double sinHalf = 0.0;
    double cosHalf = 0.0;
    mtwnSinCos(half, &sinHalf, &cosHalf);
    double s = sinHalf / sine;
    return (mtwnVec4){(float)(x * s), (float)(y * s), (float)(z * s), (float)cosHalf};
}

// An incremental track's value at an iteration: moved on k steps.
static MTWN_NOINLINE mtwnVec4 MovedOn(const mtwnClipNode* node, mtwnVec4 value, double k)
{
    if (k == 0.0)
    {
        return value;
    }
    if (node->track.kind == mtwn_valueScale)
    {
        // The ratio of its last keyframe to its first, k times, in double.
        const mtwnPath* path = &node->track.path;
        double first = (double)path->keyframes[0].value.x;
        double last = (double)path->keyframes[path->count - 1].value.x;
        double factor = mtwnExp(k * (mtwnLog(last) - mtwnLog(first)));
        return (mtwnVec4){mtwnScaleFloat((double)value.x * factor), 0.0f, 0.0f, 0.0f};
    }
    if (node->track.kind == mtwn_valueRotation)
    {
        mtwnVec4 turn = Power(node->step, k);
        mtwnMoving under = {turn, {0.0f, 0.0f, 0.0f, 0.0f}};
        mtwnMoving over = {value, {0.0f, 0.0f, 0.0f, 0.0f}};
        // The turn so far, then the iteration's own path in its frame.
        return mtwnCompose(mtwn_valueRotation, mtwn_compositeAdd, 1.0, under, over).value;
    }
    // The last keyframe minus the first, exact in double: as a float a step
    // past the floats' range would be infinite.
    const mtwnPath* path = &node->track.path;
    mtwnVec4 first = path->keyframes[0].value;
    mtwnVec4 last = path->keyframes[path->count - 1].value;
    return mtwnNumbersHeld(
        (mtwnVec4){(float)((double)value.x + k * ((double)last.x - (double)first.x)),
                   (float)((double)value.y + k * ((double)last.y - (double)first.y)),
                   (float)((double)value.z + k * ((double)last.z - (double)first.z)),
                   (float)((double)value.w + k * ((double)last.w - (double)first.w))});
}

// Kept apart from MovedOn, the frame's common case, a track that does not
// move on, stays a test.
static mtwnVec4 Moved(const mtwnClipNode* node, mtwnVec4 value, double k)
{
    return node->incremental ? MovedOn(node, value, k) : value;
}

// A track's value, an integer's unrounded for its velocity.
static mtwnVec4 Unrounded(const mtwnTrack* track, double progress)
{
    if (track->kind != mtwn_valueInteger)
    {
        return mtwnSampleTrackWork(track, progress);
    }
    mtwnTrack scalar = *track;
    scalar.kind = mtwn_valueScalar;
    return mtwnSampleTrackWork(&scalar, progress);
}

// A clip track's velocity left lazy: the parent's time a moment ago, and
// which track, the top bit of its node index whether time ran backwards in
// its parent. It is stored in the target's velocity field, which it fits.
typedef struct mtwnClipVelocity
{
    double before;
    uint32_t clip;
    uint32_t node;
} mtwnClipVelocity;

static_assert(sizeof(mtwnClipVelocity) == sizeof(mtwnVec4), "a lazy velocity fits its field");

static const uint32_t s_backwardsBit = 0x80000000u;

// A track's velocity at its value now, from its progress a moment ago:
// at rest when it had none then, or for a discrete value.
static mtwnVec4 TrackVelocity(const mtwnClipNode* node, mtwnVec4 value, double earlier,
                              double earlierIteration)
{
    float velocity[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (!isnan(earlier) && node->track.kind != mtwn_valueDiscrete)
    {
        mtwnVec4 before = Moved(node, Unrounded(&node->track, earlier), earlierIteration);
        const float* a = &value.x;
        const float* b = &before.x;
        for (int i = 0; i < 4; ++i)
        {
            velocity[i] = mtwnVelocityFloat(((double)a[i] - (double)b[i]) / s_velocitySpan);
        }
    }
    return (mtwnVec4){velocity[0], velocity[1], velocity[2], velocity[3]};
}

// Whether a target's velocity is a clip track's left lazy: the track a
// node of its clip. Only an assertion calls it.
[[maybe_unused]] static inline bool LazyTrack(const mtwnClipSlot* clip,
                                              const mtwnClipVelocity* lazy)
{
    uint32_t node = lazy->node & ~s_backwardsBit;
    return node < clip->nodeCount && clip->nodes[node].kind == mtwn_nodeTrack;
}

mtwnVec4 mtwnClipTrackVelocity(const mtwnContext* context, const mtwnTargetSlot* target)
{
    MTWN_ASSERT((target->header.flags & MTWN_TARGET_CLIP_LAZY) != 0);
    mtwnClipVelocity lazy;
    memcpy(&lazy, &target->velocity, sizeof lazy);
    const mtwnClipSlot* clip = mtwnClipAt(context, lazy.clip);
    MTWN_ASSERT(LazyTrack(clip, &lazy));
    const mtwnClipNode* node = &clip->nodes[lazy.node & ~s_backwardsBit];
    double earlierIteration = 0.0;
    double earlier = TrackProgress(node, lazy.before - node->start,
                                   (lazy.node & s_backwardsBit) != 0, &earlierIteration);
    return TrackVelocity(node, target->value, earlier, earlierIteration);
}

void mtwnSettleClipVelocities(mtwnContext* context, uint32_t clip)
{
    for (uint32_t i = 1; i <= context->targets.high; ++i)
    {
        mtwnTargetSlot* target = mtwnTargetAt(context, i);
        mtwnClipVelocity lazy;
        memcpy(&lazy, &target->velocity, sizeof lazy);
        if ((target->header.flags & MTWN_TARGET_CLIP_LAZY) != 0 && lazy.clip == clip)
        {
            mtwnSettleVelocity(context, i);
        }
    }
}

// Whether a track's velocity can be left to be computed when read: a
// number's, not an integer's (whose target holds its rounded value), the
// track replacing what is under it at full weight, so the target's value
// is the track's own and its velocity the track's.
static bool LeavesVelocity(const mtwnClipNode* node, const mtwnBound* bound, double weight)
{
    mtwnValueKind kind = node->track.kind;
    bool numbers = kind == mtwn_valueScalar || kind == mtwn_valueVec2 || kind == mtwn_valueVec3 ||
                   kind == mtwn_valueVec4;
    return numbers && bound->composite == mtwn_compositeReplace &&
           weight * (double)bound->weight == 1.0;
}

static TrackTimes TimesOf(const mtwnClipNode* node, const mtwnNodeTime* parent)
{
    TrackTimes t = {(double)NAN, 0.0, (double)NAN, 0.0, false};
    t.progress = TrackProgress(node, parent->now - node->start, parent->backwards, &t.iteration);
    return t;
}

static void KnowEarlier(TrackTimes* t, const mtwnClipNode* node, const mtwnNodeTime* parent,
                        bool moving)
{
    if (!t->earlierKnown && moving && !isnan(t->progress))
    {
        t->earlier = TrackProgress(node, parent->before - node->start, parent->backwards,
                                   &t->earlierIteration);
    }
    t->earlierKnown = true;
}

// Where a track is, for its target's velocity left lazy.
typedef struct TrackPlace
{
    uint32_t clip;
    uint32_t node;
    const mtwnNodeTime* parent;
    bool moving;
} TrackPlace;

// A track's result, which reads no target: false when it has no effect.
static MTWN_ALWAYS_INLINE bool TrackResult(const mtwnClipNode* node, const mtwnBound* bound,
                                           double weight, const TrackPlace* place,
                                           TrackTimes* times, mtwnTrackOut* out)
{
    double progress = times->progress;
    if (isnan(progress))
    {
        return false;
    }
    out->value = Moved(node, Unrounded(&node->track, progress), times->iteration);
    out->weight = weight * (double)bound->weight;
    out->kind = node->track.kind;
    out->composite = bound->composite;
    out->lazy = place->moving && LeavesVelocity(node, bound, weight);
    if (out->lazy)
    {
        // Replacing at full weight composes to the track's value; its
        // velocity is worked out when read.
        mtwnClipVelocity lazy = {place->parent->before, place->clip,
                                 place->node | (place->parent->backwards ? s_backwardsBit : 0u)};
        memcpy(&out->velocity, &lazy, sizeof lazy);
    }
    else
    {
        KnowEarlier(times, node, place->parent, place->moving);
        out->velocity = TrackVelocity(node, out->value, times->earlier, times->earlierIteration);
    }
    return true;
}

// Whether a track's result is one LeavesVelocity left lazy: numbers
// replacing at full weight. Only an assertion calls it.
[[maybe_unused]] static inline bool LeftLazy(const mtwnTrackOut* out)
{
    bool numbers = out->kind == mtwn_valueScalar || out->kind == mtwn_valueVec2 ||
                   out->kind == mtwn_valueVec3 || out->kind == mtwn_valueVec4;
    return numbers && out->composite == mtwn_compositeReplace && out->weight == 1.0;
}

// What lies under a track in its target: what earlier players wrote in
// this update, or the base.
static MTWN_ALWAYS_INLINE void ApplyTrack(mtwnContext* context, const mtwnTrackOut* out,
                                          mtwnWork* group)
{
    uint32_t target = out->target;
    mtwnTargetSlot* slot = mtwnTargetAt(context, target);
    mtwnVec4 value = out->value;
    if (out->lazy)
    {
        // Its value is the track's own, so the velocity read later is too.
        MTWN_ASSERT(LeftLazy(out));
        WriteTargetIn(context, group, target, value, (mtwnVec4){0});
        memcpy(&slot->velocity, &out->velocity, sizeof slot->velocity);
        slot->header.flags |= MTWN_TARGET_CLIP_LAZY;
    }
    else
    {
        bool unwritten = (slot->header.flags & MTWN_TARGET_UNWRITTEN) != 0;
        mtwnMoving under = {unwritten ? slot->base : slot->value,
                            unwritten ? (mtwnVec4){0.0f, 0.0f, 0.0f, 0.0f}
                                      : mtwnTargetVelocity(context, target)};
        mtwnMoving effect = {value, out->velocity};
        mtwnMoving composed = mtwnCompose(out->kind, out->composite, out->weight, under, effect);
        value = composed.value;
        WriteTargetIn(context, group, target, value, composed.velocity);
    }
    bool rebased = slot->base.x != value.x || slot->base.y != value.y || slot->base.z != value.z ||
                   slot->base.w != value.w;
    if (out->commit && rebased)
    {
        slot->base = value;
        if (group == NULL)
        {
            mtwnMarkChanged(context, target);
        }
        else
        {
            MarkChangedIn(context, group, target);
        }
    }
}

// A timeline's times for its children, from its parent's.
static void TimelineTimes(const mtwnClipNode* node, const mtwnNodeTime* parent, bool moving,
                          mtwnNodeTime* out)
{
    bool forwards = true;
    out->now = isnan(parent->now) ? (double)NAN
                                  : ChildTime(&node->timing, parent->now - node->start,
                                              parent->backwards, &forwards);
    out->before =
        moving && !isnan(parent->before)
            ? ChildTime(&node->timing, parent->before - node->start, parent->backwards, NULL)
            : (double)NAN;
    // Time runs back in a reversed iteration, and in a node played backwards.
    bool reversed = forwards != (node->timing.playbackRate > 0.0);
    out->backwards = parent->backwards != reversed;
}

void mtwnApplyTrack(mtwnContext* context, const mtwnTrackOut* out, mtwnWork* group)
{
    ApplyTrack(context, out, group);
}

// Stages a track's result in a staging work.
static void Stage(mtwnWork* work, const mtwnTrackOut* out)
{
    if (work->trackCount < work->trackCapacity)
    {
        // Field by field: a whole copy kept the serial path's result in
        // memory too, 21 instructions a track.
        mtwnTrackOut* to = &work->tracks[work->trackCount++];
        to->value = out->value;
        to->velocity = out->velocity;
        to->weight = out->weight;
        to->target = out->target;
        to->kind = out->kind;
        to->composite = out->composite;
        to->lazy = out->lazy;
        to->commit = out->commit;
        to->changesBefore = work->changeCount;
    }
    else
    {
        work->overflow = true;
    }
}

// Applies a track's result, or in a group, applies it to its alone target
// or stages it.
static void Put(mtwnContext* context, mtwnWork* work, const mtwnTrackOut* out, bool alone)
{
    if (work->tracks == NULL)
    {
        ApplyTrack(context, out, NULL);
    }
    else if (alone)
    {
        mtwnApplyTrack(context, out, work);
    }
    else
    {
        Stage(work, out);
    }
}

// A shared track's result for this player: the value and velocity data
// the last player in step worked out, its own binding's weight and
// composite. Worked out here only what this player alone needs.
static void SharedResult(const mtwnClipNode* node, const mtwnBound* bound, double weight,
                         const TrackPlace* place, mtwnSharedTrack* shared, mtwnTrackOut* out)
{
    out->value = shared->value;
    out->weight = weight * (double)bound->weight;
    out->kind = node->track.kind;
    out->composite = bound->composite;
    out->lazy = place->moving && LeavesVelocity(node, bound, weight);
    if (out->lazy)
    {
        mtwnClipVelocity lazy = {shared->parentBefore, place->clip,
                                 place->node | (shared->parentBackwards ? s_backwardsBit : 0u)};
        memcpy(&out->velocity, &lazy, sizeof lazy);
        return;
    }
    if (!shared->velocityKnown)
    {
        TrackTimes t = {shared->progress, shared->iteration, shared->earlier,
                        shared->earlierIteration, shared->earlierKnown};
        mtwnNodeTime parent = {
            0.0, shared->parentBefore, shared->parentBackwards, false, false, false, false};
        KnowEarlier(&t, node, &parent, place->moving);
        shared->earlier = t.earlier;
        shared->earlierIteration = t.earlierIteration;
        shared->earlierKnown = true;
        shared->velocity = TrackVelocity(node, out->value, t.earlier, t.earlierIteration);
        shared->velocityKnown = true;
    }
    out->velocity = shared->velocity;
}

// Keeps a track's result for the next player in step.
static void Share(mtwnSharedTrack* shared, const TrackTimes* t, const mtwnNodeTime* parent,
                  const mtwnTrackOut* out, bool effect)
{
    shared->known = true;
    shared->effect = effect;
    shared->progress = t->progress;
    shared->iteration = t->iteration;
    shared->earlier = t->earlier;
    shared->earlierIteration = t->earlierIteration;
    shared->earlierKnown = t->earlierKnown;
    shared->parentBefore = parent->before;
    shared->parentBackwards = parent->backwards;
    shared->value = effect ? out->value : (mtwnVec4){0};
    shared->velocityKnown = effect && !out->lazy;
    shared->velocity = shared->velocityKnown ? out->velocity : (mtwnVec4){0};
}

// Samples a player in step with the last of its clip in the work from
// what that one left: false when a track it needs was not worked out.
static bool SampleShared(mtwnContext* context, mtwnWork* work, const mtwnClipPlay* play,
                         const mtwnClipSlot* clip, bool moving, bool commit)
{
    mtwnSharing* sharing = &work->sharing;
    // Kept only for a clip whose nodes the sharing holds, and in step
    // only with that clip.
    MTWN_ASSERT(clip->nodeCount <= sharing->trackCapacity);
    for (uint32_t i = 1; i < clip->nodeCount; ++i)
    {
        const mtwnClipNode* node = &clip->nodes[i];
        if (node->kind == mtwn_nodeTrack && mtwnBoundTarget(context, play, node->slot) != 0 &&
            !sharing->tracks[i].known)
        {
            return false;
        }
    }
    for (uint32_t i = 1; i < clip->nodeCount; ++i)
    {
        const mtwnClipNode* node = &clip->nodes[i];
        uint32_t target =
            node->kind == mtwn_nodeTrack ? mtwnBoundTarget(context, play, node->slot) : 0;
        mtwnSharedTrack* shared = &sharing->tracks[i];
        if (target == 0 || !shared->effect)
        {
            continue;
        }
        const mtwnBound* bound = &context->bindings.entries[play->bindingStart + node->slot];
        TrackPlace place = {play->clip.index1, i, NULL, moving};
        mtwnTrackOut out;
        out.target = target;
        out.commit = commit;
        SharedResult(node, bound, play->weight, &place, shared, &out);
        // The library's own call, not the loop's copy of it.
        if (work->tracks == NULL || bound->alone)
        {
            mtwnApplyTrack(context, &out, work->tracks == NULL ? NULL : work);
        }
        else
        {
            Stage(work, &out);
        }
    }
    return true;
}

// Keeps every bound track's result of a player for the next in step, from
// the node times its sample left.
static void KeepTracks(const mtwnContext* context, mtwnSharing* sharing, const mtwnClipPlay* play,
                       const mtwnClipSlot* clip, const mtwnNodeTime* times, bool moving)
{
    MTWN_ASSERT(clip->nodeCount <= sharing->trackCapacity);
    for (uint32_t i = 1; i < clip->nodeCount; ++i)
    {
        const mtwnClipNode* node = &clip->nodes[i];
        sharing->tracks[i].known = false;
        if (node->kind != mtwn_nodeTrack || mtwnBoundTarget(context, play, node->slot) == 0)
        {
            continue;
        }
        const mtwnNodeTime* parent = &times[node->parent - 1];
        TrackTimes t = TimesOf(node, parent);
        const mtwnBound* bound = &context->bindings.entries[play->bindingStart + node->slot];
        TrackPlace place = {play->clip.index1, i, parent, moving};
        mtwnTrackOut out;
        bool effect = TrackResult(node, bound, play->weight, &place, &t, &out);
        Share(&sharing->tracks[i], &t, parent, &out, effect);
    }
}

// Whether a player is in step with the last of its clip the work sampled.
static bool InStep(const mtwnSharing* sharing, const mtwnClipPlay* play, double current,
                   double rate, bool backwards)
{
    return sharing->sampleValid && sharing->sampleClip.index1 == play->clip.index1 &&
           sharing->sampleClip.generation == play->clip.generation &&
           mtwnSameBits(sharing->current, current) && mtwnSameBits(sharing->rate, rate) &&
           sharing->backwards == backwards;
}

// Whether node i reads within its player's sample: its parent a node
// before it, whose times are worked out, and a track's slot one of the
// player's bindings. Only an assertion calls it.
[[maybe_unused]] static inline bool NodeWithin(const mtwnClipSlot* clip, uint32_t i,
                                               const mtwnClipPlay* play)
{
    const mtwnClipNode* node = &clip->nodes[i];
    return node->parent != 0 && node->parent - 1 < i &&
           (node->kind != mtwn_nodeTrack || node->slot < play->bindingCount);
}

void mtwnSampleClip(mtwnContext* context, mtwnWork* work, const mtwnClipPlay* play, double current,
                    double rate, bool backwards, bool commit)
{
    mtwnClipSlot* clip = mtwnClipAt(context, play->clip.index1);
    mtwnNodeTime* times = mtwnTimesFor(work, clip);
    // A group's times hold MTWN_GROUP_NODES nodes; a clip with more runs
    // serially, or in its own times when it has one player.
    MTWN_ASSERT(times == clip->times || clip->nodeCount <= MTWN_GROUP_NODES);
    bool moving = rate != 0.0;
    // The player's time is the root's parent's.
    mtwnNodeTime player = {current, current - rate * s_velocitySpan, backwards, false, false, false,
                           false};
    TimelineTimes(&clip->nodes[0], &player, moving, &times[0]);
    // The last track's timing results, for the tracks after it that share
    // its timing; 0 when none is kept.
    TrackTimes kept = {0.0, 0.0, 0.0, 0.0, false};
    uint32_t keptFor = 0;
    for (uint32_t i = 1; i < clip->nodeCount; ++i)
    {
        const mtwnClipNode* node = &clip->nodes[i];
        MTWN_ASSERT(NodeWithin(clip, i, play));
        const mtwnNodeTime* parent = &times[node->parent - 1];
        if (node->kind == mtwn_nodeTimeline)
        {
            TimelineTimes(node, parent, moving, &times[i]);
        }
        else if (node->kind == mtwn_nodeTrack)
        {
            // In a group an alone binding's target is alive: the writers
            // were counted after any target was destroyed.
            const mtwnBound* bound = &context->bindings.entries[play->bindingStart + node->slot];
            uint32_t target = work->tracks != NULL && bound->alone
                                  ? bound->target.index1
                                  : mtwnBoundTarget(context, play, node->slot);
            if (target != 0)
            {
                if (!(node->sharesTiming && keptFor == i))
                {
                    kept = TimesOf(node, parent);
                }
                keptFor = i + 1;
                TrackPlace place = {play->clip.index1, i, parent, moving};
                mtwnTrackOut out;
                out.target = target;
                out.commit = commit;
                if (TrackResult(node, bound, play->weight, &place, &kept, &out))
                {
                    Put(context, work, &out, bound->alone);
                }
            }
        }
    }
}

void mtwnSampleClipNear(mtwnContext* context, mtwnWork* work, const mtwnClipPlay* play,
                        double current, double rate, bool backwards, bool commit)
{
    mtwnClipSlot* clip = mtwnClipAt(context, play->clip.index1);
    mtwnSharing* sharing = &work->sharing;
    bool moving = rate != 0.0;
    bool same = InStep(sharing, play, current, rate, backwards);
    if (same && sharing->sampleKept && SampleShared(context, work, play, clip, moving, commit))
    {
        return;
    }
    mtwnSampleClip(context, work, play, current, rate, backwards, commit);
    // One near notes its key; the second in step keeps its results for
    // the rest, worked out again from the node times its sample left.
    bool keep = same && clip->nodeCount <= sharing->trackCapacity;
    sharing->sampleValid = true;
    sharing->sampleKept = keep;
    sharing->sampleClip = play->clip;
    sharing->current = current;
    sharing->rate = rate;
    sharing->backwards = backwards;
    if (keep)
    {
        KeepTracks(context, sharing, play, clip, mtwnTimesFor(work, clip), moving);
    }
}
