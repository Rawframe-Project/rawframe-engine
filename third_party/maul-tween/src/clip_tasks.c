// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The clips' players on the host's task system. Before
// an update's serial pass, the clips' players in creation order are split
// into up to MTWN_GROUPS groups, which the host runs on any thread. Each
// group advances its players in its own scratch, staging their records and
// track results; a player writes only its own state. The first group's
// first players run before the others, which start from their sharing. The serial pass then
// takes each player in creation order, between the transitions, and
// queues its records and composes its results into its targets: what a
// serial update makes, bit for bit, whatever the host's split.

#include "context_internal.h"
#include "fpenv.h"

#include <string.h>

typedef struct GroupTask
{
    mtwnContext* context;
    double before;
    double now;
    // The first group's players run before the groups, and whether its
    // staging ran out in them.
    uint32_t seeded;
    bool seedFull;
} GroupTask;

// The players the first group runs before the others to seed them.
static const uint32_t s_seedPlayers = 3;

// Whether a player's staging lies within its group's work, in the order
// it was made: its records, its results, and its changes before and after
// its resets. Only an assertion calls it.
[[maybe_unused]] static inline bool StagedWithin(const mtwnStaged* staged, const mtwnWork* work)
{
    return staged->recordStart <= staged->recordEnd && staged->recordEnd <= work->recordCapacity &&
           staged->trackStart <= staged->trackEnd && staged->trackEnd <= work->trackCapacity &&
           staged->changeStart <= staged->resetStart && staged->resetStart <= staged->changeEnd &&
           staged->changeEnd <= work->changeCapacity;
}

// A group's players: [first, end) of the staged order.
static void GroupRange(uint32_t count, uint32_t group, uint32_t* first, uint32_t* end)
{
    uint32_t groups = count < MTWN_GROUPS ? count : MTWN_GROUPS;
    *first = (uint32_t)((uint64_t)group * count / groups);
    *end = (uint32_t)((uint64_t)(group + 1u) * count / groups);
}

// Whether a player whose group writes its alone targets in place, which
// cannot be undone, stages all it can make: a result per node and at most
// six records a node a sweep (a start, iterations and an end, each way),
// unless records past the staging are dropped as the queue would. Under
// reduced motion a player not yet swept sweeps twice, to where it was
// first and then to the end it is finished at.
static bool Fits(const mtwnContext* context, const mtwnWork* work, const mtwnClipPlay* play)
{
    uint64_t nodes = mtwnClipAt(context, play->clip.index1)->nodeCount;
    uint64_t sweeps = context->reducedMotion && isnan(play->swept) ? 2u : 1u;
    bool records = work->recordCapacity == context->def.eventCapacity ||
                   work->recordCount + 6u * sweeps * nodes <= work->recordCapacity;
    return records && work->trackCount + nodes <= work->trackCapacity &&
           work->changeCount + 2u * (uint64_t)play->bindingCount <= work->changeCapacity;
}

// Advances a group's player, staging what it makes; its alone targets
// are marked, written and shown their bases here. One whose clip has more
// nodes than a group keeps times for and other players is left to the
// serial pass, as is one with alone targets that might not fit, and one
// whose staging ran out, put back as it was, and every one after it
// (full).
static void RunStaged(const GroupTask* task, uint32_t group, uint32_t i, bool* full)
{
    mtwnContext* context = task->context;
    mtwnWork* work = &context->works[group];
    mtwnStaged* staged = &context->staged[i];
    mtwnClipPlay* play = &mtwnPlayerAt(context, staged->player)->play;
    staged->group = (uint8_t)group;
    staged->recordStart = work->recordCount;
    staged->trackStart = work->trackCount;
    staged->changeStart = work->changeCount;
    staged->done = false;
    uint32_t layered = 0;
    bool unfit =
        mtwnMarkUnwrittenOf(context, play, true, &layered) > 0 && !Fits(context, work, play);
    staged->layered = layered > 0;
    *full = *full || unfit;
    // The group keeps times for MTWN_GROUP_NODES nodes; a clip of one
    // player uses its own (mtwnTimesFor), however many.
    const mtwnClipSlot* clip = mtwnClipAt(context, play->clip.index1);
    staged->serial = *full || (clip->nodeCount > MTWN_GROUP_NODES && clip->users != 1u);
    if (!staged->serial)
    {
        mtwnClipPlay was = *play;
        uint64_t dropped = work->dropped;
        work->overflow = false;
        staged->done =
            mtwnAdvanceClipPlayer(context, work, staged->player, task->before, task->now);
        if (work->overflow)
        {
            // Only a player with no alone target can run out (Fits held
            // room for the rest), so it staged no change to undo.
            MTWN_ASSERT(work->changeCount == staged->changeStart);
            *play = was;
            work->recordCount = staged->recordStart;
            work->trackCount = staged->trackStart;
            work->dropped = dropped;
            staged->serial = true;
            *full = true;
        }
    }
    staged->resetStart = work->changeCount;
    if (!staged->serial)
    {
        mtwnResetAlone(context, play, work);
    }
    staged->recordEnd = work->recordCount;
    staged->trackEnd = work->trackCount;
    staged->changeEnd = work->changeCount;
    MTWN_ASSERT(StagedWithin(staged, work));
}

