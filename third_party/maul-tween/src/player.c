// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Players: every player in
// creation order, and a clip's player by Web Animations Level 1's
// procedures, its pending play resolved at the next update, whose host
// time is the ready time. Times are seconds; NaN is unresolved.

#include "maul-tween/player.h"

#include "context_internal.h"
#include "fpenv.h"
#include "invariant.h"
#include "player_internal.h"

#include <math.h>
#include <string.h>

static mtwnPlayState StateOf(const mtwnClipPlay* play, double now, double end)
{
    double current = mtwnCurrentTime(play, now);
    if (isnan(current) && isnan(play->startTime) && !play->pending)
    {
        return mtwn_playIdle;
    }
    if (isnan(play->startTime) && !play->pending)
    {
        return mtwn_playPaused;
    }
    // Level 1 tests the finished state against the effective rate.
    double rate = mtwnEffectiveRate(play);
    if (!isnan(current) && ((rate > 0.0 && current >= end) || (rate < 0.0 && current <= 0.0)))
    {
        return mtwn_playFinished;
    }
    return mtwn_playRunning;
}

// The start time from which a player at rate reaches time t now. Finite
// by the bounds on rates and seeks (MTWN_SEEK_LIMIT).
static double StartFor(double now, double t, double rate)
{
    double start = now - t / rate;
    MTWN_ASSERT(isfinite(start));
    return start;
}

// Level 1's update of the hold time, with the time not held already
// worked out.
static void Hold(mtwnClipPlay* play, double now, double end, bool didSeek, double unconstrained)
{
    double previous = play->previousTime;
    if (play->rate > 0.0 && unconstrained >= end)
    {
        // Not past the previous time: a finished player stays where it was.
        double past = isnan(previous) || previous < end ? end : previous;
        play->holdTime = didSeek ? unconstrained : past;
    }
    else if (play->rate < 0.0 && unconstrained <= 0.0)
    {
        double past = isnan(previous) || previous > 0.0 ? 0.0 : previous;
        play->holdTime = didSeek ? unconstrained : past;
    }
    else if (play->rate != 0.0)
    {
        if (didSeek && !isnan(play->holdTime))
        {
            play->startTime = StartFor(now, play->holdTime, play->rate);
        }
        play->holdTime = (double)NAN;
    }
}

// Level 1's update of the finished state.
static void UpdateFinished(mtwnClipPlay* play, double now, double end, bool didSeek)
{
    double unconstrained = (double)NAN;
    if (didSeek)
    {
        unconstrained = mtwnCurrentTime(play, now);
    }
    else if (!isnan(play->startTime))
    {
        unconstrained = (now - play->startTime) * play->rate;
    }
    if (!isnan(unconstrained) && !isnan(play->startTime) && !play->pending)
    {
        Hold(play, now, end, didSeek, unconstrained);
    }
    play->previousTime = mtwnCurrentTime(play, now);
}

// Level 1's silent setting of the current time.
static void SilentlySet(mtwnClipPlay* play, double now, double seek)
{
    if (!isnan(play->holdTime) || isnan(play->startTime) || play->rate == 0.0)
    {
        play->holdTime = seek;
    }
    else
    {
        play->startTime = StartFor(now, seek, play->rate);
    }
    play->previousTime = (double)NAN;
}

void mtwnSetClipTime(mtwnClipPlay* play, double now, double end, double seek)
{
    // A seek's time is resolved: a finite argument, a label's start, or the
    // time a rate change keeps.
    MTWN_ASSERT(!isnan(seek));
    SilentlySet(play, now, seek);
    UpdateFinished(play, now, end, true);
    // Held there or running from there, the player's time is resolved.
    MTWN_ASSERT(!isnan(mtwnCurrentTime(play, now)));
}

