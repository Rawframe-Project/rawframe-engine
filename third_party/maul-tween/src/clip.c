// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Clips: a block
// of storage per clip, sized by its def and counted against the context's
// clip storage; nodes added under the library's floating-point
// environment, their positions resolved to start times and their ends
// carried up to the root as they arrive.

#include "allocator.h"
#include "clip_internal.h"
#include "context_internal.h"
#include "css_easing.h"
#include "fpenv.h"
#include "valid.h"
#include "work.h"

#include <math.h>
#include <stdckdint.h>
#include <string.h>

static const size_t s_blockAlignment = alignof(max_align_t);

// Where each part of a clip's block starts, and its size.
typedef struct ClipLayout
{
    uint64_t nodes;
    uint64_t keyframes;
    uint64_t points;
    uint64_t lengths;
    uint64_t mixes;
    uint64_t slots;
    uint64_t times;
    uint64_t bytes;
} ClipLayout;

static bool Place(uint64_t* end, uint64_t count, uint64_t size, uint64_t* offsetOut)
{
    uint64_t bytes = 0;
    uint64_t start = 0;
    uint64_t align = s_blockAlignment - 1;
    if (ckd_add(&start, *end, align) || ckd_mul(&bytes, count, size))
    {
        return false;
    }
    start &= ~align;
    *offsetOut = start;
    return !ckd_add(end, start, bytes);
}

static bool PlanClip(const mtwnClipDef* def, ClipLayout* layout)
{
    uint64_t end = 0;
    bool placed =
        Place(&end, (uint64_t)def->nodeCapacity + 1, sizeof(mtwnClipNode), &layout->nodes) &&
        Place(&end, def->keyframeCapacity, sizeof(mtwnKeyframe), &layout->keyframes) &&
        Place(&end, def->pointCapacity, sizeof(mtwnLinearPoint), &layout->points) &&
        Place(&end, def->keyframeCapacity, sizeof(double), &layout->lengths) &&
        Place(&end, def->keyframeCapacity, sizeof(mtwnSegmentMix), &layout->mixes) &&
        Place(&end, def->slotCapacity, sizeof(uint8_t), &layout->slots) &&
        Place(&end, (uint64_t)def->nodeCapacity + 1, sizeof(mtwnNodeTime), &layout->times);
    layout->bytes = end;
    return placed && end <= SIZE_MAX;
}

static bool IsCount(uint32_t count)
{
    return count <= MTWN_MAX_COUNT;
}

// Counts a refusal against a live context as misuse.
static mtwnResult Misuse(mtwnContext* context, mtwnResult result)
{
    mtwnCountMisuse(context);
    return result;
}

// Whether a clip def has its cookie and counts in range.
static mtwnResult CheckClipDef(const mtwnClipDef* def)
{
    if (def->cookie != MTWN_CLIP_DEF_COOKIE)
    {
        return mtwn_errorInvalid;
    }
    bool counts = IsCount(def->nodeCapacity) && IsCount(def->keyframeCapacity) &&
                  IsCount(def->slotCapacity) && IsCount(def->pointCapacity);
    return counts ? mtwn_success : mtwn_errorLimit;
}

static mtwnResult RunGetClipBytes(mtwnContext* context, const mtwnClipDef* def, uint64_t* bytesOut)
{
    if (context == NULL)
    {
        return mtwn_errorInvalid;
    }
    mtwnResult checked = def == NULL || bytesOut == NULL ? mtwn_errorInvalid : CheckClipDef(def);
    if (checked != mtwn_success)
    {
        return Misuse(context, checked);
    }
    // Counts within MTWN_MAX_COUNT never overflow the plan; a size past
    // what this platform can allocate is still the size.
    ClipLayout layout = {0};
    (void)PlanClip(def, &layout);
    *bytesOut = layout.bytes;
    return mtwn_success;
}

static MTWN_NOINLINE mtwnResult mtwnContext_GetClipBytesWork(mtwnContext* context,
                                                             const mtwnClipDef* def,
                                                             uint64_t* bytesOut)
{
    // Cleared first, so a busy or missing context's failure clears it too.
    if (bytesOut != NULL)
    {
        *bytesOut = 0;
    }
    if (!mtwnEnter(context))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunGetClipBytes(context, def, bytesOut);
    mtwnLeave(context);
    return result;
}

