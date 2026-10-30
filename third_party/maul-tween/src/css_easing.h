// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// CSS Easing Level 2's curves, for
// src/easing.c: cubic-bezier with its end tangents, steps with the
// before flag, and linear() with its canonicalization.

#ifndef MAUL_TWEEN_SRC_CSS_EASING_H
#define MAUL_TWEEN_SRC_CSS_EASING_H

#include "maul-tween/easing.h"

// The largest control y of a cubic bezier: its polynomial's coefficients,
// at most 21 times it, stay finite.
#define MTWN_LARGEST_BEZIER_Y 0x1.0p1000

// Whether control points are those mtwnMakeCubicBezierEasing takes: both x
// from 0 to 1, both y at most MTWN_LARGEST_BEZIER_Y in magnitude.
bool mtwnValidCubicBezier(double x1, double y1, double x2, double y2);

// The CSS curves take any progress x but NaN: an infinite one is where a
// mode's 2p overflowed. Each is finite, a value past the doubles given as
// the largest double of its sign.

// cubic-bezier(control[0], control[1], control[2], control[3]) for control
// points it takes, a flat end tangent still its end at an infinite x.
double mtwnSampleCubicBezier(const double control[4], double x);

// Whether steps(count, position) is one mtwnMakeStepsEasing makes.
bool mtwnValidSteps(uint32_t count, mtwnStepPosition position);

// steps(count, position).
double mtwnSampleSteps(uint32_t count, mtwnStepPosition position, double x, bool before);

// linear() through at least 2 points: CSS's value through canonical
// points, and for any finite points, sorted or not, a finite value.
double mtwnSampleLinearPoints(const mtwnLinearPoint* points, uint32_t count, double x);

// Whether count points are canonical, as mtwnMakeLinearPointsEasing leaves
// them: inputs and outputs finite, inputs in order.
bool mtwnCanonicalLinearPoints(const mtwnLinearPoint* points, uint32_t count);

// Copies a linear() easing's points to `to`, which holds them, and points
// the easing at the copy.
void mtwnCopyLinearPoints(mtwnEasing* easing, mtwnLinearPoint* to);

// Validates count points (at least 2, finite outputs, inputs finite or
// NaN) and canonicalizes them in place as CSS does: true, or false with
// the points untouched.
bool mtwnCanonicalizeLinearPoints(mtwnLinearPoint* points, uint32_t count);

#endif // MAUL_TWEEN_SRC_CSS_EASING_H
