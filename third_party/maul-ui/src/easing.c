// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// x(t) = ((ax t + bx) t + cx) t, and y likewise. x is monotonic since
// the control points' x lie in [0, 1], so x(t) = x has one solution,
// found by a fixed number of bisection steps.

#include "easing.h"

#include <math.h>

enum
{
    // Leaves t within 2^-32 of the solution, below what a float shows.
    BISECTION_STEPS = 32,
};

muiCurve muiMakeCurve(double x1, double y1, double x2, double y2)
{
    muiCurve curve;
    curve.cx = 3.0 * x1;
    curve.bx = 3.0 * (x2 - x1) - curve.cx;
    curve.ax = 1.0 - curve.cx - curve.bx;
    curve.cy = 3.0 * y1;
    curve.by = 3.0 * (y2 - y1) - curve.cy;
    curve.ay = 1.0 - curve.cy - curve.by;
    return curve;
}

static double SampleX(const muiCurve* curve, double t)
{
    return ((curve->ax * t + curve->bx) * t + curve->cx) * t;
}

static double SampleY(const muiCurve* curve, double t)
{
    return ((curve->ay * t + curve->by) * t + curve->cy) * t;
}

// The t with x(t) = x, to 2^-32.
static double SolveX(const muiCurve* curve, double x)
{
    double low = 0.0;
    double high = 1.0;
    for (int i = 0; i < BISECTION_STEPS; i++)
    {
        double t = (low + high) * 0.5;
        if (SampleX(curve, t) < x)
        {
            low = t;
        }
        else
        {
            high = t;
        }
    }
    return (low + high) * 0.5;
}

double muiEase(const muiCurve* curve, double x)
{
    if (x <= 0.0)
    {
        return 0.0;
    }
    if (x >= 1.0)
    {
        return 1.0;
    }
    return SampleY(curve, SolveX(curve, x));
}