mtwnResult mtwnContext_GetClipBytes(mtwnContext* context, const mtwnClipDef* def,
                                    uint64_t* bytesOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnContext_GetClipBytesWork(context, def, bytesOut);
    mtwnFloatEnvLeave(env);
    return result;
}

static mtwnResult RunCreateClip(mtwnContext* context, const mtwnClipDef* def, mtwnClipId* clipOut)
{
    if (context == NULL)
    {
        return mtwn_errorInvalid;
    }
    if (clipOut != NULL)
    {
        *clipOut = (mtwnClipId){0, 0};
    }
    mtwnResult checked = def == NULL || clipOut == NULL ? mtwn_errorInvalid : CheckClipDef(def);
    if (checked != mtwn_success)
    {
        return Misuse(context, checked);
    }
    ClipLayout layout = {0};
    uint64_t used = 0;
    if (!PlanClip(def, &layout) || ckd_add(&used, context->clipStorageUsed, layout.bytes) ||
        used > context->def.clipStorageBytes)
    {
        return mtwn_errorCapacity;
    }
    uint32_t index1 = mtwnPoolAcquire(&context->clips);
    if (index1 == 0)
    {
        return mtwn_errorCapacity;
    }
    unsigned char* block =
        mtwnAllocate(&context->allocator, (size_t)layout.bytes, s_blockAlignment);
    // The root always takes room, so the block is never empty.
    if (block == NULL)
    {
        mtwnPoolRelease(&context->clips, index1);
        return mtwn_errorCapacity;
    }
    memset(block, 0, (size_t)layout.bytes);
    mtwnClipSlot* slot = mtwnClipAt(context, index1);
    slot->def = *def;
    slot->block = block;
    slot->blockBytes = layout.bytes;
    slot->nodes = (mtwnClipNode*)(block + layout.nodes);
    slot->keyframes = (mtwnKeyframe*)(block + layout.keyframes);
    slot->points = (mtwnLinearPoint*)(block + layout.points);
    slot->lengths = (double*)(block + layout.lengths);
    slot->mixes = (mtwnSegmentMix*)(block + layout.mixes);
    slot->slotKinds = block + layout.slots;
    slot->times = (mtwnNodeTime*)(block + layout.times);
    slot->nodes[0].kind = mtwn_nodeTimeline;
    slot->nodes[0].autoDuration = true;
    slot->nodes[0].timing = mtwnDefaultTiming();
    // The root is the player's whole clip: it passes the player's time to
    // its children, held at its ends, and their fills decide.
    slot->nodes[0].timing.fill = mtwn_fillBoth;
    slot->nodeCount = 1;
    context->clipStorageUsed = used;
    *clipOut = (mtwnClipId){index1, slot->header.generation};
    return mtwn_success;
}

static MTWN_NOINLINE mtwnResult mtwnCreateClipWork(mtwnContext* context, const mtwnClipDef* def,
                                                   mtwnClipId* clipOut)
{
    // Cleared first, so a busy or missing context's failure clears it too.
    if (clipOut != NULL)
    {
        *clipOut = (mtwnClipId){0};
    }
    if (!mtwnEnter(context))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunCreateClip(context, def, clipOut);
    mtwnLeave(context);
    return result;
}

mtwnResult mtwnCreateClip(mtwnContext* context, const mtwnClipDef* def, mtwnClipId* clipOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnCreateClipWork(context, def, clipOut);
    mtwnFloatEnvLeave(env);
    return result;
}

static mtwnResult RunDestroyClip(mtwnContext* context, mtwnClipId clip)
{
    if (context == NULL)
    {
        return mtwn_errorInvalid;
    }
    mtwnClipSlot* slot = mtwnFindClip(context, clip);
    if (slot == NULL)
    {
        return Misuse(context, mtwn_errorStale);
    }
    if (slot->users != 0)
    {
        return Misuse(context, mtwn_errorBusy);
    }
    // No path leaves a target lazy on a clip without players today (their
    // destruction writes the bases, a finished player is at rest), so this
    // only keeps a later one from reading a freed clip.
    mtwnSettleClipVelocities(context, clip.index1);
    mtwnRelease(&context->allocator, slot->block, (size_t)slot->blockBytes, s_blockAlignment);
    context->clipStorageUsed -= slot->blockBytes;
    mtwnPoolRelease(&context->clips, clip.index1);
    return mtwn_success;
}

