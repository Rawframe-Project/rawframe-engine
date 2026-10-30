// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// CSS Easing Level 2's curves, following the specification's algorithms
// step by step. The cubic bezier solves x(t) = x by Newton's method
// inside a bracket every step narrows, bisecting whenever a step would
// leave it or the slope is not positive; the stop
// on a tolerance is a comparison, so results are the same everywhere.
// mtwnSampleCubicBezierEasing is here, apart from mtwnSampleEasing's dispatch over every
// kind: with link-time optimization or unused sections dropped, a program
// that samples cubic beziers only through it links none of the other
// curves (the makers in src/easing.c bring them along otherwise).

#include "css_easing.h"

#include "fpenv.h"
#include "invariant.h"

#include <float.h>
#include <math.h>
#include <string.h>

// The solver stops once x(t) is this close to x, or the bracket narrower
// than an ulp of 1/2, or after this many steps.
static const double s_bezierTolerance = 1e-14;
static const double s_bezierBracket = 0x1.0p-52;
static const int s_bezierSteps = 64;

// The polynomial a t^3 + b t^2 + c t of one coordinate, from its control
// values p1 and p2 between 0 and 1.
typedef struct Cubic
{
    double a;
    double b;
    double c;
} Cubic;

static Cubic MakeCubic(double p1, double p2)
{
    Cubic cubic;
    cubic.c = 3.0 * p1;
    cubic.b = 3.0 * (p2 - p1) - cubic.c;
    cubic.a = 1.0 - cubic.c - cubic.b;
    return cubic;
}

static double Evaluate(const Cubic* cubic, double t)
{
    return ((cubic->a * t + cubic->b) * t + cubic->c) * t;
}

static double Slope(const Cubic* cubic, double t)
{
    return (3.0 * cubic->a * t + 2.0 * cubic->b) * t + cubic->c;
}

// A value past the doubles as the largest double of its sign.
static double Saturated(double value)
{
    return isinf(value) ? copysign(DBL_MAX, value) : value;
}

// An end tangent's value from (0, 0) or (1, 1), a slope times a distance
// from there: saturated, and a flat one's at an infinite distance (where
// a mode's 2p overflowed) still its end.
static double FromZero(double slope, double distance)
{
    double moved = slope * distance;
    return isnan(moved) ? 0.0 : Saturated(moved);
}

static double FromOne(double slope, double distance)
{
    double moved = slope * distance;
    return isnan(moved) ? 1.0 : Saturated(1.0 + moved);
}

bool mtwnValidCubicBezier(double x1, double y1, double x2, double y2)
{
    return x1 >= 0.0 && x1 <= 1.0 && x2 >= 0.0 && x2 <= 1.0 && fabs(y1) <= MTWN_LARGEST_BEZIER_Y &&
           fabs(y2) <= MTWN_LARGEST_BEZIER_Y;
}

// Whether x is strictly between 0 and 1. Only an assertion calls it.
[[maybe_unused]] static inline bool StrictlyInside(double x)
{
    return x > 0.0 && x < 1.0;
}

// Whether t lies in a bracket within [0, 1]. Only an assertion calls it.
[[maybe_unused]] static inline bool WithinBracket(double low, double t, double high)
{
    return 0.0 <= low && low <= t && t <= high && high <= 1.0;
}

double mtwnSampleCubicBezier(const double control[4], double x)
{
    double x1 = control[0];
    double y1 = control[1];
    double x2 = control[2];
    double y2 = control[3];
    if (x == 0.0 || x == 1.0)
    {
        return x;
    }
    if (x < 0.0)
    {
        // The tangent through P0 and P1, else P0 and P2, else flat at 0.
        if (x1 > 0.0)
        {
            return FromZero(y1 / x1, x);
        }
        return x2 > 0.0 ? FromZero(y2 / x2, x) : 0.0;
    }
    if (x > 1.0)
    {
        // The tangent through P2 and P3, else P1 and P3, else flat at 1.
        if (x2 < 1.0)
        {
            return FromOne((y2 - 1.0) / (x2 - 1.0), x - 1.0);
        }
        return x1 < 1.0 ? FromOne((y1 - 1.0) / (x1 - 1.0), x - 1.0) : 1.0;
    }
    Cubic xs = MakeCubic(x1, x2);
    Cubic ys = MakeCubic(y1, y2);
    double low = 0.0;
    double high = 1.0;
    double t = x;
    // The returns above take 0, 1 and everything outside; NaN passes them,
    // but no caller passes it, as mtwnSampleEasing refuses a progress not finite.
    MTWN_ASSERT(StrictlyInside(x));
    for (int i = 0; i < s_bezierSteps; ++i)
    {
        double error = Evaluate(&xs, t) - x;
        if (fabs(error) <= s_bezierTolerance || high - low <= s_bezierBracket)
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
        double slope = Slope(&xs, t);
        double next = t - error / slope;
        t = (slope > 0.0 && next >= low && next <= high) ? next : 0.5 * (low + high);
        // The bracket only narrows within [0, 1], and the step or the
        // midpoint stays inside it.
        MTWN_ASSERT(WithinBracket(low, t, high));
    }
    return Evaluate(&ys, t);
}