// Level 1's play with auto-rewind; the pending play is resolved by the next
// update.
mtwnResult mtwnPlayClip(mtwnClipPlay* play, double now, double end)
{
    double seek = (double)NAN;
    double current = mtwnCurrentTime(play, now);
    double rate = mtwnEffectiveRate(play);
    if (rate >= 0.0 && (isnan(current) || current < 0.0 || current >= end))
    {
        seek = 0.0;
    }
    else if (rate < 0.0 && (isnan(current) || current <= 0.0 || current > end))
    {
        if (isinf(end))
        {
            return mtwn_errorState;
        }
        seek = end;
    }
    if (!isnan(seek))
    {
        // A rewind starts afresh: the next sweep takes its point.
        play->holdTime = seek;
        play->swept = (double)NAN;
        play->sweptWay = 0;
    }
    if (!isnan(play->holdTime))
    {
        play->startTime = (double)NAN;
    }
    // A seek sets the hold time.
    MTWN_ASSERT(isnan(seek) || play->holdTime == seek);
    if (isnan(play->holdTime))
    {
        // Already running: nothing to do but a pending rate, which the next
        // update applies from the time it reaches.
        return mtwn_success;
    }
    play->pending = true;
    UpdateFinished(play, now, end, false);
    // A pending play holds its time until the next update starts it.
    MTWN_ASSERT(isnan(play->startTime) && !isnan(play->holdTime));
    return mtwn_success;
}

mtwnResult mtwnPauseClip(mtwnClipPlay* play, double now, double end)
{
    double current = mtwnCurrentTime(play, now);
    if (isnan(current))
    {
        if (mtwnEffectiveRate(play) < 0.0 && isinf(end))
        {
            return mtwn_errorState;
        }
        play->holdTime = mtwnEffectiveRate(play) >= 0.0 ? 0.0 : end;
    }
    play->pending = false;
    if (!isnan(play->startTime) && isnan(play->holdTime))
    {
        play->holdTime = (now - play->startTime) * play->rate;
    }
    mtwnApplyPendingRate(play);
    play->startTime = (double)NAN;
    UpdateFinished(play, now, end, false);
    // A paused player holds its time, with nothing pending.
    MTWN_ASSERT(isnan(play->startTime) && !isnan(play->holdTime) && !play->pending &&
                isnan(play->pendingRate));
    return mtwn_success;
}

mtwnResult mtwnFinishClip(mtwnClipPlay* play, double now, double end)
{
    double rate = mtwnEffectiveRate(play);
    if (rate == 0.0 || (rate > 0.0 && isinf(end)))
    {
        return mtwn_errorState;
    }
    // The pending rate is applied now, keeping the time reached.
    if (!isnan(play->pendingRate) && !isnan(play->startTime) && isnan(play->holdTime))
    {
        play->holdTime = (now - play->startTime) * play->rate;
        play->startTime = (double)NAN;
    }
    mtwnApplyPendingRate(play);
    double limit = play->rate > 0.0 ? end : 0.0;
    SilentlySet(play, now, limit);
    if (isnan(play->startTime))
    {
        play->startTime = StartFor(now, limit, play->rate);
    }
    play->pending = false;
    // Held exactly at the limit, as Level 1's exact arithmetic leaves it:
    // a start rebased by limit / rate can round the time just short of it.
    Hold(play, now, end, true, limit);
    // A finished player holds its limit, its start resolved.
    MTWN_ASSERT(play->holdTime == limit && !isnan(play->startTime) && !play->pending);
    play->previousTime = mtwnCurrentTime(play, now);
    return mtwn_success;
}

// A rate change keeps the current time: a moving player changes at the next
// update, as Level 1's seamless update does at its ready time; a held or
// idle one at once.
void mtwnSetClipRate(mtwnClipPlay* play, double now, double end, double rate)
{
    bool moving = !isnan(play->startTime) && isnan(play->holdTime);
    // Running at a rate of 0, its time held, it is running all the same:
    // its new rate waits for the next update too.
    bool held = !play->pending && !isnan(play->startTime) && play->rate == 0.0 &&
                StateOf(play, now, end) == mtwn_playRunning;
    if (moving || held)
    {
        play->pendingRate = rate;
        return;
    }
    double previous = mtwnCurrentTime(play, now);
    play->rate = rate;
    play->pendingRate = (double)NAN;
    if (!isnan(previous))
    {
        mtwnSetClipTime(play, now, end, previous);
    }
}

