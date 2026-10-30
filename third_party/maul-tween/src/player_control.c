// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A clip's player's controls: play, pause, reverse, finish, cancel, the
// seeks and the rate and weight, each by Web Animations Level 1's
// procedures in the library's floating-point environment.

#include "context_internal.h"
#include "fpenv.h"
#include "player_internal.h"

#include "maul-tween/player.h"

#include <math.h>

static mtwnResult Misuse(mtwnContext* context, mtwnResult result)
{
    mtwnCountMisuse(context);
    return result;
}

// What a control asks of its player.
typedef enum Control
{
    s_play,
    s_pause,
    s_reverse,
    s_finish,
    s_cancel,
    s_seek,
    s_seekProgress,
    s_seekLabel,
    s_sweep,
    s_setRate,
    s_setWeight,
} Control;

// A seek jumped: nothing between is crossed, and a record says so.
static void Jumped(mtwnContext* context, uint32_t player, double now)
{
    mtwnClipPlay* play = &mtwnPlayerAt(context, player)->play;
    play->swept = mtwnCurrentTime(play, now);
    play->sweptWay = 0;
    if (!isnan(play->swept))
    {
        mtwnPlayerEvent(context, player, mtwn_eventSeek, now, play->swept);
    }
}

// A seek that sweeps: what lies between the time it leaves and the time it
// reaches is crossed now, at the last update's time.
static void Swept(mtwnContext* context, uint32_t player, double before, double now)
{
    mtwnClipPlay* play = &mtwnPlayerAt(context, player)->play;
    play->swept = before;
    double after = mtwnCurrentTime(play, now);
    if (!isnan(after))
    {
        uint32_t queued = context->eventCount;
        mtwnSweepEvents(context, context->serial, player, after, 0.0, now, now);
        mtwnSortEvents(context, queued);
    }
}

static mtwnResult Seeking(mtwnContext* context, uint32_t player, Control control, double argument,
                          uint32_t key);

// Whether a player's clip is the sealed clip it was made with, alive while
// it plays: player.c's mtwnCreatePlayer counts it a user, and clip.c's
// mtwnDestroyClip refuses a clip with users. Only an assertion calls it.
[[maybe_unused]] static inline bool PlaysItsClip(const mtwnContext* context,
                                                 const mtwnClipPlay* play)
{
    const mtwnClipSlot* clip = mtwnClipAt(context, play->clip.index1);
    return clip->header.generation == play->clip.generation && clip->sealed && clip->users >= 1;
}

// Whether a clip's end is one its player can hold: its root's children end,
// never negative or NaN, and a finite one within MTWN_SEEK_LIMIT (clip.c's
// Raise). Only an assertion calls it.
[[maybe_unused]] static inline bool IsClipEnd(double end)
{
    return end >= 0.0 && (end <= MTWN_SEEK_LIMIT || isinf(end));
}

static MTWN_NOINLINE mtwnResult ControlWorker(mtwnContext* context, uint32_t player,
                                              Control control, double argument, uint32_t key)
{
    // RunControl refused a transition: a clip's player has a play.
    MTWN_ASSERT(mtwnPlayerAt(context, player)->type == MTWN_PLAYER_CLIP);
    mtwnClipPlay* play = &mtwnPlayerAt(context, player)->play;
    MTWN_ASSERT(PlaysItsClip(context, play));
    double now = mtwnTimelineNow(context);
    double end = mtwnEffectEnd(context, play);
    MTWN_ASSERT(IsClipEnd(end));
    switch (control)
    {
    case s_play:
        return mtwnPlayClip(play, now, end);
    case s_pause:
        return mtwnPauseClip(play, now, end);
    case s_reverse:
    {
        // Level 1's reverse: the negated rate pending, then a play with
        // auto-rewind; both take effect at the next update.
        mtwnClipPlay before = *play;
        play->pendingRate = -mtwnEffectiveRate(play);
        // The rates a player takes are rates (mtwnIsRate), so is the
        // negation of one.
        MTWN_ASSERT(mtwnIsRate(play->pendingRate));
        mtwnResult result = mtwnPlayClip(play, now, end);
        if (result != mtwn_success)
        {
            *play = before;
        }
        return result;
    }
    case s_finish:
    {
        // Finishing runs to the end, crossing what lies before it.
        double before = mtwnCurrentTime(play, now);
        mtwnResult result = mtwnFinishClip(play, now, end);
        if (result == mtwn_success)
        {
            Swept(context, player, before, now);
        }
        return result;
    }
    case s_cancel:
        if (!isnan(mtwnCurrentTime(play, now)))
        {
            mtwnPlayerEvent(context, player, mtwn_eventCancel, now, mtwnCurrentTime(play, now));
        }
        play->swept = (double)NAN;
        mtwnApplyPendingRate(play);
        play->pending = false;
        play->holdTime = (double)NAN;
        play->startTime = (double)NAN;
        play->previousTime = (double)NAN;
        return mtwn_success;
    case s_setRate:
        mtwnSetClipRate(play, now, end, argument);
        return mtwn_success;
    case s_setWeight:
        play->weight = argument;
        return mtwn_success;
    default:
        return Seeking(context, player, control, argument, key);
    }
}

