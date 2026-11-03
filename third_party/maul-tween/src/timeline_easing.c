// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A timeline's easing, which must never decrease: a cubic bezier sampled
// so that it never does to the bit, and every other kind as
// mtwnSampleEasing samples it. Only the clips' child times call it, so
// a program of easings, timings or transitions links none of it.

#include "timeline_easing.h"

#include "css_easing.h"
#include "invariant.h"
#include "work.h"

#include "maul-tween/easing.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

// The monotone cubic bezier samples t on the grid K 2^-52, K from 0 to
// 2^52, each t exact. A coordinate's polynomial with control values p1, p2
// from 0 to 1, B(t) = 3 p1 (1 - t)^2 t + 3 p2 (1 - t) t^2 + t^3, never
// decreases: its derivative's Bernstein coefficients p1, p2 - p1 and
// 1 - p2 give it at least 0 where p1 - p2 <= (p1 (1 - p2))^(1/2), which
// (1 - p1) p2 >= 0 grants. mtwnEvaluateCubic gives B(t) within ErrorBound on
// [0, 1]: mtwnMakeCubic rounds c within 3 u, b within 15 u and a within 24 u
// (u = 2^-53), 42 u at most at t up to 1, and Horner adds at most 6 u
// (|a| + |b| + |c|) of the coefficients it has.
static const double s_gridStep = 0x1.0p-52;
static const int64_t s_gridEnd = (int64_t)1 << 52;

static double ErrorBound(const mtwnCubic* cubic)
{
    return 0x1.0p-50 * (fabs(cubic->a) + fabs(cubic->b) + fabs(cubic->c)) + 0x1.0p-47;
}

// The grid's K a bisection need not test, given a guess: those up to below
// have a value clearly short of the target, those from above one clearly
// past it, 3 bounds clear, so the target less or plus them, rounded, is
// still 2 bounds from it. The window grows from the guess, 4 times a step.
static void DecidedBounds(const mtwnCubic* cubic, double target, double guess, int64_t* belowOut,
                          int64_t* aboveOut)
{
    double error = ErrorBound(cubic);
    double slope = fabs(mtwnCubicSlope(cubic, guess));
    double width = slope > 0.0 ? 3.0 * error / slope * (double)s_gridEnd : (double)s_gridEnd;
    int64_t start = (int64_t)(guess * (double)s_gridEnd);
    int64_t reach = width < (double)s_gridEnd ? (int64_t)width + 2 : s_gridEnd;
    double clearBelow = target - 3.0 * error;
    double clearAbove = target + 3.0 * error;
    int64_t below = 0;
    for (int64_t step = reach;; step *= 4)
    {
        below = start - step;
        if (below <= 0 || mtwnEvaluateCubic(cubic, (double)below * s_gridStep) < clearBelow)
        {
            break;
        }
    }
    int64_t above = s_gridEnd;
    for (int64_t step = reach;; step *= 4)
    {
        above = start + step;
        if (above >= s_gridEnd || mtwnEvaluateCubic(cubic, (double)above * s_gridStep) > clearAbove)
        {
            break;
        }
    }
    *belowOut = below < 0 ? 0 : below;
    *aboveOut = above > s_gridEnd ? s_gridEnd : above;
}

// The interval of the full bisection's first midpoint strictly between
// below and above: the grid point there with the most trailing zeros, the
// middle of the interval of its lowest bit. Neighbors are their own.
static void FirstOpen(int64_t below, int64_t above, int64_t* lowOut, int64_t* highOut)
{
    if (above - below <= 1)
    {
        *lowOut = below;
        *highOut = above;
        return;
    }
    uint64_t first = (uint64_t)below + 1;
    uint64_t last = (uint64_t)above - 1;
    uint64_t differ = first ^ last;
    for (int shift = 1; shift < 64; shift *= 2)
    {
        differ |= differ >> shift;
    }
    uint64_t middle = last & ~(differ >> 1);
    uint64_t half = middle & (~middle + 1);
    *lowOut = (int64_t)(middle - half);
    *highOut = (int64_t)(middle + half);
}

// The full bisection over the grid: from K in [0, 2^52], going up while
// the coordinate at the midpoint is at most the target (strict: below
// it), to the last K reached. Its midpoints are fixed, so for a target
// that is larger the test passes wherever it did, and the first test that
// differs sends the larger target up: its K is never smaller, whatever
// mtwnEvaluateCubic's rounding. With skip, the tests the error bound
// decides are not evaluated: below a K whose value is clearly short of the
// target, every value is (B never decreases), and above one clearly past
// it, none is, so the bisection runs as the full one would, from the first
// midpoint between the two.
static int64_t Bisect(const mtwnCubic* cubic, double target, bool strict, double guess, bool skip)
{
    int64_t below = 0;
    int64_t above = s_gridEnd;
    if (skip)
    {
        DecidedBounds(cubic, target, guess, &below, &above);
    }
    int64_t low = 0;
    int64_t high = s_gridEnd;
    FirstOpen(below, above, &low, &high);
    while (high - low > 1)
    {
        int64_t middle = low + (high - low) / 2;
        bool up = middle <= below;
        if (middle > below && middle < above)
        {
            double value = mtwnEvaluateCubic(cubic, (double)middle * s_gridStep);
            up = strict ? value < target : value <= target;
        }
        if (up)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }
    return low;
}

// Newton's method for the coordinate at a target from t, safeguarded, to
// the solver's tolerance: only a guess for Bisect, so any deterministic one
// serves.
static const double s_guessTolerance = 1e-14;

