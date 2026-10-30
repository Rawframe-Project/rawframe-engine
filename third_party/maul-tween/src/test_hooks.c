// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The tests' switches and counts (test_hooks.h).

#include "test_hooks.h"

#include "context_internal.h"

#include <string.h>

void mtwnTestSetCountEvery(mtwnContext* context, bool on)
{
    context->countEvery = on;
}

static void Add(mtwnTestSharing* out, const mtwnSharing* sharing)
{
    out->sampleKept = out->sampleKept || sharing->sampleKept;
    out->sweepKept = out->sweepKept || sharing->sweepKept;
    out->noted = out->noted || sharing->sampleValid || sharing->sweepValid;
}

void mtwnTestSweep(mtwnContext* context, mtwnPlayerId player, const mtwnTestSweepInputs* in,
                   bool near)
{
    mtwnClipPlay* play = &mtwnPlayerAt(context, player.index1)->play;
    play->swept = in->swept;
    play->sweptWay = in->way;
    play->rate = in->playRate;
    if (near)
    {
        mtwnSweepEventsNear(context, context->serial, player.index1, in->to, in->rate, in->hostFrom,
                            in->hostTo);
    }
    else
    {
        mtwnSweepEvents(context, context->serial, player.index1, in->to, in->rate, in->hostFrom,
                        in->hostTo);
    }
}

bool mtwnTestIsBusy(const mtwnContext* context)
{
    return context->busy;
}

mtwnTestSharing mtwnTestGetSharing(const mtwnContext* context)
{
    mtwnTestSharing out = {false, false, false};
    Add(&out, &context->serial->sharing);
    for (uint32_t g = 0; context->works != NULL && g < MTWN_GROUPS; ++g)
    {
        Add(&out, &context->works[g].sharing);
    }
    return out;
}

static const uint64_t s_poison = 0x7FF8DEADBEEF0001ull;

void mtwnTestTasksBefore(mtwnContext* context, const mtwnClipId* clips, uint32_t clipCount,
                         mtwnTestOrder* order)
{
    order->count = 0;
    for (uint32_t index1 = context->firstPlayer; index1 != 0;)
    {
        const mtwnPlayerSlot* slot = mtwnPlayerAt(context, index1);
        if (slot->type == MTWN_PLAYER_CLIP && order->count < 128)
        {
            order->clips[order->count++] = slot->play.clip.index1;
        }
        index1 = slot->later;
    }
    for (uint32_t c = 0; c < clipCount; ++c)
    {
        const mtwnClipSlot* clip = mtwnClipAt(context, clips[c].index1);
        for (uint32_t k = 0; k < clip->nodeCount; ++k)
        {
            memcpy(&clip->times[k].now, &s_poison, sizeof s_poison);
        }
    }
}

// The players of each clip in the update's order.
static void CountPlayers(const mtwnClipId* clips, uint32_t clipCount, const mtwnTestOrder* order,
                         uint32_t* players)
{
    for (uint32_t i = 0; i < order->count; ++i)
    {
        for (uint32_t c = 0; c < clipCount; ++c)
        {
            players[c] += clips[c].index1 == order->clips[i] ? 1u : 0u;
        }
    }
}

static bool AnyAlone(const mtwnContext* context, const mtwnStaged* staged)
{
    // A player that finished was freed in the update: not read.
    if (staged->done)
    {
        return false;
    }
    const mtwnClipPlay* play = &mtwnPlayerAt(context, staged->player)->play;
    for (uint32_t s = 0; s < play->bindingCount; ++s)
    {
        if (context->bindings.entries[play->bindingStart + s].alone)
        {
            return true;
        }
    }
    return false;
}

// One staged player: where it ran, given how many players its clip has.
static void CountStaged(const mtwnContext* context, const mtwnStaged* staged, uint32_t clip,
                        uint32_t of, mtwnTestTasks* tasks)
{
    tasks->aloneInGroups += !staged->serial && AnyAlone(context, staged) ? 1u : 0u;
    tasks->serial += staged->serial ? 1u : 0u;
    tasks->staged += staged->serial ? 0u : 1u;
    bool big = mtwnClipAt(context, clip)->nodeCount > MTWN_GROUP_NODES;
    tasks->bigStaged += big && !staged->serial && of != 1 ? 1u : 0u;
    tasks->bigAlone += big && !staged->serial && of == 1 ? 1u : 0u;
    tasks->ranOut += staged->serial && !big ? 1u : 0u;
}

// The clip's node times the update wrote over the poison.
static uint32_t CountWritten(const mtwnClipSlot* clip)
{
    uint32_t written = 0;
    for (uint32_t k = 0; k < clip->nodeCount; ++k)
    {
        uint64_t bits = 0;
        memcpy(&bits, &clip->times[k].now, sizeof bits);
        written += bits == s_poison ? 0u : 1u;
    }
    return written;
}

