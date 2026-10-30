// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a clip's player (src/player.c) and its controls
// (src/player_control.c) share: Web Animations Level 1's times and
// procedures on a clip's play state. Times are seconds; NaN is
// unresolved.

#ifndef MAUL_TWEEN_SRC_PLAYER_INTERNAL_H
#define MAUL_TWEEN_SRC_PLAYER_INTERNAL_H

#include "context_internal.h"

#include <math.h>

static inline mtwnPlayerSlot* mtwnFindPlayer(const mtwnContext* context, mtwnPlayerId player)
{
    return (mtwnPlayerSlot*)mtwnPoolFind(&context->players, player.index1, player.generation);
}

// The timeline time: the last update's host time, in seconds.
static inline double mtwnTimelineNow(const mtwnContext* context)
{
    return (double)context->lastTimeNs / 1e9;
}

static inline double mtwnEffectEnd(const mtwnContext* context, const mtwnClipPlay* play)
{
    return mtwnClipAt(context, play->clip.index1)->nodes[0].childrenEnd;
}

static inline double mtwnCurrentTime(const mtwnClipPlay* play, double now)
{
    if (!isnan(play->holdTime))
    {
        return play->holdTime;
    }
    if (isnan(play->startTime))
    {
        return (double)NAN;
    }
    return (now - play->startTime) * play->rate;
}

// The rate the player is going to: its pending one, if any.
static inline double mtwnEffectiveRate(const mtwnClipPlay* play)
{
    return isnan(play->pendingRate) ? play->rate : play->pendingRate;
}

// Level 1's applying of a pending playback rate.
static inline void mtwnApplyPendingRate(mtwnClipPlay* play)
{
    if (!isnan(play->pendingRate))
    {
        play->rate = play->pendingRate;
        play->pendingRate = (double)NAN;
    }
}

// A rate whose size keeps the time to its limit's resolution and, if not
// 0, every start time finite (MTWN_SEEK_LIMIT).
static inline bool mtwnIsRate(double rate)
{
    return rate == 0.0 || (fabs(rate) >= MTWN_MIN_RATE && fabs(rate) <= MTWN_RATE_LIMIT);
}

// A rate's refusal: none for a rate, mtwn_errorInvalid for NaN, and
// mtwn_errorLimit for one other than 0 outside [MTWN_MIN_RATE,
// MTWN_RATE_LIMIT] in size, infinite included.
static inline mtwnResult mtwnCheckRate(double rate)
{
    if (isnan(rate))
    {
        return mtwn_errorInvalid;
    }
    return mtwnIsRate(rate) ? mtwn_success : mtwn_errorLimit;
}

static inline bool mtwnIsWeight(double weight)
{
    return weight >= 0.0 && weight <= 1.0;
}

// Level 1's play with auto-rewind; the pending play is resolved by the next
// update.
mtwnResult mtwnPlayClip(mtwnClipPlay* play, double now, double end);

// Level 1's pause.
mtwnResult mtwnPauseClip(mtwnClipPlay* play, double now, double end);

// Level 1's finish.
mtwnResult mtwnFinishClip(mtwnClipPlay* play, double now, double end);

// A rate change keeps the current time: a moving player changes at the next
// update, as Level 1's seamless update does at its ready time; a held or
// idle one at once.
void mtwnSetClipRate(mtwnClipPlay* play, double now, double end, double rate);

// Level 1's setting of the current time.
void mtwnSetClipTime(mtwnClipPlay* play, double now, double end, double seek);

#endif // MAUL_TWEEN_SRC_PLAYER_INTERNAL_H