// Whether a player runs under reduced motion though not essential: one
// that could not finish.
static bool PlaysOn(const mtwnContext* context, const mtwnClipPlay* play, mtwnPlayState state)
{
    return context->reducedMotion && !play->essential && state == mtwn_playRunning;
}

// Applies a pending rate from the time reached, and starts a pending play
// at this update's time, its ready time.
static void StartPending(mtwnClipPlay* play, double now)
{
    if (!isnan(play->pendingRate) && !play->pending)
    {
        // The old rate ran until now; the new one runs from the time reached.
        double current = mtwnCurrentTime(play, now);
        mtwnApplyPendingRate(play);
        if (play->rate != 0.0)
        {
            play->startTime = StartFor(now, current, play->rate);
        }
        else
        {
            play->holdTime = current;
            play->startTime = now;
        }
    }
    if (play->pending)
    {
        // The pending play's ready time is this update's. A play is
        // pending only with its hold time resolved (Play), and nothing
        // clears it while the start time is unresolved, so Level 1's other
        // case, a start time with a pending rate, is StartPending's first.
        MTWN_ASSERT(!isnan(play->holdTime));
        play->pending = false;
        mtwnApplyPendingRate(play);
        play->startTime = play->rate != 0.0 ? StartFor(now, play->holdTime, play->rate) : now;
        if (play->rate != 0.0)
        {
            play->holdTime = (double)NAN;
        }
    }
}

bool mtwnAdvanceClipPlayer(mtwnContext* context, mtwnWork* work, uint32_t player, double before,
                           double now)
{
    mtwnClipPlay* play = &mtwnPlayerAt(context, player)->play;
    double end = mtwnEffectEnd(context, play);
    // The rate its time ran at since the last update, 0 if it was held.
    double ranAt =
        !play->pending && !isnan(play->startTime) && isnan(play->holdTime) ? play->rate : 0.0;
    StartPending(play, now);
    UpdateFinished(play, now, end, false);
    // Under reduced motion one not essential finishes, as a finish does,
    // what lies between crossed at this time; one with
    // an infinite end cannot and plays on. It is judged after the finished
    // state's update, by the state this update leaves.
    double reached = mtwnCurrentTime(play, now);
    if (context->reducedMotion && !play->essential && StateOf(play, now, end) == mtwn_playRunning &&
        mtwnFinishClip(play, now, end) == mtwn_success)
    {
        // A sweep not yet begun takes where the player was first, as a
        // first update does, so its starts are crossed.
        if (isnan(play->swept))
        {
            mtwnSweepEvents(context, work, player, reached, 0.0, now, now);
        }
        ranAt = 0.0;
        before = now;
    }
    mtwnPlayState state = StateOf(play, now, end);
    if (state == mtwn_playIdle)
    {
        return false;
    }
    // A player of the same clip at the same time as the last the work
    // advanced is near it, and may take its results.
    double current = mtwnCurrentTime(play, now);
    mtwnSharing* sharing = &work->sharing;
    bool near =
        sharing->nearClip == play->clip.index1 && mtwnSameBits(sharing->nearCurrent, current);
    sharing->nearClip = play->clip.index1;
    sharing->nearCurrent = current;
    if (near)
    {
        mtwnSweepEventsNear(context, work, player, current, ranAt, before, now);
    }
    else
    {
        mtwnSweepEvents(context, work, player, current, ranAt, before, now);
    }
    // Only a running player moves; one not kept commits its end and goes.
    bool done = state == mtwn_playFinished && !play->keep;
    double rate = state == mtwn_playRunning ? play->rate : 0.0;
    if (near)
    {
        mtwnSampleClipNear(context, work, play, current, rate, play->rate < 0.0, done);
    }
    else
    {
        mtwnSampleClip(context, work, play, current, rate, play->rate < 0.0, done);
    }
    return done;
}

