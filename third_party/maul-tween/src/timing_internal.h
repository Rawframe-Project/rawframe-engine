// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the timing shares with the crossings.

#ifndef MAUL_TWEEN_SRC_TIMING_INTERNAL_H
#define MAUL_TWEEN_SRC_TIMING_INTERNAL_H

#include "maul-tween/timing.h"

#include <stdbool.h>

// Whether an iteration runs forwards under a direction (Web Animations
// Level 1's directed progress).
bool mtwnIterationForwards(mtwnDirection direction, double iteration);

// Whether a timing has a repeat delay that takes time.
bool mtwnHasRepeatDelay(const mtwnTiming* timing);

// The iteration a scaled time is in, with a repeat delay: its cycles
// counted from the start's, at least the start's own; and its time into
// that cycle. The timing and the crossings take it alike, so a crossing
// is where the timing puts it, bit for bit. The quotient rounds either
// way: below the first iteration it is that one (339 times 0.2 over 0.2
// floors to 338), where the position holds the first's start as the
// same product, so nothing lies before it; rounded up to k it leaves a
// time an ulp before k's start, below 0, in the delay before it. Infinite
// for a position past the doubles' cycles, at a time or a rate near the
// largest double, and for one past them whose cycles' count rounds by
// more than one.
double mtwnCycleOf(double scaled, double start, double duration, double cycle, double* withinOut);

#endif