static MTWN_NOINLINE void RunGroup(const GroupTask* task, uint32_t group)
{
    uint32_t first = 0;
    uint32_t end = 0;
    GroupRange(task->context->stagedCount, group, &first, &end);
    // The first group goes on after the players it ran to seed the others.
    bool full = group == 0 && task->seedFull;
    for (uint32_t i = group == 0 ? task->seeded : first; i < end; ++i)
    {
        RunStaged(task, group, i, &full);
    }
}

// A work's sharing as another's, in its own tracks and records.
static void CopySharing(const mtwnContext* context, mtwnSharing* to, const mtwnSharing* from)
{
    mtwnSharedTrack* tracks = to->tracks;
    mtwnSharedRecord* records = to->records;
    *to = *from;
    to->tracks = tracks;
    to->records = records;
    // What is not kept is not read before it is worked out again.
    // Kept tracks are the clip's nodes, at most the capacity; ones kept in
    // an earlier update for a clip since destroyed are never read, its
    // generation passed.
    if (from->sampleKept)
    {
        uint32_t nodes = mtwnClipAt(context, from->sampleClip.index1)->nodeCount;
        nodes = nodes < from->trackCapacity ? nodes : from->trackCapacity;
        memcpy(tracks, from->tracks, sizeof *tracks * nodes);
    }
    if (from->sweepKept && from->recordCount > 0)
    {
        memcpy(records, from->records, sizeof *records * from->recordCount);
    }
}

// Runs the first group's first players here, before the groups, and
// gives every group the sharing they leave: by the third in step a
// clip's sample and sweep are kept, so the first players of the other
// groups take them as the serial pass's do, rather than each group
// working them out again. A work's sharing is valid for whichever
// player comes next, so the copy changes no result.
static void Seed(GroupTask* task, uint32_t groups)
{
    mtwnContext* context = task->context;
    uint32_t first = 0;
    uint32_t end = 0;
    GroupRange(context->stagedCount, 0, &first, &end);
    uint32_t seeded = end - first < s_seedPlayers ? end - first : s_seedPlayers;
    for (uint32_t i = first; i < first + seeded; ++i)
    {
        RunStaged(task, 0, i, &task->seedFull);
    }
    task->seeded = first + seeded;
    const mtwnSharing* sharing = &context->works[0].sharing;
    for (uint32_t g = 1; (sharing->sampleKept || sharing->sweepKept) && g < groups; ++g)
    {
        CopySharing(context, &context->works[g].sharing, sharing);
    }
}

// A range of groups, in the library's floating-point environment on
// whichever thread the host runs it.
static void RunGroups(int32_t startIndex, int32_t endIndex, void* taskContext)
{
    const GroupTask* task = (const GroupTask*)taskContext;
    uint64_t env = mtwnFloatEnvEnter();
    for (int32_t g = startIndex; g < endIndex; ++g)
    {
        RunGroup(task, (uint32_t)g);
    }
    mtwnFloatEnvLeave(env);
}

// The host's task hooks, which schedule and compute nothing of the
// library's, called in the host's own environment: each the one call
// between the switches.
static MTWN_NOINLINE void* Enqueue(const mtwnContextDef* def, GroupTask* task, int32_t groups)
{
    mtwnFloatHost host = mtwnFloatHostEnter();
    void* handle = def->enqueueTask(RunGroups, groups, 1, task, def->userTaskContext);
    mtwnFloatHostLeave(host);
    return handle;
}

static MTWN_NOINLINE void Finish(const mtwnContextDef* def, void* handle)
{
    mtwnFloatHost host = mtwnFloatHostEnter();
    def->finishTask(handle, def->userTaskContext);
    mtwnFloatHostLeave(host);
}

void mtwnRunClipGroups(mtwnContext* context, double before, double now)
{
    context->stagedCount = 0;
    context->stagedNext = 0;
    if (context->works == NULL)
    {
        return;
    }
    uint32_t count = 0;
    for (uint32_t index1 = context->firstPlayer; index1 != 0;)
    {
        const mtwnPlayerSlot* slot = mtwnPlayerAt(context, index1);
        if (slot->type == MTWN_PLAYER_CLIP)
        {
            MTWN_ASSERT(count < context->def.playerCapacity);
            context->staged[count++].player = index1;
            mtwnPlayerAt(context, index1)->play.stagedAt = count;
        }
        index1 = slot->later;
    }
    // One player gains nothing from the host's threads.
    if (count < 2)
    {
        return;
    }
    context->stagedCount = count;
    for (uint32_t g = 0; g < MTWN_GROUPS; ++g)
    {
        context->works[g].recordCount = 0;
        context->works[g].trackCount = 0;
        context->works[g].dropped = 0;
        context->works[g].changeCount = 0;
    }
    // Who writes the targets is as when the groups last worked it out,
    // or they work it out again (read only while they run).
    context->aloneFresh = context->aloneEpoch == context->writersEpoch;
    context->aloneEpoch = context->writersEpoch;
    GroupTask task = {context, before, now, 0, false};
    const mtwnContextDef* def = &context->def;
    int32_t groups = (int32_t)(count < MTWN_GROUPS ? count : MTWN_GROUPS);
    Seed(&task, (uint32_t)groups);
    void* handle = Enqueue(def, &task, groups);
    if (handle != NULL)
    {
        Finish(def, handle);
    }
}

