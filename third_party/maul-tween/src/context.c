// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context: the def checked, the context, its pools and its list of
// changed targets allocated in one block through the host's allocator,
// and freed in one call with its clips' blocks.

#include "allocator.h"
#include "context_internal.h"
#include "fpenv.h"
#include "invariant.h"

#include <stdckdint.h>
#include <string.h>

static_assert(sizeof(mtwnTargetSlot) == 64, "a target slot is 64 bytes");

// The block's alignment: what the C library's malloc serves, and what
// every part of the block starts on.
static const size_t s_blockAlignment = alignof(max_align_t);

static bool IsCount(uint32_t count)
{
    return count <= MTWN_MAX_COUNT;
}

static mtwnResult CheckDef(const mtwnContextDef* def)
{
    if (def->cookie != MTWN_CONTEXT_DEF_COOKIE)
    {
        return mtwn_errorInvalid;
    }
    if (def->version != MTWN_ABI_VERSION)
    {
        return mtwn_errorVersion;
    }
    bool oneFunction = (def->allocator.alloc == NULL) != (def->allocator.free == NULL) ||
                       (def->enqueueTask == NULL) != (def->finishTask == NULL);
    if (oneFunction)
    {
        return mtwn_errorInvalid;
    }
    bool within = IsCount(def->targetCapacity) && IsCount(def->playerCapacity) &&
                  IsCount(def->clipCapacity) && IsCount(def->eventCapacity) &&
                  IsCount(def->bindingCapacity) && IsCount(def->sharedNodeCapacity) &&
                  IsCount(def->sharedRecordCapacity) && IsCount(def->pointsPerTransition) &&
                  def->clipStorageBytes <= MTWN_MAX_CLIP_STORAGE;
    return within ? mtwn_success : mtwn_errorLimit;
}

// Where each part of the block starts, and its size.
typedef struct Layout
{
    uint64_t targets;
    uint64_t players;
    uint64_t transitionPoints;
    uint64_t clips;
    uint64_t bindings;
    uint64_t events;
    uint64_t eventAlong;
    uint64_t levels;
    uint64_t serial;
    // What each work shares among players in step: the serial work's
    // first, then every group's.
    uint64_t sharedTracks;
    uint64_t sharedRecords;
    uint64_t changed;
    // With a task system: the groups' works and scratch, each kind for
    // every group in turn, and the clips' players' staging.
    uint64_t works;
    uint64_t groupLevels;
    uint64_t groupTimes;
    uint64_t groupRecords;
    uint64_t groupAlong;
    uint64_t groupTracks;
    uint64_t groupChanges;
    uint64_t staged;
    uint64_t bytes;
} Layout;

// Adds count elements of size bytes at the end, aligned; false on
// overflow.
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

// A group's staging: its records, up to the queue's capacity, and its
// share of twice the bindings' track results.
static uint32_t GroupRecords(const mtwnContextDef* def)
{
    return def->eventCapacity < MTWN_GROUP_RECORDS ? def->eventCapacity : MTWN_GROUP_RECORDS;
}

static uint32_t GroupTracks(const mtwnContextDef* def)
{
    return (uint32_t)(((uint64_t)def->bindingCapacity * 2u + MTWN_GROUPS - 1u) / MTWN_GROUPS);
}

// The works a context holds: the serial one, and with a task system each
// group's.
static uint32_t Works(const mtwnContextDef* def)
{
    return def->enqueueTask != NULL ? 1u + MTWN_GROUPS : 1u;
}

// Points a work's sharing at the k-th share of the block's.
static void SetSharing(mtwnWork* work, const mtwnContextDef* def, unsigned char* block,
                       const Layout* layout, uint32_t k)
{
    uint32_t nodes = def->sharedNodeCapacity;
    uint32_t records = def->sharedRecordCapacity;
    work->sharing = (mtwnSharing){0};
    work->sharing.tracks = (mtwnSharedTrack*)(block + layout->sharedTracks) + (size_t)k * nodes;
    work->sharing.records =
        (mtwnSharedRecord*)(block + layout->sharedRecords) + (size_t)k * records;
    work->sharing.trackCapacity = nodes;
    work->sharing.recordCapacity = records;
}