// mtwnSampleEasing's in and out curves of a cubic bezier, by the same arithmetic.
static double BezierIn(const double control[4], double x)
{
    return mtwnSampleCubicBezier(control, x);
}

static double BezierOut(const double control[4], double x)
{
    return 1.0 - mtwnSampleCubicBezier(control, 1.0 - x);
}

// mtwnSampleEasing's linear easing, or its cubic bezier in its mode, by the same
// arithmetic; an easing of another kind is not one.
static double EaseCubicBezier(const mtwnEasing* easing, double progress)
{
    if (easing->kind == mtwn_easingLinear)
    {
        return progress;
    }
    const double* c = easing->parameters;
    double p = progress;
    switch (easing->mode)
    {
    case mtwn_easeIn:
        return BezierIn(c, p);
    case mtwn_easeOut:
        return BezierOut(c, p);
    case mtwn_easeInOut:
        return p < 0.5 ? 0.5 * BezierIn(c, 2.0 * p) : 0.5 + 0.5 * BezierOut(c, 2.0 * p - 1.0);
    default:
        return p < 0.5 ? 0.5 * BezierOut(c, 2.0 * p) : 0.5 + 0.5 * BezierIn(c, 2.0 * p - 1.0);
    }
}

// The checks of mtwnValidEasing for the two kinds this samples, kept here
// so that sampling a cubic bezier links none of the other curves.
static MTWN_NOINLINE mtwnResult mtwnSampleCubicBezierEasingWork(const mtwnEasing* easing,
                                                                double progress, double* easedOut)
{
    if (easing == NULL || easing->mode > mtwn_easeOutIn || !isfinite(progress) || easedOut == NULL)
    {
        return mtwn_errorInvalid;
    }
    const double* c = easing->parameters;
    bool bezier =
        easing->kind == mtwn_easingCubicBezier && mtwnValidCubicBezier(c[0], c[1], c[2], c[3]);
    if (easing->kind != mtwn_easingLinear && !bezier)
    {
        return mtwn_errorInvalid;
    }
    *easedOut = EaseCubicBezier(easing, progress);
    return mtwn_success;
}

mtwnResult mtwnSampleCubicBezierEasing(const mtwnEasing* easing, double progress, double* easedOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnSampleCubicBezierEasingWork(easing, progress, easedOut);
    mtwnFloatEnvLeave(env);
    return result;
}

bool mtwnValidSteps(uint32_t count, mtwnStepPosition position)
{
    return position <= mtwn_jumpBoth && count >= (position == mtwn_jumpNone ? 2u : 1u);
}

// The jumps steps make from 0 to 1.
static double StepJumps(double steps, mtwnStepPosition position)
{
    if (position == mtwn_jumpNone)
    {
        return steps - 1.0;
    }
    return position == mtwn_jumpBoth ? steps + 1.0 : steps;
}

double mtwnSampleSteps(uint32_t count, mtwnStepPosition position, double x, bool before)
{
    double steps = (double)count;
    double scaled = x * steps;
    if (isinf(scaled))
    {
        // So far past the ends that x is an integer and the floor and the
        // jump at it vanish beside it: x steps of 1 / jumps each, saturated
        // where that passes the doubles too (jump-none's steps are longer
        // than 1, and a mode's 2p may have overflowed).
        return Saturated(x * (steps / StepJumps(steps, position)));
    }
    double step = floor(scaled);
    if (position == mtwn_jumpStart || position == mtwn_jumpBoth)
    {
        step += 1.0;
    }
    if (before && scaled == floor(scaled))
    {
        step -= 1.0;
    }
    if (x >= 0.0 && step < 0.0)
    {
        step = 0.0;
    }
    double jumps = StepJumps(steps, position);
    if (x <= 1.0 && step > jumps)
    {
        step = jumps;
    }
    return step / jumps;
}