void mtwnTestTasksAfter(const mtwnContext* context, const mtwnClipId* clips, uint32_t clipCount,
                        const mtwnTestOrder* order, mtwnTestTasks* tasks)
{
    if (order->count < 2 || context->works == NULL || clipCount > 16)
    {
        return;
    }
    bool serialClip[16] = {false};
    uint32_t players[16] = {0};
    CountPlayers(clips, clipCount, order, players);
    for (uint32_t i = 0; i < order->count; ++i)
    {
        uint32_t of = 0;
        const mtwnStaged* staged = &context->staged[i];
        for (uint32_t c = 0; c < clipCount; ++c)
        {
            bool its = clips[c].index1 == order->clips[i];
            of += its ? players[c] : 0u;
            serialClip[c] = serialClip[c] || (staged->serial && its);
        }
        CountStaged(context, staged, order->clips[i], of, tasks);
    }
    for (uint32_t c = 0; c < clipCount; ++c)
    {
        uint32_t written = serialClip[c] ? 0u : CountWritten(mtwnClipAt(context, clips[c].index1));
        // A clip of one player is sampled in its own times; another's are
        // left alone, as groups may sample it side by side.
        tasks->touched += players[c] == 1 ? 0u : written;
        tasks->ownTimes += players[c] == 1 && written > 0 ? 1u : 0u;
    }
    for (uint32_t g = 0; g < MTWN_GROUPS; ++g)
    {
        tasks->dropped += context->works[g].dropped > 0 ? 1u : 0u;
    }
}

uint32_t mtwnTestShareTiming(mtwnContext* context, mtwnClipId clip, bool keep)
{
    mtwnClipSlot* slot = mtwnClipAt(context, clip.index1);
    uint32_t sharing = 0;
    for (uint32_t i = 0; i < slot->nodeCount; ++i)
    {
        sharing += slot->nodes[i].sharesTiming ? 1u : 0u;
        slot->nodes[i].sharesTiming = slot->nodes[i].sharesTiming && keep;
    }
    return sharing;
}

uint32_t mtwnTestClipUsers(const mtwnContext* context, mtwnClipId clip)
{
    return mtwnClipAt(context, clip.index1)->users;
}

void mtwnTestEmptyStaging(mtwnContext* context, unsigned kinds)
{
    for (uint32_t g = 0; context->works != NULL && g < MTWN_GROUPS; ++g)
    {
        mtwnWork* work = &context->works[g];
        work->recordCapacity = (kinds & 1u) != 0 ? 0u : work->recordCapacity;
        work->trackCapacity = (kinds & 2u) != 0 ? 0u : work->trackCapacity;
        work->changeCapacity = (kinds & 4u) != 0 ? 0u : work->changeCapacity;
        // Nowhere to write: a change staged past the capacity crashes.
        work->changes = (kinds & 4u) != 0 ? NULL : work->changes;
    }
}

bool mtwnTestIsAlone(const mtwnContext* context, mtwnPlayerId player, uint32_t slot)
{
    const mtwnClipPlay* play = &mtwnPlayerAt(context, player.index1)->play;
    return context->bindings.entries[play->bindingStart + slot].alone;
}

uint32_t mtwnTestAloneCount(const mtwnContext* context, mtwnPlayerId player)
{
    if (player.index1 == 0 ||
        mtwnPoolFind(&context->players, player.index1, player.generation) == NULL)
    {
        return 0;
    }
    const mtwnClipPlay* play = &mtwnPlayerAt(context, player.index1)->play;
    uint32_t alone = 0;
    for (uint32_t s = 0; s < play->bindingCount; ++s)
    {
        alone += context->bindings.entries[play->bindingStart + s].alone ? 1u : 0u;
    }
    return alone;
}

mtwnTestStaging mtwnTestStageRecords(mtwnContext* context)
{
    mtwnTestStaging out = {0, 0, 0, 0, false, false};
    if (context->works == NULL)
    {
        return out;
    }
    mtwnWork* first = &context->works[0];
    mtwnWork* next = &context->works[1];
    next->records[0].key = 77;
    out.capacity = first->recordCapacity;
    first->recordCount = 0;
    first->dropped = 0;
    first->overflow = false;
    mtwnEvent event = {0};
    event.count = 1;
    for (uint32_t i = 0; i <= out.capacity; ++i)
    {
        event.key = i + 1;
        mtwnWorkEvent(context, first, &event, 0.0);
    }
    out.staged = first->recordCount;
    out.lastKey = out.staged > 0 ? first->records[out.staged - 1].key : 0;
    out.dropped = first->dropped;
    out.overflow = first->overflow;
    out.nextUntouched = next->records[0].key == 77;
    return out;
}