static MTWN_NOINLINE mtwnResult mtwnDestroyClipWork(mtwnContext* context, mtwnClipId clip)
{
    if (!mtwnEnter(context))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunDestroyClip(context, clip);
    mtwnLeave(context);
    return result;
}

mtwnResult mtwnDestroyClip(mtwnContext* context, mtwnClipId clip)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnDestroyClipWork(context, clip);
    mtwnFloatEnvLeave(env);
    return result;
}

bool mtwnClip_IsValid(const mtwnContext* context, mtwnClipId clip)
{
    return context != NULL && mtwnFindClip(context, clip) != NULL;
}

// The start a position gives in a timeline.
static double StartOf(const mtwnClipSlot* clip, uint32_t parent, mtwnPosition position)
{
    const mtwnClipNode* timeline = mtwnClipNodeAt(clip, parent);
    const mtwnClipNode* previous =
        timeline->previous != 0 ? mtwnClipNodeAt(clip, timeline->previous) : NULL;
    switch (position.kind)
    {
    case mtwn_positionAfterEnd:
        return timeline->childrenEnd + position.offset;
    case mtwn_positionWithPrevious:
        return (previous != NULL ? previous->start : 0.0) + position.offset;
    case mtwn_positionAfterPrevious:
        return (previous != NULL ? previous->end : 0.0) + position.offset;
    case mtwn_positionAtLabel:
        return mtwnClipNodeAt(clip, mtwnClipLabelIn(clip, parent, position.label))->start +
               position.offset;
    default:
        return position.offset;
    }
}

// Whether a position names a finite start: after an endless child (whose
// timeline is endless too) there is none.
static bool IsPosition(const mtwnClipSlot* clip, uint32_t parent, mtwnPosition position)
{
    return position.kind <= mtwn_positionAtLabel && isfinite(position.offset) &&
           (position.kind != mtwn_positionAtLabel ||
            mtwnClipLabelIn(clip, parent, position.label) != 0) &&
           isfinite(StartOf(clip, parent, position));
}

// A node's end in its parent, from its start and timing.
static double EndOf(double start, const mtwnTiming* timing)
{
    mtwnTimingResult result;
    mtwnComputeTimingWork(timing, 0.0, false, &result);
    return start + result.endTime;
}

// Carries a child's end up its ancestors: each timeline's children end
// rises to it, and a timeline lasting as long as its children moves its
// own end. Nothing is written on a refusal: mtwn_errorInvalid when the
// duration a timeline would take makes its timing one mtwnValidateTiming
// refuses (played backwards and infinitely long, which leaves it no end to
// start from, or with an active duration or a cycle past a double), and
// mtwn_errorLimit when the clip's end would be finite and past
// MTWN_SEEK_LIMIT.
static mtwnResult Raise(mtwnClipSlot* clip, uint32_t parent, double end, bool write)
{
    while (parent != 0)
    {
        mtwnClipNode* timeline = &clip->nodes[parent - 1];
        // Only a timeline is a parent.
        MTWN_ASSERT(timeline->kind == mtwn_nodeTimeline);
        double childrenEnd = end > timeline->childrenEnd ? end : timeline->childrenEnd;
        // The root's is the clip's end, which a player seeks to: past the
        // limit, it would overflow the start time (MTWN_SEEK_LIMIT).
        if (parent == MTWN_ROOT_NODE && isfinite(childrenEnd) && childrenEnd > MTWN_SEEK_LIMIT)
        {
            return mtwn_errorLimit;
        }
        if (write)
        {
            timeline->childrenEnd = childrenEnd;
        }
        if (!timeline->autoDuration || parent == MTWN_ROOT_NODE)
        {
            return mtwn_success;
        }
        mtwnTiming timing = mtwnClipTimelineTiming(timeline, childrenEnd);
        if (mtwnValidateTimingWork(&timing) != mtwn_success)
        {
            return mtwn_errorInvalid;
        }
        end = EndOf(timeline->start, &timing);
        if (write)
        {
            timeline->end = end;
        }
        parent = timeline->parent;
    }
    return mtwn_success;
}

// Whether there is room for one more node.
static bool HasRoom(const mtwnClipSlot* clip)
{
    return clip->nodeCount <= clip->def.nodeCapacity;
}