static bool PlanGroups(const mtwnContextDef* def, uint64_t* end, Layout* layout)
{
    if (def->enqueueTask == NULL)
    {
        return true;
    }
    uint64_t levels = (uint64_t)MTWN_GROUPS * (MTWN_MAX_CLIP_DEPTH + 2u);
    return Place(end, MTWN_GROUPS, sizeof(mtwnWork), &layout->works) &&
           Place(end, levels, sizeof(mtwnLevel), &layout->groupLevels) &&
           Place(end, (uint64_t)MTWN_GROUPS * MTWN_GROUP_NODES, sizeof(mtwnNodeTime),
                 &layout->groupTimes) &&
           Place(end, (uint64_t)MTWN_GROUPS * GroupRecords(def), sizeof(mtwnEvent),
                 &layout->groupRecords) &&
           Place(end, (uint64_t)MTWN_GROUPS * GroupRecords(def), sizeof(double),
                 &layout->groupAlong) &&
           Place(end, (uint64_t)MTWN_GROUPS * GroupTracks(def), sizeof(mtwnTrackOut),
                 &layout->groupTracks) &&
           Place(end, (uint64_t)MTWN_GROUPS * GroupTracks(def), sizeof(uint32_t),
                 &layout->groupChanges) &&
           Place(end, def->playerCapacity, sizeof(mtwnStaged), &layout->staged);
}

static bool PlanBlock(const mtwnContextDef* def, Layout* layout)
{
    uint64_t end = sizeof(mtwnContext);
    bool placed = Place(&end, def->targetCapacity, sizeof(mtwnTargetSlot), &layout->targets) &&
                  Place(&end, def->playerCapacity, sizeof(mtwnPlayerSlot), &layout->players) &&
                  Place(&end, (uint64_t)def->playerCapacity * def->pointsPerTransition,
                        sizeof(mtwnLinearPoint), &layout->transitionPoints) &&
                  Place(&end, def->clipCapacity, sizeof(mtwnClipSlot), &layout->clips) &&
                  Place(&end, def->bindingCapacity, sizeof(mtwnBound), &layout->bindings) &&
                  Place(&end, def->eventCapacity, sizeof(mtwnEvent), &layout->events) &&
                  Place(&end, def->eventCapacity, sizeof(double), &layout->eventAlong) &&
                  Place(&end, MTWN_MAX_CLIP_DEPTH + 2u, sizeof(mtwnLevel), &layout->levels) &&
                  Place(&end, 1, sizeof(mtwnWork), &layout->serial) &&
                  Place(&end, (uint64_t)Works(def) * def->sharedNodeCapacity,
                        sizeof(mtwnSharedTrack), &layout->sharedTracks) &&
                  Place(&end, (uint64_t)Works(def) * def->sharedRecordCapacity,
                        sizeof(mtwnSharedRecord), &layout->sharedRecords) &&
                  Place(&end, def->targetCapacity, sizeof(uint32_t), &layout->changed) &&
                  PlanGroups(def, &end, layout);
    layout->bytes = end;
    return placed && end <= SIZE_MAX;
}