// The share of the way from input a to input b that x lies at, for
// distinct finite inputs: with each distance halved when one overflows, so
// it is still their ratio.
static double Between(double a, double b, double x)
{
    double width = b - a;
    double offset = x - a;
    if (isinf(width) || isinf(offset))
    {
        width = 0.5 * b - 0.5 * a;
        offset = 0.5 * x - 0.5 * a;
    }
    return offset / width;
}

double mtwnSampleLinearPoints(const mtwnLinearPoint* points, uint32_t count, double x)
{
    // The last point whose input is at most x, or the first; then one
    // before the last, so a next point exists.
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        uint32_t middle = low + (high - low) / 2;
        if (points[middle].input <= x)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    uint32_t index = low == 0 ? 0 : low - 1;
    if (index == count - 1)
    {
        index -= 1;
    }
    // A linear() easing has at least two points, so the next one exists.
    MTWN_ASSERT(count >= 2 && index + 1 < count);
    const mtwnLinearPoint* a = &points[index];
    const mtwnLinearPoint* b = &points[index + 1];
    if (a->input == b->input)
    {
        return b->output;
    }
    double between = Between(a->input, b->input, x);
    double span = b->output - a->output;
    // Outputs of opposite signs near the double range overflow their
    // difference; the weighted sum stays between them, and is each at its
    // own input.
    double value = isinf(span) ? (1.0 - between) * a->output + between * b->output
                               : a->output + between * span;
    if (isfinite(value))
    {
        return value;
    }
    // Only past the segment, or with its inputs out of order: an infinite
    // share of a flat segment is its output, and a value past the doubles
    // the largest of its sign, the line's way from a.
    if (span == 0.0)
    {
        return a->output;
    }
    return (between > 0.0) == (span > 0.0) ? DBL_MAX : -DBL_MAX;
}

// Whether every output is finite and every input finite or not given.
static bool ArePointsValid(const mtwnLinearPoint* points, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        if (!isfinite(points[i].output) || isinf(points[i].input))
        {
            return false;
        }
    }
    return true;
}

// The given inputs, raised to the largest before them; the first and last
// filled in when not given.
static void RaiseInputs(mtwnLinearPoint* points, uint32_t count)
{
    double largest = -HUGE_VAL;
    for (uint32_t i = 0; i < count; ++i)
    {
        double input = points[i].input;
        if (isnan(input) && i == 0)
        {
            input = 0.0;
        }
        else if (isnan(input) && i == count - 1)
        {
            // CSS's max(1, largest), which the raise below completes.
            input = 1.0;
        }
        else if (isnan(input))
        {
            continue;
        }
        input = input > largest ? input : largest;
        points[i].input = input;
        largest = input;
    }
}

// Each run still without inputs, spread evenly between its neighbors,
// which have inputs now.
static void SpreadRuns(mtwnLinearPoint* points, uint32_t count)
{
    uint32_t i = 1;
    while (i < count - 1)
    {
        if (!isnan(points[i].input))
        {
            ++i;
            continue;
        }
        uint32_t end = i;
        while (isnan(points[end].input))
        {
            ++end;
        }
        double start = points[i - 1].input;
        double span = points[end].input - start;
        double gaps = (double)(end - i + 1);
        for (uint32_t k = i; k < end; ++k)
        {
            points[k].input = start + span * ((double)(k - i + 1) / gaps);
        }
        i = end;
    }
}

bool mtwnCanonicalLinearPoints(const mtwnLinearPoint* points, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        bool ordered = i == 0 || points[i].input >= points[i - 1].input;
        if (!isfinite(points[i].input) || !isfinite(points[i].output) || !ordered)
        {
            return false;
        }
    }
    return true;
}

void mtwnCopyLinearPoints(mtwnEasing* easing, mtwnLinearPoint* to)
{
    // memmove: the points are the host's, wherever it put them.
    memmove(to, easing->points, (size_t)easing->count * sizeof *to);
    easing->points = to;
}

bool mtwnCanonicalizeLinearPoints(mtwnLinearPoint* points, uint32_t count)
{
    if (points == NULL || count < 2 || !ArePointsValid(points, count))
    {
        return false;
    }
    RaiseInputs(points, count);
    SpreadRuns(points, count);
    return true;
}