// Whether the bindings name slots the clip's tracks use, each once, and
// live targets of their kinds.
static bool AreBindings(const mtwnContext* context, const mtwnPlayerDef* def,
                        const mtwnClipSlot* clip)
{
    if (def->bindingCount > 0 && def->bindings == NULL)
    {
        return false;
    }
    for (uint32_t i = 0; i < def->bindingCount; ++i)
    {
        const mtwnBinding* binding = &def->bindings[i];
        const mtwnTargetSlot* target = (const mtwnTargetSlot*)mtwnPoolFind(
            &context->targets, binding->target.index1, binding->target.generation);
        if (binding->slot >= clip->slotCount || target == NULL ||
            binding->composite > mtwn_compositeAdd || !mtwnIsWeight((double)binding->weight) ||
            clip->slotKinds[binding->slot] != target->kind + 1)
        {
            return false;
        }
        for (uint32_t j = 0; j < i; ++j)
        {
            if (def->bindings[j].slot == def->bindings[i].slot)
            {
                return false;
            }
        }
    }
    return true;
}

static mtwnResult Misuse(mtwnContext* context, mtwnResult result)
{
    mtwnCountMisuse(context);
    return result;
}

static void BeginClips(mtwnContext* context, double before, double now)
{
    context->playingOn = 0;
    // With groups to run (two players at least), each marks its players'
    // alone targets itself.
    uint32_t first = context->firstClipPlayer;
    bool grouped =
        context->works != NULL && first != 0 && mtwnPlayerAt(context, first)->play.nextClip != 0;
    if (grouped)
    {
        // The groups work out their players' bindings' alone and mark those
        // targets; the others, which no group touches, after them.
        mtwnRunClipGroups(context, before, now);
    }
    // The count the groups skip by is the targets' own.
    MTWN_ASSERT(!grouped || context->layeredTargets == mtwnCountLayered(context));
    for (uint32_t index1 = grouped && context->layeredTargets == 0 ? 0 : first; index1 != 0;)
    {
        mtwnPlayerSlot* slot = mtwnPlayerAt(context, index1);
        if (grouped)
        {
            (void)mtwnMarkUnwrittenOf(context, &slot->play, false, NULL);
        }
        else
        {
            mtwnMarkUnwritten(context, &slot->play);
        }
        index1 = slot->play.nextClip;
    }
    if (!grouped)
    {
        mtwnRunClipGroups(context, before, now);
    }
}

static void AdvanceClip(mtwnContext* context, uint32_t player, double before, double now)
{
    bool done = context->stagedCount > 0
                    ? mtwnApplyStaged(context, player, before, now)
                    : mtwnAdvanceClipPlayer(context, context->serial, player, before, now);
    const mtwnClipPlay* play = &mtwnPlayerAt(context, player)->play;
    if (!done && context->reducedMotion &&
        PlaysOn(context, play, StateOf(play, now, mtwnEffectEnd(context, play))))
    {
        ++context->playingOn;
    }
    if (done)
    {
        const mtwnClipPlay* ended = &mtwnPlayerAt(context, player)->play;
        if (context->stagedCount > 0)
        {
            mtwnResetStaged(context, ended);
        }
        else
        {
            mtwnResetUnwritten(context, ended);
        }
        mtwnFreePlayer(context, player);
    }
}

static void EndClips(mtwnContext* context)
{
    if (context->stagedCount > 0)
    {
        mtwnResetEveryStaged(context);
    }
    for (uint32_t index1 = context->stagedCount > 0 ? 0 : context->firstClipPlayer; index1 != 0;)
    {
        mtwnPlayerSlot* slot = mtwnPlayerAt(context, index1);
        mtwnResetUnwritten(context, &slot->play);
        index1 = slot->play.nextClip;
    }
    mtwnEndClipGroups(context);
}