// The seeks: to a time, a share, a root label, or sweeping to a time.
static mtwnResult Seeking(mtwnContext* context, uint32_t player, Control control, double argument,
                          uint32_t key)
{
    mtwnClipPlay* play = &mtwnPlayerAt(context, player)->play;
    double now = mtwnTimelineNow(context);
    double end = mtwnEffectEnd(context, play);
    double target = argument;
    if (control == s_seekProgress)
    {
        // A share of an endless clip names no time, and no call changes a
        // sealed clip's end.
        if (!isfinite(end))
        {
            return mtwn_errorInvalid;
        }
        target = argument * end;
    }
    else if (control == s_seekLabel)
    {
        const mtwnClipSlot* clip = mtwnClipAt(context, play->clip.index1);
        uint32_t child = clip->nodes[0].firstChild;
        while (child != 0 && !(clip->nodes[child - 1].kind == mtwn_nodeLabel &&
                               clip->nodes[child - 1].key == key))
        {
            child = clip->nodes[child - 1].nextSibling;
        }
        if (child == 0)
        {
            return mtwn_errorInvalid;
        }
        // The root's children are its own nodes, within the clip.
        MTWN_ASSERT(child <= clip->nodeCount && clip->nodes[child - 1].parent == MTWN_ROOT_NODE);
        target = clip->nodes[child - 1].start;
    }
    // A time beyond the limit, a product that overflowed among them, would
    // overflow the start time (MTWN_SEEK_LIMIT). The time is never NaN: a
    // finite argument, its product with a finite end, or a label's start.
    if (!(fabs(target) <= MTWN_SEEK_LIMIT))
    {
        return mtwn_errorLimit;
    }
    double before = mtwnCurrentTime(play, now);
    mtwnSetClipTime(play, now, end, target);
    if (control == s_sweep && !isnan(before))
    {
        Swept(context, player, before, now);
    }
    else
    {
        Jumped(context, player, now);
    }
    return mtwn_success;
}

static mtwnResult RunControl(mtwnContext* context, mtwnPlayerId player, Control control,
                             double argument, uint32_t key)
{
    if (context == NULL)
    {
        return mtwn_errorInvalid;
    }
    mtwnPlayerSlot* slot = mtwnFindPlayer(context, player);
    if (slot == NULL)
    {
        return Misuse(context, mtwn_errorStale);
    }
    // A transition's player is the wrong kind for a clip's controls.
    if (slot->type != MTWN_PLAYER_CLIP)
    {
        return Misuse(context, mtwn_errorInvalid);
    }
    if (control == s_setWeight && !mtwnIsWeight(argument))
    {
        return Misuse(context, mtwn_errorInvalid);
    }
    if (control == s_setRate && mtwnCheckRate(argument) != mtwn_success)
    {
        return Misuse(context, mtwnCheckRate(argument));
    }
    // A NaN time or share names none; an infinite one passes
    // MTWN_SEEK_LIMIT, refused here before a product with an end of 0 could
    // make it NaN.
    bool seeks = control == s_seek || control == s_seekProgress || control == s_sweep;
    if (seeks && isnan(argument))
    {
        return Misuse(context, mtwn_errorInvalid);
    }
    if (seeks && isinf(argument))
    {
        return Misuse(context, mtwn_errorLimit);
    }
    mtwnResult result = ControlWorker(context, player.index1, control, argument, key);
    return result == mtwn_success ? result : Misuse(context, result);
}

// A control, the context entered for it.
static mtwnResult Apply(mtwnContext* context, mtwnPlayerId player, Control control, double argument,
                        uint32_t key)
{
    if (!mtwnEnter(context))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunControl(context, player, control, argument, key);
    mtwnLeave(context);
    return result;
}

static MTWN_NOINLINE mtwnResult mtwnPlayer_PlayWork(mtwnContext* context, mtwnPlayerId player)
{
    return Apply(context, player, s_play, 0.0, 0);
}