// Puts a prepared node into the clip, last among its parent's children.
static uint32_t Attach(mtwnClipSlot* clip, const mtwnClipNode* node)
{
    // HasRoom held: the nodes hold the root and nodeCapacity more.
    MTWN_ASSERT(clip->nodeCount <= clip->def.nodeCapacity);
    uint32_t index1 = ++clip->nodeCount;
    // Its parent, a timeline the caller checked, comes before it.
    MTWN_ASSERT(node->parent >= 1 && node->parent < index1);
    clip->nodes[index1 - 1] = *node;
    mtwnClipNode* parent = &clip->nodes[node->parent - 1];
    if (parent->lastChild != 0)
    {
        clip->nodes[parent->lastChild - 1].nextSibling = index1;
    }
    else
    {
        parent->firstChild = index1;
    }
    parent->lastChild = index1;
    if (node->kind == mtwn_nodeTrack || node->kind == mtwn_nodeTimeline)
    {
        parent->previous = index1;
        if (Raise(clip, node->parent, node->end, true) != mtwn_success)
        {
            // Its caller raised the same end without writing, which
            // succeeded, so writing it cannot fail.
            MTWN_ASSERT(false);
        }
    }
    return index1;
}

// An incremental rotation's turn per iteration, from its keyframes as made
// (on their path): the turn from the first to the last. A number's step is
// taken in double where it moves on, past the floats' range as a float it
// would be infinite.
static mtwnVec4 Turn(mtwnVec4 first, mtwnVec4 last)
{
    // last times first's inverse (its conjugate, first being unit).
    double ax = (double)last.x;
    double ay = (double)last.y;
    double az = (double)last.z;
    double aw = (double)last.w;
    double bx = -(double)first.x;
    double by = -(double)first.y;
    double bz = -(double)first.z;
    double bw = (double)first.w;
    double x = aw * bx + ax * bw + ay * bz - az * by;
    double y = aw * by - ax * bz + ay * bw + az * bx;
    double z = aw * bz + ax * by - ay * bx + az * bw;
    double w = aw * bw - ax * bx - ay * by - az * bz;
    // Of unit rotations, so of length near 1.
    double norm = sqrt(x * x + y * y + z * z + w * w);
    return (mtwnVec4){(float)(x / norm), (float)(y / norm), (float)(z / norm), (float)(w / norm)};
}

// The points an easing taken in has for the clip to copy.
static uint32_t PointsOf(const mtwnEasing* easing)
{
    return easing->kind == mtwn_easingLinearPoints ? easing->count : 0;
}

// Copies the linear() points of a node's timing easing and of its
// keyframes' easings into the clip after those it holds, pointing the
// easings at the copies, so the host's may change once added; the count
// it then holds in countOut, for the caller to keep once the node is.
// mtwn_errorInvalid when the host changed points from what their maker
// left, mtwn_errorCapacity when they do not fit; the easings valid.
static mtwnResult CopyPoints(mtwnClipSlot* clip, mtwnEasing* timing, mtwnKeyframe* keyframes,
                             uint32_t keyframeCount, uint32_t* countOut)
{
    uint64_t needed = PointsOf(timing);
    bool canonical = mtwnCanonicalLinearPoints(timing->points, PointsOf(timing));
    for (uint32_t i = 0; i < keyframeCount; ++i)
    {
        const mtwnEasing* easing = &keyframes[i].easing;
        needed += PointsOf(easing);
        canonical = canonical && mtwnCanonicalLinearPoints(easing->points, PointsOf(easing));
    }
    if (!canonical)
    {
        return mtwn_errorInvalid;
    }
    if (needed > clip->def.pointCapacity - clip->pointCount)
    {
        return mtwn_errorCapacity;
    }
    uint32_t at = clip->pointCount;
    if (PointsOf(timing) > 0)
    {
        mtwnCopyLinearPoints(timing, &clip->points[at]);
        at += timing->count;
    }
    for (uint32_t i = 0; i < keyframeCount; ++i)
    {
        mtwnEasing* easing = &keyframes[i].easing;
        if (PointsOf(easing) > 0)
        {
            mtwnCopyLinearPoints(easing, &clip->points[at]);
            at += easing->count;
        }
    }
    *countOut = at;
    return mtwn_success;
}

