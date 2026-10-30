// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Staggers. The distances' least and range are found
// first, without writing, so a spread that is not finite is refused with
// nothing written; then each item's distance is written into the
// caller's offsets and normalized, eased and scaled in place, so nothing
// is allocated. The distances use only exactly
// rounded operations and the easing is the library's own, so the
// offsets are the same bits on every platform. A random order is an
// inside-out shuffle driven by SplitMix64 of the seed.

#include "fpenv.h"
#include "stagger_internal.h"
#include "valid.h"
#include "work.h"

#include "maul-tween/timing.h"

#include <math.h>
#include <stdbool.h>

// A point's ratios within this size keep every distance far from
// overflow: 2^20 rows or columns beyond the grid, which has at most 2^32.
static const double s_pointLimit = 0x1.0p20;

// The grid the items fill, and the origin's place in it.
typedef struct Grid
{
    uint32_t columns;
    double lastX;
    double lastY;
    double originX;
    double originY;
} Grid;

mtwnStagger mtwnDefaultStagger(void)
{
    mtwnStagger stagger = {0};
    stagger.spacing = 0.1;
    stagger.easing = mtwnMakeLinearEasing();
    return stagger;
}

static bool IsValid(const mtwnStagger* stagger, uint32_t count, const double* offsetsOut)
{
    if (stagger == NULL || (count != 0 && offsetsOut == NULL))
    {
        return false;
    }
    bool known = stagger->origin <= mtwn_staggerRandom && stagger->axis <= mtwn_staggerY &&
                 stagger->spacingKind <= mtwn_staggerAmount;
    bool finite = isfinite(stagger->start) && isfinite(stagger->spacing) &&
                  fabs(stagger->pointX) <= s_pointLimit && fabs(stagger->pointY) <= s_pointLimit;
    bool index = stagger->origin != mtwn_staggerIndex || count == 0 || stagger->index < count;
    return known && finite && index && mtwnValidEasing(&stagger->easing);
}

static Grid MakeGrid(const mtwnStagger* stagger, uint32_t count)
{
    Grid grid = {0};
    grid.columns = stagger->columns == 0 || stagger->columns > count ? count : stagger->columns;
    grid.lastX = (double)(grid.columns - 1u);
    uint32_t lastRow = (count - 1u) / grid.columns;
    grid.lastY = (double)lastRow;
    uint32_t at = stagger->origin == mtwn_staggerLast ? count - 1u : stagger->index;
    switch (stagger->origin)
    {
    case mtwn_staggerLast:
    case mtwn_staggerIndex:
    {
        uint32_t row = at / grid.columns;
        grid.originX = (double)(at % grid.columns);
        grid.originY = (double)row;
        break;
    }
    case mtwn_staggerCenter:
        grid.originX = grid.lastX / 2.0;
        grid.originY = grid.lastY / 2.0;
        break;
    case mtwn_staggerPoint:
        grid.originX = stagger->pointX * grid.lastX;
        grid.originY = stagger->pointY * grid.lastY;
        break;
    default:
        break;
    }
    return grid;
}

static double Least(double a, double b)
{
    return a < b ? a : b;
}

// An item's distance to the nearest edge.
static double EdgeDistance(const Grid* grid, mtwnStaggerAxis axis, double x, double y)
{
    double toX = Least(x, grid->lastX - x);
    double toY = Least(y, grid->lastY - y);
    if (axis == mtwn_staggerX || (axis == mtwn_staggerBoth && grid->lastY == 0.0))
    {
        return toX;
    }
    if (axis == mtwn_staggerY || grid->lastX == 0.0)
    {
        return toY;
    }
    return Least(toX, toY);
}

static double Distance(const Grid* grid, const mtwnStagger* stagger, uint32_t item)
{
    uint32_t row = item / grid->columns;
    double x = (double)(item % grid->columns);
    double y = (double)row;
    if (stagger->origin == mtwn_staggerEdges)
    {
        return EdgeDistance(grid, stagger->axis, x, y);
    }
    double dx = x - grid->originX;
    double dy = y - grid->originY;
    if (stagger->axis == mtwn_staggerX)
    {
        return fabs(dx);
    }
    if (stagger->axis == mtwn_staggerY)
    {
        return fabs(dy);
    }
    return sqrt(dx * dx + dy * dy);
}

// The next 32 random bits of a seed's stream (SplitMix64's top half).
static uint32_t NextBits(uint64_t* state)
{
    *state += 0x9E3779B97F4A7C15ull;
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return (uint32_t)(z >> 32);
}

uint32_t mtwnUniformBelow(uint64_t* state, uint32_t range)
{
    // Lemire's multiply, the few low products drawn again so that every
    // place has the same share of the 2^32 draws.
    uint64_t product = (uint64_t)NextBits(state) * range;
    uint32_t low = (uint32_t)product;
    if (low < range)
    {
        uint32_t threshold = (uint32_t)(0u - range) % range;
        while (low < threshold)
        {
            product = (uint64_t)NextBits(state) * range;
            low = (uint32_t)product;
        }
    }
    return (uint32_t)(product >> 32);
}

// A permutation of 0 ... count - 1 from the seed, every one equally likely.
static void Shuffle(uint64_t seed, uint32_t count, double* out)
{
    uint64_t state = seed;
    for (uint32_t j = 0; j < count; ++j)
    {
        uint32_t k = mtwnUniformBelow(&state, j + 1u);
        if (k != j)
        {
            out[j] = out[k];
        }
        out[k] = (double)j;
    }
}

// The distances' least and range, nothing written; a random order's are
// 0 and count - 1.
static void Spread(const mtwnStagger* stagger, uint32_t count, double* leastOut, double* rangeOut)
{
    if (stagger->origin == mtwn_staggerRandom)
    {
        *leastOut = 0.0;
        *rangeOut = (double)(count - 1u);
        return;
    }
    Grid grid = MakeGrid(stagger, count);
    double least = Distance(&grid, stagger, 0);
    double most = least;
    for (uint32_t i = 1; i < count; ++i)
    {
        double d = Distance(&grid, stagger, i);
        least = Least(least, d);
        most = d > most ? d : most;
    }
    *leastOut = least;
    *rangeOut = most - least;
}

static MTWN_NOINLINE mtwnResult mtwnComputeStaggerWork(const mtwnStagger* stagger, uint32_t count,
                                                       double* offsetsOut)
{
    if (!IsValid(stagger, count, offsetsOut))
    {
        return mtwn_errorInvalid;
    }
    if (count == 0)
    {
        return mtwn_success;
    }
    double least = 0.0;
    double range = 0.0;
    Spread(stagger, count, &least, &range);
    double span =
        stagger->spacingKind == mtwn_staggerAmount ? stagger->spacing : stagger->spacing * range;
    if (!isfinite(span))
    {
        return mtwn_errorInvalid;
    }
    if (stagger->origin == mtwn_staggerRandom)
    {
        Shuffle(stagger->seed, count, offsetsOut);
    }
    else
    {
        Grid grid = MakeGrid(stagger, count);
        for (uint32_t i = 0; i < count; ++i)
        {
            offsetsOut[i] = Distance(&grid, stagger, i);
        }
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        double t = range > 0.0 ? (offsetsOut[i] - least) / range : 0.0;
        offsetsOut[i] = stagger->start + span * mtwnSampleEasingWork(&stagger->easing, t, false);
    }
    return mtwn_success;
}

mtwnResult mtwnComputeStagger(const mtwnStagger* stagger, uint32_t count, double* offsetsOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnComputeStaggerWork(stagger, count, offsetsOut);
    mtwnFloatEnvLeave(env);
    return result;
}
