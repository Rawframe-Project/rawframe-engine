// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Events: records in the
// context's bounded queue, never callbacks. Each update appends what its
// players crossed, at most one record per player, node or mark, kind and
// direction, with the count of crossings, ordered by the time of the first
// crossing, then the players' creation order, then the clip's order; the
// host reads them when it likes. The queue holds the context def's
// eventCapacity records. One that arrives at a full queue is dropped and
// counted, never silently: mtwnContext_ReadEvents reports the drops since the last
// read, and mtwnContext_GetDroppedEvents all of them. Records arrive in
// the order they are made (players in creation order, each player's nodes
// in clip order), so a full queue keeps the first made and drops the rest
// of an update's; those kept are sorted. A context of eventCapacity 0
// makes no records and drops none: its clips' players skip the sweeps
// that find crossings, the cost of the records.

#ifndef MAUL_TWEEN_EVENT_H
#define MAUL_TWEEN_EVENT_H

#include "maul-tween/base.h"
#include "maul-tween/clip.h"
#include "maul-tween/context.h"
#include "maul-tween/player.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // What a record tells, as CSS Animations names a node's changes.
    typedef uint8_t mtwnEventKind;
    enum
    {
        // A node entered its active interval: from before it going
        // forwards, from after it going backwards. A transition began after
        // its delay. Node 1, the clip's root, starting is the player's.
        mtwn_eventStart = 1,
        // A node left its active interval: to after it going forwards, to
        // before it going backwards. A transition ended, committed.
        mtwn_eventEnd = 2,
        // A node's current iteration changed: made at the time an
        // iteration's start is reached, going forwards the start of the
        // iteration entered, going backwards the start of the one left,
        // whose iteration is current there and the one before it just
        // after.
        mtwn_eventIteration = 3,
        // A mark was crossed.
        mtwn_eventMark = 4,
        // A seek moved a player's time without crossing what lay between.
        mtwn_eventSeek = 5,
        // A player was canceled while it had a time, or a transition was
        // replaced by another on its target.
        mtwn_eventCancel = 6,
    };

    // A record.
    typedef struct mtwnEvent
    {
        // The host's time of the first crossing, in seconds.
        double hostTime;
        // The player's time then: seconds into its clip, or a transition's
        // since its delay ended.
        double clipTime;
        // The iteration of the node (a mark's parent) at the first and the
        // last crossing, as its timing reports it at that time: for an
        // iteration record the one entered going forwards and the one
        // left going backwards; 0 for a transition.
        double firstIteration;
        double lastIteration;
        // The crossings it stands for, 1 at least; UINT64_MAX for 2^64 or
        // more.
        uint64_t count;
        mtwnPlayerId player;
        // The node or mark; 0 for a transition.
        mtwnNode node;
        // A mark's key; 0 otherwise.
        uint32_t key;
        mtwnEventKind kind;
        // Whether the crossings went backwards.
        bool backwards;
    } mtwnEvent;

    /// Takes the oldest records off the queue, as many as fit, and reports
    /// how many were dropped at a full queue since the last read.
    ///
    /// @param context     The context.
    /// @param eventsOut   Receives the records; NULL only with a capacity of 0;
    ///                    nothing written on failure.
    /// @param capacity    Its length.
    /// @param countOut    Receives how many were taken; 0 on failure.
    /// @param droppedOut  Receives the records dropped since the last read (the
    ///                    count then starts again); may be NULL; 0 on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null context or
    ///         countOut, or a null array with a capacity.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnContext_ReadEvents(mtwnContext* context,
                                                              mtwnEvent* eventsOut,
                                                              uint32_t capacity, uint32_t* countOut,
                                                              uint64_t* droppedOut);

    /// Returns how many records waited in the queue, not yet taken.
    ///
    /// @param context  The context.
    /// @return The count; 0 for a null context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_API uint32_t mtwnContext_GetEventCount(const mtwnContext* context);

    /// Returns how many records were dropped since the context was made,
    /// each arriving at a full queue (its capacity is the context def's
    /// eventCapacity).
    ///
    /// @param context  The context.
    /// @return The count; 0 for a null context.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MTWN_API uint64_t mtwnContext_GetDroppedEvents(const mtwnContext* context);

#ifdef __cplusplus
}
#endif

#endif