// What the def asks of the clip, before anything in the clip changes.
static mtwnResult CheckTrack(const mtwnClipSlot* clip, const mtwnClipTrackDef* def)
{
    bool slot = def->slot < clip->def.slotCapacity && def->track.kind <= mtwn_valueScale;
    if (slot)
    {
        uint8_t kind = clip->slotKinds[def->slot];
        slot = kind == 0 || kind == def->track.kind + 1;
    }
    // Endless iterations of no duration all pass at once: the iteration
    // past them is infinite, and no value moves on by it.
    bool endlessAtOnce = isinf(def->timing.iterations) && def->timing.duration == 0.0;
    bool incremental =
        !def->incremental || (def->track.kind != mtwn_valueColor &&
                              def->track.kind != mtwn_valueDiscrete && !endlessAtOnce);
    if (!mtwnClipIsTimeline(clip, def->parent) || !IsPosition(clip, def->parent, def->position) ||
        !slot || !incremental || def->keyframes == NULL ||
        mtwnValidateTimingWork(&def->timing) != mtwn_success)
    {
        return mtwn_errorInvalid;
    }
    if (!HasRoom(clip) || def->keyframeCount > clip->def.keyframeCapacity - clip->keyframeCount)
    {
        return mtwn_errorCapacity;
    }
    return mtwn_success;
}

static MTWN_NOINLINE mtwnResult AddTrackWorker(mtwnClipSlot* clip, const mtwnClipTrackDef* def,
                                               mtwnNode* nodeOut)
{
    mtwnResult checked = CheckTrack(clip, def);
    if (checked != mtwn_success)
    {
        return checked;
    }
    uint32_t base = clip->keyframeCount;
    mtwnKeyframe* keyframes = &clip->keyframes[base];
    memcpy(keyframes, def->keyframes, (size_t)def->keyframeCount * sizeof(mtwnKeyframe));
    mtwnClipNode node = {0};
    mtwnResult made = mtwnMakeTrackWork(&def->track, keyframes, def->keyframeCount,
                                        def->constantSpeed ? &clip->lengths[base] : NULL,
                                        &clip->mixes[base], &node.track);
    if (made != mtwn_success)
    {
        return made;
    }
    node.timing = def->timing;
    uint32_t points = 0;
    mtwnResult copied =
        CopyPoints(clip, &node.timing.easing, keyframes, def->keyframeCount, &points);
    if (copied != mtwn_success)
    {
        return copied;
    }
    node.start = StartOf(clip, def->parent, def->position);
    node.end = EndOf(node.start, &def->timing);
    node.parent = def->parent;
    node.slot = def->slot;
    node.kind = mtwn_nodeTrack;
    node.incremental = def->incremental;
    if (def->incremental && def->track.kind == mtwn_valueRotation)
    {
        node.step = Turn(keyframes[0].value, keyframes[def->keyframeCount - 1].value);
    }
    mtwnResult raised = Raise(clip, def->parent, node.end, false);
    if (raised != mtwn_success)
    {
        return raised;
    }
    *nodeOut = Attach(clip, &node);
    clip->keyframeCount += def->keyframeCount;
    clip->pointCount = points;
    clip->slotKinds[def->slot] = (uint8_t)(def->track.kind + 1);
    clip->slotCount = def->slot + 1 > clip->slotCount ? def->slot + 1 : clip->slotCount;
    return mtwn_success;
}

static MTWN_NOINLINE mtwnResult AddTimelineWorker(mtwnClipSlot* clip,
                                                  const mtwnClipTimelineDef* def, mtwnNode* nodeOut)
{
    mtwnClipNode node = {0};
    node.timing = def->timing;
    node.autoDuration = def->autoDuration;
    mtwnTiming resolved = mtwnClipTimelineTiming(&node, 0.0);
    if (!mtwnClipIsTimeline(clip, def->parent) || !IsPosition(clip, def->parent, def->position) ||
        mtwnValidateTimingWork(&resolved) != mtwn_success ||
        !mtwnIsEasingMonotonicWork(&def->timing.easing))
    {
        return mtwn_errorInvalid;
    }
    node.depth = (uint8_t)(mtwnClipNodeAt(clip, def->parent)->depth + 1);
    if (node.depth > MTWN_MAX_CLIP_DEPTH)
    {
        return mtwn_errorLimit;
    }
    if (!HasRoom(clip))
    {
        return mtwn_errorCapacity;
    }
    uint32_t points = 0;
    mtwnResult copied = CopyPoints(clip, &node.timing.easing, NULL, 0, &points);
    if (copied != mtwn_success)
    {
        return copied;
    }
    node.start = StartOf(clip, def->parent, def->position);
    node.end = EndOf(node.start, &resolved);
    node.parent = def->parent;
    node.kind = mtwn_nodeTimeline;
    mtwnResult raised = Raise(clip, def->parent, node.end, false);
    if (raised != mtwn_success)
    {
        return raised;
    }
    *nodeOut = Attach(clip, &node);
    clip->pointCount = points;
    return mtwn_success;
}