static const mtwnClipUpdate s_clipUpdate = {BeginClips, AdvanceClip, EndClips,
                                            mtwnClipTrackVelocity};

static mtwnResult CreateWorker(mtwnContext* context, const mtwnPlayerDef* def,
                               mtwnPlayerId* playerOut)
{
    mtwnClipSlot* clip =
        (mtwnClipSlot*)mtwnPoolFind(&context->clips, def->clip.index1, def->clip.generation);
    if (clip == NULL)
    {
        return Misuse(context, mtwn_errorStale);
    }
    if (!clip->sealed)
    {
        return Misuse(context, mtwn_errorState);
    }
    if (isnan(def->rate) || !mtwnIsWeight((double)def->weight) || !AreBindings(context, def, clip))
    {
        return Misuse(context, mtwn_errorInvalid);
    }
    if (!mtwnIsRate(def->rate))
    {
        return Misuse(context, mtwn_errorLimit);
    }
    uint32_t start = 0;
    if (clip->slotCount > 0 && !mtwnTakeRun(&context->bindings, clip->slotCount, &start))
    {
        return mtwn_errorCapacity;
    }
    uint32_t index1 = mtwnTakePlayer(context, MTWN_PLAYER_CLIP);
    if (index1 == 0)
    {
        if (clip->slotCount > 0)
        {
            mtwnGiveRun(&context->bindings, start, clip->slotCount);
        }
        return mtwn_errorCapacity;
    }
    mtwnBound* bound = &context->bindings.entries[start];
    for (uint32_t s = 0; s < clip->slotCount; ++s)
    {
        bound[s] = (mtwnBound){.composite = mtwn_compositeReplace};
    }
    for (uint32_t i = 0; i < def->bindingCount; ++i)
    {
        const mtwnBinding* binding = &def->bindings[i];
        bound[binding->slot] = (mtwnBound){
            .target = binding->target, .weight = binding->weight, .composite = binding->composite};
        // A bound target is alive (AreBindings): one more writer.
        mtwnAddWriter(context, mtwnTargetAt(context, binding->target.index1), true);
    }
    mtwnClipPlay* play = &mtwnPlayerAt(context, index1)->play;
    *play = (mtwnClipPlay){.clip = def->clip,
                           .startTime = (double)NAN,
                           .holdTime = (double)NAN,
                           .previousTime = (double)NAN,
                           .rate = def->rate,
                           .weight = (double)def->weight,
                           .pendingRate = (double)NAN,
                           .swept = (double)NAN,
                           .previousClip = play->previousClip,
                           .nextClip = play->nextClip,
                           .bindingStart = start,
                           .bindingCount = clip->slotCount,
                           .keep = def->keep,
                           .essential = def->essential};
    ++clip->users;
    context->clipUpdate = &s_clipUpdate;
    if (def->play)
    {
        (void)mtwnPlayClip(play, mtwnTimelineNow(context), mtwnEffectEnd(context, play));
    }
    *playerOut = (mtwnPlayerId){index1, mtwnPlayerAt(context, index1)->header.generation};
    return mtwn_success;
}

static mtwnResult RunCreatePlayer(mtwnContext* context, const mtwnPlayerDef* def,
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
    if (def == NULL || playerOut == NULL || def->cookie != MTWN_PLAYER_DEF_COOKIE)
    {
        return Misuse(context, mtwn_errorInvalid);
    }
    mtwnResult result = CreateWorker(context, def, playerOut);
    return result;
}

static MTWN_NOINLINE mtwnResult mtwnCreatePlayerWork(mtwnContext* context, const mtwnPlayerDef* def,
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
    mtwnResult result = RunCreatePlayer(context, def, playerOut);
    mtwnLeave(context);
    return result;
}

