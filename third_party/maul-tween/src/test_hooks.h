// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Switches and counts for the tests and the fuzz targets, which no host
// needs. Nothing here is MTWN_API: the library's symbols are hidden but
// for the public functions, so a shared build exports none of these, and
// no header here is installed. A test reads what it needs through these
// functions, never through the library's structs.

#ifndef MAUL_TWEEN_SRC_TEST_HOOKS_H
#define MAUL_TWEEN_SRC_TEST_HOOKS_H

#include "maul-tween/clip.h"
#include "maul-tween/context.h"
#include "maul-tween/player.h"

#include <stdbool.h>
#include <stdint.h>

// Has every crossing record of the context's clip players counted, the
// sweep's shortcuts for records certainly missed or settled off, so a
// test can check that leaving them out changes nothing. Off as made; the
// update only branches on it.
void mtwnTestSetCountEvery(mtwnContext* context, bool on);

// What the sharing of players in step held after the last update, over
// the serial work and every group's.
typedef struct mtwnTestSharing
{
    // A kept sample, a kept sweep, and any key noted at all.
    bool sampleKept;
    bool sweepKept;
    bool noted;
} mtwnTestSharing;

mtwnTestSharing mtwnTestGetSharing(const mtwnContext* context);

// A clip player's inputs to one sweep of its records: its swept time (NaN
// for none) and way, its own rate, and the sweep's end, rate and host
// times.
typedef struct mtwnTestSweepInputs
{
    double swept;
    int8_t way;
    double playRate;
    double to;
    double rate;
    double hostFrom;
    double hostTo;
} mtwnTestSweepInputs;

// Sets a clip player's sweep inputs and sweeps its records on the serial
// work: alone, or near as one in step with the last swept, which takes the
// records last kept if its key is theirs.
void mtwnTestSweep(mtwnContext* context, mtwnPlayerId player, const mtwnTestSweepInputs* in,
                   bool near);

// Whether the context is inside a call, so host code called back from it
// is refused.
bool mtwnTestIsBusy(const mtwnContext* context);

// The clips of a context's clip players before an update, in creation
// order: the order the groups stage them in.
typedef struct mtwnTestOrder
{
    uint32_t count;
    uint32_t clips[128];
} mtwnTestOrder;

// What the groups of a task system did, summed over updates.
typedef struct mtwnTestTasks
{
    // Players the groups ran, and left to the serial pass.
    uint32_t staged;
    uint32_t serial;
    // Left to the serial pass for staging that ran out.
    uint32_t ranOut;
    // Updates in which a group dropped records past its staging.
    uint32_t dropped;
    // A clip of more nodes than a group keeps, staged anyway: with other
    // players, which must not be, or alone, in its own node times.
    uint32_t bigStaged;
    uint32_t bigAlone;
    // A clip's own node times written by a group while it has other
    // players, which must not be; updates in which a group sampled a clip
    // of one player in them.
    uint32_t touched;
    uint32_t ownTimes;
    // Players with a binding alone on its target that a group ran.
    uint32_t aloneInGroups;
} mtwnTestTasks;

// Before an update with a task system: notes the clip players' order and
// marks the own node times of the clips given, which only the serial
// pass may write.
void mtwnTestTasksBefore(mtwnContext* context, const mtwnClipId* clips, uint32_t clipCount,
                         mtwnTestOrder* order);

// After it: adds what the groups did.
void mtwnTestTasksAfter(const mtwnContext* context, const mtwnClipId* clips, uint32_t clipCount,
                        const mtwnTestOrder* order, mtwnTestTasks* tasks);

// The nodes of a sealed clip that share their timing with a sibling;
// with keep false, that sharing forced off for every one.
uint32_t mtwnTestShareTiming(mtwnContext* context, mtwnClipId clip, bool keep);

// The players a clip has.
uint32_t mtwnTestClipUsers(const mtwnContext* context, mtwnClipId clip);

// Empties the groups' staging of the kinds set: records 1, track results
// 2, changed targets 4.
void mtwnTestEmptyStaging(mtwnContext* context, unsigned kinds);

// Whether a clip player's binding of a slot was its target's alone in the
// last update with groups, and how many of a live player's were.
bool mtwnTestIsAlone(const mtwnContext* context, mtwnPlayerId player, uint32_t slot);
uint32_t mtwnTestAloneCount(const mtwnContext* context, mtwnPlayerId player);

// Records staged into the first group's work, one more than it holds.
typedef struct mtwnTestStaging
{
    uint32_t capacity;
    uint32_t staged;
    // The last staged record's key, keys counting from 1.
    uint32_t lastKey;
    uint64_t dropped;
    bool overflow;
    // Whether the next group's staging was left as it was.
    bool nextUntouched;
} mtwnTestStaging;

mtwnTestStaging mtwnTestStageRecords(mtwnContext* context);

#endif // MAUL_TWEEN_SRC_TEST_HOOKS_H