static MTWN_NOINLINE mtwnResult AddInstantWorker(mtwnClipSlot* clip, mtwnNodeKind kind,
                                                 mtwnNode parent, mtwnPosition position,
                                                 uint32_t key, mtwnNode* nodeOut)
{
    if (!mtwnClipIsTimeline(clip, parent) || !IsPosition(clip, parent, position) ||
        (kind == mtwn_nodeLabel && mtwnClipLabelIn(clip, parent, key) != 0))
    {
        return mtwn_errorInvalid;
    }
    if (!HasRoom(clip))
    {
        return mtwn_errorCapacity;
    }
    mtwnClipNode node = {0};
    node.start = StartOf(clip, parent, position);
    node.end = node.start;
    node.parent = parent;
    node.key = key;
    node.kind = kind;
    *nodeOut = Attach(clip, &node);
    return mtwn_success;
}

// The checks every add shares: the clip alive and not sealed, the def
// given with its cookie. Returns the clip, or NULL with the refusal,
// counted, in resultOut.
static mtwnClipSlot* Adding(mtwnContext* context, mtwnClipId clip, bool given, bool cookie,
                            mtwnNode* nodeOut, mtwnResult* resultOut)
{
    if (nodeOut != NULL)
    {
        *nodeOut = 0;
    }
    mtwnClipSlot* slot = mtwnFindClip(context, clip);
    mtwnResult result = mtwn_success;
    if (slot == NULL)
    {
        result = mtwn_errorStale;
    }
    else if (!given || nodeOut == NULL || !cookie)
    {
        result = mtwn_errorInvalid;
    }
    else if (slot->sealed)
    {
        result = mtwn_errorState;
    }
    *resultOut = result == mtwn_success ? result : Misuse(context, result);
    return result == mtwn_success ? slot : NULL;
}

// A worker's result, an invalid argument or one past a named bound counted
// as misuse; a full clip is not.
static mtwnResult Counted(mtwnContext* context, mtwnResult result)
{
    return result == mtwn_errorInvalid || result == mtwn_errorLimit ? Misuse(context, result)
                                                                    : result;
}

static mtwnResult RunClipAddTrack(mtwnContext* context, mtwnClipId clip,
                                  const mtwnClipTrackDef* def, mtwnNode* nodeOut)
{
    if (context == NULL)
    {
        return mtwn_errorInvalid;
    }
    mtwnResult result = mtwn_success;
    bool given = def != NULL;
    mtwnClipSlot* slot = Adding(
        context, clip, given, given && def->cookie == MTWN_CLIP_TRACK_DEF_COOKIE, nodeOut, &result);
    if (slot == NULL)
    {
        return result;
    }
    result = AddTrackWorker(slot, def, nodeOut);
    return Counted(context, result);
}

static MTWN_NOINLINE mtwnResult mtwnClip_AddTrackWork(mtwnContext* context, mtwnClipId clip,
                                                      const mtwnClipTrackDef* def,
                                                      mtwnNode* nodeOut)
{
    // Cleared first, so a busy or missing context's failure clears it too.
    if (nodeOut != NULL)
    {
        *nodeOut = 0;
    }
    if (!mtwnEnter(context))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunClipAddTrack(context, clip, def, nodeOut);
    mtwnLeave(context);
    return result;
}

mtwnResult mtwnClip_AddTrack(mtwnContext* context, mtwnClipId clip, const mtwnClipTrackDef* def,
                             mtwnNode* nodeOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnClip_AddTrackWork(context, clip, def, nodeOut);
    mtwnFloatEnvLeave(env);
    return result;
}

