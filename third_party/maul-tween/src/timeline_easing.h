// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A timeline's cubic bezier, for src/timeline_easing.c's
// mtwnSampleTimelineEasingWork and its test.

#ifndef MAUL_TWEEN_SRC_TIMELINE_EASING_H
#define MAUL_TWEEN_SRC_TIMELINE_EASING_H

#include <stdbool.h>

// A cubic bezier with both y from 0 to 1, as a timeline samples it: never
// decreasing in x to the bit, which the solver of mtwnSampleCubicBezier,
// stopping within 1e-14 in x and rounding y, does not keep. The same as it
// at 0, at 1 and outside, and within an ulp or so of it between, more near a
// vertical tangent, where the solver misses. skip is true but in a test,
// which checks that the steps it skips are the ones a bisection would take.
double mtwnSampleMonotoneCubicBezier(const double control[4], double x, bool skip);

#endif // MAUL_TWEEN_SRC_TIMELINE_EASING_H