// Points the groups' works at their scratch, staging in each.
static void SetGroups(mtwnContext* context, unsigned char* block, const Layout* layout)
{
    const mtwnContextDef* def = &context->def;
    context->serial = (mtwnWork*)(block + layout->serial);
    *context->serial = (mtwnWork){0};
    context->serial->levels = context->levels;
    SetSharing(context->serial, def, block, layout, 0);
    context->works = NULL;
    context->staged = NULL;
    if (def->enqueueTask == NULL)
    {
        return;
    }
    context->works = (mtwnWork*)(block + layout->works);
    context->staged = (mtwnStaged*)(block + layout->staged);
    uint32_t records = GroupRecords(def);
    uint32_t tracks = GroupTracks(def);
    for (uint32_t g = 0; g < MTWN_GROUPS; ++g)
    {
        mtwnWork* work = &context->works[g];
        *work = (mtwnWork){0};
        work->levels = (mtwnLevel*)(block + layout->groupLevels) + g * (MTWN_MAX_CLIP_DEPTH + 2u);
        work->times = (mtwnNodeTime*)(block + layout->groupTimes) + g * MTWN_GROUP_NODES;
        work->records = (mtwnEvent*)(block + layout->groupRecords) + (size_t)g * records;
        work->recordAlong = (double*)(block + layout->groupAlong) + (size_t)g * records;
        work->tracks = (mtwnTrackOut*)(block + layout->groupTracks) + (size_t)g * tracks;
        work->changes = (uint32_t*)(block + layout->groupChanges) + (size_t)g * tracks;
        work->changeCapacity = tracks;
        work->recordCapacity = records;
        work->trackCapacity = tracks;
        SetSharing(work, def, block, layout, 1u + g);
    }
    // Each group's levels hold the deepest clip's, apart from the next
    // group's and from the node times after them.
    MTWN_ASSERT((unsigned char*)(context->works[MTWN_GROUPS - 1u].levels + MTWN_MAX_CLIP_DEPTH +
                                 2u) <= block + layout->groupTimes);
}

static mtwnPool MakePool(unsigned char* slots, uint32_t stride, uint32_t capacity)
{
    return (mtwnPool){slots, stride, capacity, 0, 0};
}

static MTWN_NOINLINE mtwnResult mtwnCreateContextWork(const mtwnContextDef* def,
                                                      mtwnContext** contextOut)
{
    if (contextOut == NULL)
    {
        return mtwn_errorInvalid;
    }
    *contextOut = NULL;
    if (def == NULL)
    {
        return mtwn_errorInvalid;
    }
    mtwnResult checked = CheckDef(def);
    if (checked != mtwn_success)
    {
        return checked;
    }
    Layout layout = {0};
    if (!PlanBlock(def, &layout))
    {
        return mtwn_errorCapacity;
    }
    unsigned char* block = mtwnAllocate(&def->allocator, (size_t)layout.bytes, s_blockAlignment);
    if (block == NULL)
    {
        return mtwn_errorCapacity;
    }
    memset(block, 0, (size_t)layout.bytes);
    mtwnContext* context = (mtwnContext*)block;
    context->allocator = def->allocator;
    context->def = *def;
    context->blockBytes = layout.bytes;
    context->targets =
        MakePool(block + layout.targets, sizeof(mtwnTargetSlot), def->targetCapacity);
    context->players =
        MakePool(block + layout.players, sizeof(mtwnPlayerSlot), def->playerCapacity);
    context->transitionPoints = (mtwnLinearPoint*)(block + layout.transitionPoints);
    context->clips = MakePool(block + layout.clips, sizeof(mtwnClipSlot), def->clipCapacity);
    context->bindings = mtwnMakeRuns((mtwnBound*)(block + layout.bindings), def->bindingCapacity);
    context->events = (mtwnEvent*)(block + layout.events);
    context->eventAlong = (double*)(block + layout.eventAlong);
    context->levels = (mtwnLevel*)(block + layout.levels);
    context->changed = (uint32_t*)(block + layout.changed);
    SetGroups(context, block, &layout);
    *contextOut = context;
    return mtwn_success;
}

mtwnResult mtwnCreateContext(const mtwnContextDef* def, mtwnContext** contextOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnCreateContextWork(def, contextOut);
    mtwnFloatEnvLeave(env);
    return result;
}

static bool SameAllocator(const mtwnAllocator* a, const mtwnAllocator* b)
{
    return a->alloc == b->alloc && a->free == b->free && a->context == b->context;
}