static mtwnResult RunClipAddTimeline(mtwnContext* context, mtwnClipId clip,
                                     const mtwnClipTimelineDef* def, mtwnNode* nodeOut)
{
    if (context == NULL)
    {
        return mtwn_errorInvalid;
    }
    mtwnResult result = mtwn_success;
    bool given = def != NULL;
    mtwnClipSlot* slot =
        Adding(context, clip, given, given && def->cookie == MTWN_CLIP_TIMELINE_DEF_COOKIE, nodeOut,
               &result);
    if (slot == NULL)
    {
        return result;
    }
    result = AddTimelineWorker(slot, def, nodeOut);
    return Counted(context, result);
}

static MTWN_NOINLINE mtwnResult mtwnClip_AddTimelineWork(mtwnContext* context, mtwnClipId clip,
                                                         const mtwnClipTimelineDef* def,
                                                         mtwnNode* nodeOut)
{
    // Cleared first, so a busy or missing context's failure clears it too.
    if (nodeOut != NULL)
    {
        *nodeOut = 0;
    }
    if (!mtwnEnter(context))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunClipAddTimeline(context, clip, def, nodeOut);
    mtwnLeave(context);
    return result;
}

mtwnResult mtwnClip_AddTimeline(mtwnContext* context, mtwnClipId clip,
                                const mtwnClipTimelineDef* def, mtwnNode* nodeOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnClip_AddTimelineWork(context, clip, def, nodeOut);
    mtwnFloatEnvLeave(env);
    return result;
}

static mtwnResult AddInstant(mtwnContext* context, mtwnClipId clip, mtwnNodeKind kind,
                             mtwnNode parent, mtwnPosition position, uint32_t key,
                             mtwnNode* nodeOut)
{
    if (context == NULL)
    {
        return mtwn_errorInvalid;
    }
    // No def to check: these take their arguments directly.
    mtwnResult result = mtwn_success;
    mtwnClipSlot* slot = Adding(context, clip, true, true, nodeOut, &result);
    if (slot == NULL)
    {
        return result;
    }
    result = AddInstantWorker(slot, kind, parent, position, key, nodeOut);
    return Counted(context, result);
}

static mtwnResult RunClipAddLabel(mtwnContext* context, mtwnClipId clip, mtwnNode parent,
                                  mtwnPosition position, uint32_t key, mtwnNode* nodeOut)
{
    return AddInstant(context, clip, mtwn_nodeLabel, parent, position, key, nodeOut);
}

static MTWN_NOINLINE mtwnResult mtwnClip_AddLabelWork(mtwnContext* context, mtwnClipId clip,
                                                      mtwnNode parent, mtwnPosition position,
                                                      uint32_t key, mtwnNode* nodeOut)
{
    // Cleared first, so a busy or missing context's failure clears it too.
    if (nodeOut != NULL)
    {
        *nodeOut = 0;
    }
    if (!mtwnEnter(context))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunClipAddLabel(context, clip, parent, position, key, nodeOut);
    mtwnLeave(context);
    return result;
}

mtwnResult mtwnClip_AddLabel(mtwnContext* context, mtwnClipId clip, mtwnNode parent,
                             mtwnPosition position, uint32_t key, mtwnNode* nodeOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnClip_AddLabelWork(context, clip, parent, position, key, nodeOut);
    mtwnFloatEnvLeave(env);
    return result;
}

static mtwnResult RunClipAddMark(mtwnContext* context, mtwnClipId clip, mtwnNode parent,
                                 mtwnPosition position, uint32_t key, mtwnNode* nodeOut)
{
    return AddInstant(context, clip, mtwn_nodeMark, parent, position, key, nodeOut);
}

static MTWN_NOINLINE mtwnResult mtwnClip_AddMarkWork(mtwnContext* context, mtwnClipId clip,
                                                     mtwnNode parent, mtwnPosition position,
                                                     uint32_t key, mtwnNode* nodeOut)
{
    // Cleared first, so a busy or missing context's failure clears it too.
    if (nodeOut != NULL)
    {
        *nodeOut = 0;
    }
    if (!mtwnEnter(context))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunClipAddMark(context, clip, parent, position, key, nodeOut);
    mtwnLeave(context);
    return result;
}