static double Guess(const mtwnCubic* cubic, double target, double t)
{
    double low = 0.0;
    double high = 1.0;
    for (int i = 0; i < 16; ++i)
    {
        double error = mtwnEvaluateCubic(cubic, t) - target;
        if (fabs(error) <= s_guessTolerance)
        {
            break;
        }
        if (error < 0.0)
        {
            low = t;
        }
        else
        {
            high = t;
        }
        double slope = mtwnCubicSlope(cubic, t);
        double next = t - error / slope;
        t = (slope > 0.0 && next >= low && next <= high) ? next : 0.5 * (low + high);
    }
    return t;
}

// The double of the bits.
static double BitsValue(uint64_t bits)
{
    double value = 0.0;
    memcpy(&value, &bits, sizeof value);
    return value;
}

// Whether y = v is reached at or before the grid's K: v at most 0, or the
// bisection for y below v ending before K. That bisection's K never
// decreases in v, so the v reached form a run from 0 up.
static bool Reached(const mtwnCubic* ys, double v, int64_t k, bool skip)
{
    if (v <= 0.0)
    {
        return true;
    }
    double t = (double)k * s_gridStep;
    return Bisect(ys, v, true, Guess(ys, v, t), skip) < k;
}

// Bits yes reached and no not, from 0 to 1's bits and one past, that
// bracket the last double reached at K: galloping from the seed's.
static void Gallop(const mtwnCubic* ys, double seed, int64_t k, bool skip, uint64_t* yesOut,
                   uint64_t* noOut)
{
    double unit = 1.0;
    uint64_t one = 0;
    uint64_t yes = 0;
    memcpy(&one, &unit, sizeof one);
    memcpy(&yes, &seed, sizeof yes);
    uint64_t no = one + 1;
    if (Reached(ys, seed, k, skip))
    {
        for (uint64_t step = 1; yes < one; step *= 2)
        {
            uint64_t next = one - yes > step ? yes + step : one;
            if (!Reached(ys, BitsValue(next), k, skip))
            {
                no = next;
                break;
            }
            yes = next;
        }
    }
    else
    {
        // 0 is always reached, so this ends.
        no = yes;
        for (uint64_t step = 1;; step *= 2)
        {
            uint64_t next = no > step ? no - step : 0;
            if (Reached(ys, BitsValue(next), k, skip))
            {
                yes = next;
                break;
            }
            no = next;
        }
    }
    *yesOut = yes;
    *noOut = no;
}

double mtwnSampleMonotoneCubicBezier(const double control[4], double x, bool skip)
{
    if (!(x > 0.0 && x < 1.0))
    {
        return mtwnSampleCubicBezier(control, x);
    }
    // A timeline's easing is monotonic; mtwnClip_AddTimeline refuses others.
    MTWN_ASSERT(control[1] >= 0.0 && control[1] <= 1.0 && control[3] >= 0.0 && control[3] <= 1.0);
    mtwnCubic xs = mtwnMakeCubic(control[0], control[2]);
    mtwnCubic ys = mtwnMakeCubic(control[1], control[3]);
    // The grid's t for x, which never decreases in x; then the largest
    // double from 0 to 1 reached by it, which never decreases in K, as the
    // doubles reached by a K are reached by every K after it.
    int64_t k = Bisect(&xs, x, false, Guess(&xs, x, x), skip);
    double seed = mtwnEvaluateCubic(&ys, (double)k * s_gridStep);
    seed = seed < 0.0 ? 0.0 : (seed > 1.0 ? 1.0 : seed);
    // The doubles from 0 to 1 in order are their bits in order: gallop from
    // the seed's to bracket the last one reached, then bisect between.
    uint64_t yes = 0;
    uint64_t no = 0;
    Gallop(&ys, seed, k, skip, &yes, &no);
    while (no - yes > 1)
    {
        uint64_t middle = yes + (no - yes) / 2;
        if (Reached(&ys, BitsValue(middle), k, skip))
        {
            yes = middle;
        }
        else
        {
            no = middle;
        }
    }
    return BitsValue(yes);
}

MTWN_NOINLINE double mtwnSampleTimelineEasingWork(const mtwnEasing* easing, double progress,
                                                  bool before, bool* hostNan)
{
    if (easing->kind == mtwn_easingLinear)
    {
        return progress;
    }
    if (easing->kind != mtwn_easingCubicBezier)
    {
        return mtwnSampleEasingNoted(easing, progress, before, hostNan);
    }
    // mtwnSampleEasingNoted's reflections of a CSS curve, unclamped, with
    // the monotone curve.
    const double* c = easing->parameters;
    double p = progress;
    switch (easing->mode)
    {
    case mtwn_easeIn:
        return mtwnSampleMonotoneCubicBezier(c, p, true);
    case mtwn_easeOut:
        return 1.0 - mtwnSampleMonotoneCubicBezier(c, 1.0 - p, true);
    case mtwn_easeInOut:
        return p < 0.5 ? 0.5 * mtwnSampleMonotoneCubicBezier(c, 2.0 * p, true)
                       : 0.5 + 0.5 * (1.0 - mtwnSampleMonotoneCubicBezier(c, 1.0 - (2.0 * p - 1.0),
                                                                          true));
    default:
        return p < 0.5 ? 0.5 * (1.0 - mtwnSampleMonotoneCubicBezier(c, 1.0 - 2.0 * p, true))
                       : 0.5 + 0.5 * mtwnSampleMonotoneCubicBezier(c, 2.0 * p - 1.0, true);
    }
}