// Whether a def holds at least what a context does.
static bool Holds(const mtwnContextDef* def, const mtwnContext* context)
{
    const mtwnContextDef* now = &context->def;
    return def->targetCapacity >= now->targetCapacity &&
           def->playerCapacity >= now->playerCapacity &&
           def->pointsPerTransition >= now->pointsPerTransition &&
           def->clipCapacity >= now->clipCapacity && def->eventCapacity >= now->eventCapacity &&
           def->bindingCapacity >= now->bindingCapacity &&
           def->clipStorageBytes >= now->clipStorageBytes &&
           SameAllocator(&def->allocator, &context->allocator);
}

// A pool's slots in a new place, ids and free list kept.
static void MovePool(mtwnPool* to, const mtwnPool* from)
{
    memcpy(to->slots, from->slots, (size_t)from->high * from->stride);
    to->high = from->high;
    to->free = from->free;
}

// Each transition's linear() points into the new block, at its player's
// place in the new stride, its easing pointed at them. A slot freed since
// keeps the old block's points it had, so moving them too is harmless.
static void MoveTransitionPoints(mtwnContext* to)
{
    for (uint32_t i = 1; i <= to->players.high; ++i)
    {
        mtwnPlayerSlot* slot = mtwnPlayerAt(to, i);
        if (slot->type == MTWN_PLAYER_TRANSITION && slot->transitionKind == mtwn_transitionEasing &&
            slot->easing.kind == mtwn_easingLinearPoints)
        {
            mtwnLinearPoint* points = mtwnTransitionPointsAt(to, i);
            memcpy(points, slot->easing.points, (size_t)slot->easing.count * sizeof *points);
            slot->easing.points = points;
        }
    }
}

// Moves what a context holds into a new block laid out for a larger def.
static void MoveInto(mtwnContext* to, const mtwnContext* from, unsigned char* block,
                     const Layout* layout)
{
    const mtwnContextDef* def = &to->def;
    to->targets = MakePool(block + layout->targets, sizeof(mtwnTargetSlot), def->targetCapacity);
    to->players = MakePool(block + layout->players, sizeof(mtwnPlayerSlot), def->playerCapacity);
    to->clips = MakePool(block + layout->clips, sizeof(mtwnClipSlot), def->clipCapacity);
    MovePool(&to->targets, &from->targets);
    MovePool(&to->players, &from->players);
    MovePool(&to->clips, &from->clips);
    to->transitionPoints = (mtwnLinearPoint*)(block + layout->transitionPoints);
    MoveTransitionPoints(to);
    // The bindings keep their runs; the new tail is one more free run.
    uint32_t had = from->bindings.capacity;
    to->bindings = (mtwnRuns){(mtwnBound*)(block + layout->bindings), def->bindingCapacity,
                              from->bindings.free};
    memcpy(to->bindings.entries, from->bindings.entries, (size_t)had * sizeof(mtwnBound));
    if (def->bindingCapacity > had)
    {
        mtwnGiveRun(&to->bindings, had, def->bindingCapacity - had);
    }
    // The queue's records in order, from the start of the new ring (their
    // places along their sweeps ordered them when made, and are done).
    to->events = (mtwnEvent*)(block + layout->events);
    to->eventAlong = (double*)(block + layout->eventAlong);
    for (uint32_t i = 0; i < from->eventCount; ++i)
    {
        uint32_t at = (from->eventHead + i) % from->def.eventCapacity;
        to->events[i] = from->events[at];
    }
    to->eventHead = 0;
    to->levels = (mtwnLevel*)(block + layout->levels);
    to->changed = (uint32_t*)(block + layout->changed);
    memcpy(to->changed, from->changed, (size_t)from->changedCount * sizeof(uint32_t));
    SetGroups(to, block, layout);
}