bool mtwnApplyStaged(mtwnContext* context, uint32_t player, double before, double now)
{
    // The serial pass meets the clips' players in the creation order the
    // staging was made in, so the next is this player.
    MTWN_ASSERT(context->stagedNext < context->stagedCount &&
                context->staged[context->stagedNext].player == player);
    mtwnStaged* staged = &context->staged[context->stagedNext++];
    if (staged->serial)
    {
        ++context->serialAdvances;
        staged->done = mtwnAdvanceClipPlayer(context, context->serial, staged->player, before, now);
        return staged->done;
    }
    const mtwnWork* work = &context->works[staged->group];
    for (uint32_t r = staged->recordStart; r < staged->recordEnd; ++r)
    {
        mtwnPushEvent(context, &work->records[r], work->recordAlong[r]);
    }
    // The group's changes to alone targets between the results, in the
    // order the serial pass would have made them.
    uint32_t k = staged->changeStart;
    for (uint32_t t = staged->trackStart; t < staged->trackEnd; ++t)
    {
        for (; k < work->tracks[t].changesBefore; ++k)
        {
            context->changed[context->changedCount++] = work->changes[k];
        }
        mtwnApplyTrack(context, &work->tracks[t], NULL);
    }
    for (; k < staged->resetStart; ++k)
    {
        context->changed[context->changedCount++] = work->changes[k];
    }
    // Every change it staged before its resets is on the list, once.
    MTWN_ASSERT(k == staged->resetStart && context->changedCount <= context->targets.capacity);
    return staged->done;
}

void mtwnResetEveryStaged(mtwnContext* context)
{
    // The clips' chain is newest first: the staging walked from its end.
    // A player with every target its own has its group's changes put on
    // the list whole, its own slot not read.
    for (uint32_t i = context->stagedCount; i-- > 0;)
    {
        const mtwnStaged* staged = &context->staged[i];
        if (staged->done)
        {
            continue;
        }
        if (staged->serial || staged->layered)
        {
            mtwnResetStaged(context, &mtwnPlayerAt(context, staged->player)->play);
            continue;
        }
        const mtwnWork* work = &context->works[staged->group];
        for (uint32_t k = staged->resetStart; k < staged->changeEnd; ++k)
        {
            context->changed[context->changedCount++] = work->changes[k];
        }
    }
    MTWN_ASSERT(context->changedCount <= context->targets.capacity);
}

void mtwnResetStaged(mtwnContext* context, const mtwnClipPlay* play)
{
    // Every clip's player was staged when the groups ran: at 1 and up.
    MTWN_ASSERT(context->stagedCount == 0 ||
                (play->stagedAt >= 1 && play->stagedAt <= context->stagedCount));
    const mtwnStaged* staged =
        context->stagedCount > 0 ? &context->staged[play->stagedAt - 1u] : NULL;
    if (staged == NULL || staged->serial)
    {
        mtwnResetUnwritten(context, play);
        return;
    }
    // Binding by binding: an alone target its group showed, its change
    // put on the list here, the target itself not read (its group's core
    // wrote it); another shown now.
    const mtwnWork* work = &context->works[staged->group];
    uint32_t k = staged->resetStart;
    for (uint32_t s = 0; s < play->bindingCount; ++s)
    {
        const mtwnBound* bound = &context->bindings.entries[play->bindingStart + s];
        uint32_t target = bound->alone ? 0 : mtwnBoundTarget(context, play, s);
        if (bound->alone)
        {
            if (k < staged->changeEnd && work->changes[k] == bound->target.index1)
            {
                context->changed[context->changedCount++] = work->changes[k++];
            }
        }
        else if (target != 0 &&
                 (mtwnTargetAt(context, target)->header.flags & MTWN_TARGET_UNWRITTEN) != 0)
        {
            mtwnWriteTarget(context, target, mtwnTargetAt(context, target)->base, (mtwnVec4){0});
        }
    }
    // Its group staged its resets' changes in binding order, at most one
    // for each alone target, so every one was matched and put on the list.
    MTWN_ASSERT(k == staged->changeEnd);
}

void mtwnEndClipGroups(mtwnContext* context)
{
    if (context->stagedCount == 0)
    {
        return;
    }
    for (uint32_t g = 0; g < MTWN_GROUPS; ++g)
    {
        context->droppedEvents += context->works[g].dropped;
        context->droppedSinceRead += context->works[g].dropped;
    }
    context->stagedCount = 0;
}