mtwnResult mtwnCreatePlayer(mtwnContext* context, const mtwnPlayerDef* def, mtwnPlayerId* playerOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnCreatePlayerWork(context, def, playerOut);
    mtwnFloatEnvLeave(env);
    return result;
}

static mtwnResult RunDestroyPlayer(mtwnContext* context, mtwnPlayerId player)
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
    if (slot->type == MTWN_PLAYER_CLIP)
    {
        // Its targets show their bases again, until another player writes
        // them.
        for (uint32_t s = 0; s < slot->play.bindingCount; ++s)
        {
            uint32_t target = mtwnBoundTarget(context, &slot->play, s);
            if (target != 0)
            {
                mtwnWriteTarget(context, target, mtwnTargetAt(context, target)->base,
                                (mtwnVec4){0});
            }
        }
        mtwnFreePlayer(context, player.index1);
        return mtwn_success;
    }
    uint32_t index1 = slot->header.link;
    mtwnEndTransition(context, player.index1);
    // The target shows its base again.
    mtwnTargetSlot* target = mtwnTargetAt(context, index1);
    target->header.flags &= (uint8_t)~MTWN_TARGET_CLIP_LAZY;
    target->value = target->base;
    target->velocity = (mtwnVec4){0};
    mtwnMarkChanged(context, index1);
    return mtwn_success;
}

static MTWN_NOINLINE mtwnResult mtwnDestroyPlayerWork(mtwnContext* context, mtwnPlayerId player)
{
    if (!mtwnEnter(context))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunDestroyPlayer(context, player);
    mtwnLeave(context);
    return result;
}

mtwnResult mtwnDestroyPlayer(mtwnContext* context, mtwnPlayerId player)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnDestroyPlayerWork(context, player);
    mtwnFloatEnvLeave(env);
    return result;
}

bool mtwnPlayer_IsValid(const mtwnContext* context, mtwnPlayerId player)
{
    return context != NULL && mtwnFindPlayer(context, player) != NULL;
}

static mtwnResult RunPlayerGetState(mtwnContext* context, mtwnPlayerId player,
                                    mtwnPlayerState* stateOut)
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
    if (stateOut == NULL)
    {
        return Misuse(context, mtwn_errorInvalid);
    }
    // A transition's player is the wrong kind for a clip's state.
    if (slot->type != MTWN_PLAYER_CLIP)
    {
        return Misuse(context, mtwn_errorInvalid);
    }
    const mtwnClipPlay* play = &slot->play;
    double now = mtwnTimelineNow(context);
    double end = mtwnEffectEnd(context, play);
    mtwnPlayState state = StateOf(play, now, end);
    *stateOut = (mtwnPlayerState){.currentTime = mtwnCurrentTime(play, now),
                                  .rate = mtwnEffectiveRate(play),
                                  .duration = end,
                                  .state = state,
                                  .pending = play->pending || !isnan(play->pendingRate),
                                  .playsOnReduced = PlaysOn(context, play, state)};
    return mtwn_success;
}

static MTWN_NOINLINE mtwnResult mtwnPlayer_GetStateWork(mtwnContext* context, mtwnPlayerId player,
                                                        mtwnPlayerState* stateOut)
{
    // Cleared first, so a busy or missing context's failure clears it too.
    if (stateOut != NULL)
    {
        memset(stateOut, 0, sizeof *stateOut);
    }
    if (!mtwnEnter(context))
    {
        return mtwn_errorBusy;
    }
    mtwnResult result = RunPlayerGetState(context, player, stateOut);
    mtwnLeave(context);
    return result;
}

mtwnResult mtwnPlayer_GetState(mtwnContext* context, mtwnPlayerId player, mtwnPlayerState* stateOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnPlayer_GetStateWork(context, player, stateOut);
    mtwnFloatEnvLeave(env);
    return result;
}
