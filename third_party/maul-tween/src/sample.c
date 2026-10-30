// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Sampling easings. The fit is Ramer, Douglas and
// Peucker's simplification on the continuous curve: a segment whose
// chord misses the curve by more than the tolerance at one of its
// probes splits there, depth first, so points come out in order. The
// split is kept within the segment's middle 3/4, so every child is at
// most 28/33 of its parent and the pending segments fit a fixed stack.
// The points are limited, so a tolerance the curve cannot meet costs at
// most about 2 MTWN_MAX_FIT_POINTS segments of 32 probes.

#include "maul-tween/sample.h"

#include "fpenv.h"
#include "invariant.h"
#include "valid.h"
#include "work.h"

#include <math.h>

enum
{
    // The probes inside a segment, evenly spaced.
    s_probes = 32,
    // The probes a split may fall on: the middle ones, from 5/33 to 28/33
    // of the segment.
    s_firstSplit = 5,
    s_lastSplit = 28,
    // Pending segments: one per level, and a segment narrower than
    // s_narrowest is never split, so 24 / log2(33 / 28) < 102 levels.
    s_stackDepth = 104,
};

static const double s_narrowest = 0x1.0p-24;

static MTWN_NOINLINE mtwnResult mtwnSampleEasingTableWork(const mtwnEasing* easing, uint32_t count,
                                                          float* valuesOut)
{
    if (!mtwnValidEasing(easing) || valuesOut == NULL || count < 2)
    {
        return mtwn_errorInvalid;
    }
    double last = (double)(count - 1);
    for (uint32_t i = 0; i < count; ++i)
    {
        valuesOut[i] = (float)mtwnSampleEasingWork(easing, (double)i / last, false);
    }
    return mtwn_success;
}

mtwnResult mtwnSampleEasingTable(const mtwnEasing* easing, uint32_t count, float* valuesOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnSampleEasingTableWork(easing, count, valuesOut);
    mtwnFloatEnvLeave(env);
    return result;
}

// A point of the curve.
typedef struct Point
{
    double x;
    double y;
} Point;

// The probe of a segment where its chord misses the curve most, or -1
// when it misses by no more than the tolerance anywhere.
static int WorstProbe(const mtwnEasing* easing, Point a, Point b, double tolerance)
{
    double width = b.x - a.x;
    double worst = tolerance;
    int index = -1;
    for (int k = 1; k <= s_probes; ++k)
    {
        double share = (double)k / (double)(s_probes + 1);
        double x = a.x + width * share;
        double chord = a.y + (b.y - a.y) * share;
        double miss = fabs(mtwnSampleEasingWork(easing, x, false) - chord);
        if (miss > worst)
        {
            worst = miss;
            index = k;
        }
    }
    return index;
}

// The point a segment splits at, its worst probe held within the middle
// probes; false when the curve is not finite there, as a host function's
// infinity, and no linear() holds the point.
static bool SplitAt(const mtwnEasing* easing, Point start, Point next, int probe, Point* split)
{
    probe = probe < s_firstSplit ? s_firstSplit : (probe > s_lastSplit ? s_lastSplit : probe);
    split->x = start.x + (next.x - start.x) * ((double)probe / (double)(s_probes + 1));
    // At least 5/33 of a segment no narrower than s_narrowest from each
    // end, far more than the rounding of x within [0, 1].
    MTWN_ASSERT(split->x > start.x && split->x < next.x);
    split->y = mtwnSampleEasingWork(easing, split->x, false);
    return isfinite(split->y);
}

// Whether the segment to next lies past start and, when wide enough to
// split, less than s_stackDepth deep: it is at most 102 deep, so the
// stack's own bound never stops a split. Only an assertion calls it.
[[maybe_unused]] static inline bool PendingWithin(Point start, Point next, int depth)
{
    return next.x > start.x && (next.x - start.x < s_narrowest || depth < s_stackDepth);
}

static MTWN_NOINLINE mtwnResult mtwnFitLinearEasingWork(const mtwnEasing* easing, double tolerance,
                                                        mtwnLinearPoint* pointsOut,
                                                        uint32_t capacity, uint32_t* countOut)
{
    if (!mtwnValidEasing(easing) || countOut == NULL || (pointsOut == NULL && capacity > 0) ||
        easing->kind == mtwn_easingSteps || !isfinite(tolerance) || tolerance <= 0.0)
    {
        return mtwn_errorInvalid;
    }
    Point stack[s_stackDepth];
    int depth = 0;
    Point start = {0.0, mtwnSampleEasingWork(easing, 0.0, false)};
    Point end = {1.0, mtwnSampleEasingWork(easing, 1.0, false)};
    if (!isfinite(start.y) || !isfinite(end.y))
    {
        return mtwn_errorInvalid;
    }
    uint32_t count = 0;
    if (capacity > 0)
    {
        pointsOut[0] = (mtwnLinearPoint){start.x, start.y};
    }
    count = 1;
    // The segment from start to the top of the stack is the one examined;
    // the stack holds the ends of the segments still to come.
    stack[depth++] = end;
    while (depth > 0)
    {
        Point next = stack[depth - 1];
        MTWN_ASSERT(PendingWithin(start, next, depth));
        int probe = -1;
        if (next.x - start.x >= s_narrowest && depth < s_stackDepth)
        {
            probe = WorstProbe(easing, start, next, tolerance);
        }
        if (probe < 0)
        {
            if (count == MTWN_MAX_FIT_POINTS)
            {
                *countOut = 0;
                return mtwn_errorLimit;
            }
            if (count < capacity)
            {
                pointsOut[count] = (mtwnLinearPoint){next.x, next.y};
            }
            count += 1;
            start = next;
            depth -= 1;
            continue;
        }
        if (!SplitAt(easing, start, next, probe, &stack[depth]))
        {
            return mtwn_errorInvalid;
        }
        depth += 1;
    }
    MTWN_ASSERT(count <= MTWN_MAX_FIT_POINTS);
    *countOut = count;
    return count <= capacity ? mtwn_success : mtwn_errorCapacity;
}

mtwnResult mtwnFitLinearEasing(const mtwnEasing* easing, double tolerance,
                               mtwnLinearPoint* pointsOut, uint32_t capacity, uint32_t* countOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnFitLinearEasingWork(easing, tolerance, pointsOut, capacity, countOut);
    mtwnFloatEnvLeave(env);
    return result;
}
