// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Runs of entries in a fixed array, handed out first fit and coalesced
// when given back: the players' bindings. A free run
// keeps its length and the next free run in its first entry.

#ifndef MAUL_TWEEN_SRC_RUNS_H
#define MAUL_TWEEN_SRC_RUNS_H

#include "maul-tween/context.h"
#include "maul-tween/player.h"

#include <stdbool.h>
#include <stdint.h>

// A bound slot: its target, its weight and its composite operation. A
// free run keeps its length and next run in the target's two fields.
typedef struct mtwnBound
{
    mtwnTargetId target;
    float weight;
    mtwnComposite composite;
    // With the host's task system: its target is this binding's alone, as
    // last counted (src/clip_tasks.c).
    bool alone;
} mtwnBound;

typedef struct mtwnRuns
{
    mtwnBound* entries;
    uint32_t capacity;
    // The first free run's start plus 1, 0 when none; free runs are kept
    // in order of their starts.
    uint32_t free;
} mtwnRuns;

// Runs over entries, all free.
mtwnRuns mtwnMakeRuns(mtwnBound* entries, uint32_t capacity);

// Takes a run of count entries, count above 0; false when none is long
// enough.
bool mtwnTakeRun(mtwnRuns* runs, uint32_t count, uint32_t* startOut);

// Gives back a run mtwnTakeRun handed out.
void mtwnGiveRun(mtwnRuns* runs, uint32_t start, uint32_t count);

#endif