mtwnResult mtwnPlayer_Play(mtwnContext* context, mtwnPlayerId player)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnPlayer_PlayWork(context, player);
    mtwnFloatEnvLeave(env);
    return result;
}

static MTWN_NOINLINE mtwnResult mtwnPlayer_PauseWork(mtwnContext* context, mtwnPlayerId player)
{
    return Apply(context, player, s_pause, 0.0, 0);
}

mtwnResult mtwnPlayer_Pause(mtwnContext* context, mtwnPlayerId player)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnPlayer_PauseWork(context, player);
    mtwnFloatEnvLeave(env);
    return result;
}

static MTWN_NOINLINE mtwnResult mtwnPlayer_ReverseWork(mtwnContext* context, mtwnPlayerId player)
{
    return Apply(context, player, s_reverse, 0.0, 0);
}

mtwnResult mtwnPlayer_Reverse(mtwnContext* context, mtwnPlayerId player)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnPlayer_ReverseWork(context, player);
    mtwnFloatEnvLeave(env);
    return result;
}

static MTWN_NOINLINE mtwnResult mtwnPlayer_FinishWork(mtwnContext* context, mtwnPlayerId player)
{
    return Apply(context, player, s_finish, 0.0, 0);
}

mtwnResult mtwnPlayer_Finish(mtwnContext* context, mtwnPlayerId player)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnPlayer_FinishWork(context, player);
    mtwnFloatEnvLeave(env);
    return result;
}

static MTWN_NOINLINE mtwnResult mtwnPlayer_CancelWork(mtwnContext* context, mtwnPlayerId player)
{
    return Apply(context, player, s_cancel, 0.0, 0);
}

mtwnResult mtwnPlayer_Cancel(mtwnContext* context, mtwnPlayerId player)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnPlayer_CancelWork(context, player);
    mtwnFloatEnvLeave(env);
    return result;
}

static MTWN_NOINLINE mtwnResult mtwnPlayer_SeekWork(mtwnContext* context, mtwnPlayerId player,
                                                    double seconds)
{
    return Apply(context, player, s_seek, seconds, 0);
}

mtwnResult mtwnPlayer_Seek(mtwnContext* context, mtwnPlayerId player, double seconds)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnPlayer_SeekWork(context, player, seconds);
    mtwnFloatEnvLeave(env);
    return result;
}

static MTWN_NOINLINE mtwnResult mtwnPlayer_SweepWork(mtwnContext* context, mtwnPlayerId player,
                                                     double seconds)
{
    return Apply(context, player, s_sweep, seconds, 0);
}

mtwnResult mtwnPlayer_Sweep(mtwnContext* context, mtwnPlayerId player, double seconds)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnPlayer_SweepWork(context, player, seconds);
    mtwnFloatEnvLeave(env);
    return result;
}

static MTWN_NOINLINE mtwnResult mtwnPlayer_SeekProgressWork(mtwnContext* context,
                                                            mtwnPlayerId player, double progress)
{
    return Apply(context, player, s_seekProgress, progress, 0);
}

mtwnResult mtwnPlayer_SeekProgress(mtwnContext* context, mtwnPlayerId player, double progress)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnPlayer_SeekProgressWork(context, player, progress);
    mtwnFloatEnvLeave(env);
    return result;
}

static MTWN_NOINLINE mtwnResult mtwnPlayer_SeekLabelWork(mtwnContext* context, mtwnPlayerId player,
                                                         uint32_t key)
{
    return Apply(context, player, s_seekLabel, 0.0, key);
}

mtwnResult mtwnPlayer_SeekLabel(mtwnContext* context, mtwnPlayerId player, uint32_t key)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnPlayer_SeekLabelWork(context, player, key);
    mtwnFloatEnvLeave(env);
    return result;
}

static MTWN_NOINLINE mtwnResult mtwnPlayer_SetRateWork(mtwnContext* context, mtwnPlayerId player,
                                                       double rate)
{
    return Apply(context, player, s_setRate, rate, 0);
}

mtwnResult mtwnPlayer_SetRate(mtwnContext* context, mtwnPlayerId player, double rate)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnPlayer_SetRateWork(context, player, rate);
    mtwnFloatEnvLeave(env);
    return result;
}

static MTWN_NOINLINE mtwnResult mtwnPlayer_SetWeightWork(mtwnContext* context, mtwnPlayerId player,
                                                         float weight)
{
    return Apply(context, player, s_setWeight, (double)weight, 0);
}

mtwnResult mtwnPlayer_SetWeight(mtwnContext* context, mtwnPlayerId player, float weight)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnPlayer_SetWeightWork(context, player, weight);
    mtwnFloatEnvLeave(env);
    return result;
}