mtwnResult mtwnClip_AddMark(mtwnContext* context, mtwnClipId clip, mtwnNode parent,
                            mtwnPosition position, uint32_t key, mtwnNode* nodeOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnClip_AddMarkWork(context, clip, parent, position, key, nodeOut);
    mtwnFloatEnvLeave(env);
    return result;
}

// Whether two timings give the same results for the same inputs: every
// field the same, doubles as bits, linear() points by their values. A host easing is never the
// same, as nothing says calling it fewer times is allowed.
static bool SameTiming(const mtwnTiming* a, const mtwnTiming* b)
{
    const mtwnEasing* e = &a->easing;
    const mtwnEasing* f = &b->easing;
    bool easing = e->kind != mtwn_easingHost && e->kind == f->kind && e->mode == f->mode &&
                  e->position == f->position && e->count == f->count;
    // A linear() easing's points are each node's own copy.
    for (uint32_t i = 0; easing && i < PointsOf(e); ++i)
    {
        easing = mtwnSameBits(e->points[i].input, f->points[i].input) &&
                 mtwnSameBits(e->points[i].output, f->points[i].output);
    }
    for (int k = 0; k < 5; ++k)
    {
        easing = easing && mtwnSameBits(e->parameters[k], f->parameters[k]);
    }
    return easing && mtwnSameBits(a->delay, b->delay) && mtwnSameBits(a->endDelay, b->endDelay) &&
           mtwnSameBits(a->iterationStart, b->iterationStart) &&
           mtwnSameBits(a->iterations, b->iterations) && mtwnSameBits(a->duration, b->duration) &&
           mtwnSameBits(a->repeatDelay, b->repeatDelay) &&
           mtwnSameBits(a->playbackRate, b->playbackRate) && a->fill == b->fill &&
           a->direction == b->direction && a->alternateEasing == b->alternateEasing;
}

static MTWN_NOINLINE void SealWorker(mtwnClipSlot* slot)
{
    // Timelines sized by their children take the size for good; every node
    // keeps its active duration and its iterations at its active start and
    // end for its events.
    for (uint32_t i = 0; i < slot->nodeCount; ++i)
    {
        mtwnClipNode* node = &slot->nodes[i];
        if (node->kind == mtwn_nodeTimeline && node->autoDuration)
        {
            node->timing.duration = node->childrenEnd;
        }
        if (node->kind == mtwn_nodeTimeline || node->kind == mtwn_nodeTrack)
        {
            mtwnTimingResult shape;
            mtwnComputeTimingWork(&node->timing, 0.0, false, &shape);
            node->activeDuration = shape.activeDuration;
            // Its iterations there, held either side: the first at its
            // start, the last at its end. Held, both are resolved (the
            // worker's postconditions), which the sweep's records take as
            // known.
            mtwnTiming held = node->timing;
            held.fill = mtwn_fillBoth;
            mtwnComputeTimingWork(&held, held.delay, false, &shape);
            node->startIteration = shape.iteration;
            mtwnComputeTimingWork(&held, held.delay + node->activeDuration, false, &shape);
            node->endIteration = shape.iteration;
        }
        const mtwnClipNode* before = i > 0 ? &slot->nodes[i - 1] : NULL;
        node->sharesTiming = node->kind == mtwn_nodeTrack && before != NULL &&
                             before->kind == mtwn_nodeTrack && before->parent == node->parent &&
                             mtwnSameBits(before->start, node->start) &&
                             SameTiming(&before->timing, &node->timing);
    }
    slot->sealed = true;
}

static mtwnResult RunClipSeal(mtwnContext* context, mtwnClipId clip)
{
    if (context == NULL)
    {
        return mtwn_errorInvalid;
    }
    mtwnClipSlot* slot = mtwnFindClip(context, clip);
    if (slot == NULL)
    {
        return Misuse(context, mtwn_errorStale);
    }
    if (slot->sealed)
    {
        return Misuse(context, mtwn_errorState);
    }
    SealWorker(slot);
    return mtwn_success;
}

static MTWN_NOINLINE mtwnResult mtwnClip_SealWork(mtwnContext* context, mtwnClipId clip)
{
    if (!mtwnEnter(context))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunClipSeal(context, clip);
    mtwnLeave(context);
    return result;
}

mtwnResult mtwnClip_Seal(mtwnContext* context, mtwnClipId clip)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnClip_SealWork(context, clip);
    mtwnFloatEnvLeave(env);
    return result;
}