static mtwnResult RunContextGrow(mtwnContext** contextInOut, const mtwnContextDef* def)
{
    if (contextInOut == NULL || *contextInOut == NULL)
    {
        return mtwn_errorInvalid;
    }
    mtwnContext* from = *contextInOut;
    mtwnResult checked = def == NULL ? mtwn_errorInvalid : CheckDef(def);
    if (checked == mtwn_success && !Holds(def, from))
    {
        checked = mtwn_errorInvalid;
    }
    if (checked != mtwn_success)
    {
        mtwnCountMisuse(from);
        return checked;
    }
    Layout layout = {0};
    if (!PlanBlock(def, &layout))
    {
        return mtwn_errorCapacity;
    }
    unsigned char* block = mtwnAllocate(&from->allocator, (size_t)layout.bytes, s_blockAlignment);
    if (block == NULL)
    {
        return mtwn_errorCapacity;
    }
    memset(block, 0, (size_t)layout.bytes);
    mtwnContext* to = (mtwnContext*)block;
    *to = *from;
    to->def = *def;
    to->blockBytes = layout.bytes;
    MoveInto(to, from, block, &layout);
    mtwnAllocator allocator = from->allocator;
    mtwnRelease(&allocator, from, (size_t)from->blockBytes, s_blockAlignment);
    *contextInOut = to;
    return mtwn_success;
}

static MTWN_NOINLINE mtwnResult mtwnContext_GrowWork(mtwnContext** contextInOut,
                                                     const mtwnContextDef* def)
{
    // The context moves: what it was enters, what it is leaves.
    if (!mtwnEnter(contextInOut == NULL ? NULL : *contextInOut))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunContextGrow(contextInOut, def);
    mtwnLeave(contextInOut == NULL ? NULL : *contextInOut);
    return result;
}

mtwnResult mtwnContext_Grow(mtwnContext** contextInOut, const mtwnContextDef* def)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnContext_GrowWork(contextInOut, def);
    mtwnFloatEnvLeave(env);
    return result;
}

void mtwnDestroyContext(mtwnContext* context)
{
    if (context == NULL || !mtwnEnter(context))
    {
        return;
    }
    // Busy to the end: the allocator's calls back in are refused until the
    // context's own block goes.
    // The clips' blocks first, then the context's; the allocator is read
    // out before the block that holds it is freed.
    mtwnAllocator allocator = context->allocator;
    for (uint32_t index1 = 1; index1 <= context->clips.high; ++index1)
    {
        mtwnClipSlot* clip = mtwnClipAt(context, index1);
        if ((clip->header.flags & MTWN_SLOT_ALIVE) != 0)
        {
            mtwnRelease(&allocator, clip->block, (size_t)clip->blockBytes, s_blockAlignment);
        }
    }
    mtwnRelease(&allocator, context, (size_t)context->blockBytes, s_blockAlignment);
}

uint64_t mtwnContext_GetMisuse(const mtwnContext* context)
{
    return atomic_load_explicit(&context->misuse, memory_order_relaxed);
}

uint64_t mtwnContext_GetSerialAdvances(const mtwnContext* context)
{
    return context->serialAdvances;
}

uint64_t mtwnContext_GetMemoryBytes(const mtwnContext* context)
{
    return context->blockBytes;
}

static mtwnResult RunContextEnableReducedMotion(mtwnContext* context, bool enabled)
{
    if (context == NULL)
    {
        return mtwn_errorInvalid;
    }
    context->reducedMotion = enabled;
    return mtwn_success;
}

static MTWN_NOINLINE mtwnResult mtwnContext_EnableReducedMotionWork(mtwnContext* context,
                                                                    bool enabled)
{
    if (!mtwnEnter(context))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunContextEnableReducedMotion(context, enabled);
    mtwnLeave(context);
    return result;
}

mtwnResult mtwnContext_EnableReducedMotion(mtwnContext* context, bool enabled)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnContext_EnableReducedMotionWork(context, enabled);
    mtwnFloatEnvLeave(env);
    return result;
}

bool mtwnContext_IsReducedMotionEnabled(const mtwnContext* context)
{
    return context != NULL && context->reducedMotion;
}

uint32_t mtwnContext_GetPlayingOnCount(const mtwnContext* context)
{
    return context != NULL ? context->playingOn : 0u;
}
