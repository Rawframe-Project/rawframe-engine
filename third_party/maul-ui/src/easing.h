// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Cubic Bezier easing curves from (0, 0) to (1, 1), as CSS defines them,
// solved by a fixed number of bisection steps, so the same input gives the
// same bits everywhere.

#ifndef MAUL_UI_SRC_EASING_H
#define MAUL_UI_SRC_EASING_H

// A curve's polynomial coefficients for x and y, from its two control
// points.
typedef struct muiCurve
{
    double ax;
    double bx;
    double cx;
    double ay;
    double by;
    double cy;
} muiCurve;

// The curve through control points (x1, y1) and (x2, y2), x1 and x2 in
// [0, 1].
muiCurve muiMakeCurve(double x1, double y1, double x2, double y2);

// The curve's y at x, for x in [0, 1].
double muiEase(const muiCurve* curve, double x);

#endif // MAUL_UI_SRC_EASING_H
