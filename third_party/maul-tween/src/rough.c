// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Rough easings: linear() points wandering from a
// shape by SplitMix64 of a seed, as shake draws its gradients.
// The inner points are stratified, one to each of m equal
// ranges of input, so they come in order without a sort and never meet
// in a jump; the offsets follow GSAP's tapers.

#include "fpenv.h"
#include "valid.h"
#include "work.h"

#include "maul-tween/easing.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>

static const uint8_t s_roughFlags = mtwn_roughClamp | mtwn_roughRising | mtwn_roughZigZag;

// A uniform draw in (0, 1) from the next 53 bits of a seed's stream.
static double Draw(uint64_t* state)
{
    *state += 0x9E3779B97F4A7C15ull;
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return ((double)(z >> 11) + 0.5) * 0x1.0p-53;
}

// The taper's share of the strength at an input.
static double Taper(mtwnRoughTaper taper, double x)
{
    const double shares[4] = {1.0, x, 1.0 - x, 1.0 - fabs(2.0 * x - 1.0)};
    return shares[taper] * shares[taper];
}

// The k-th inner point of m before its bounds, from its two draws; kept
// out of the loop so the compiler's copies of it for each flag stay small.
static MTWN_NOINLINE mtwnLinearPoint Point(const mtwnRoughDef* def, uint32_t k, double m, double u,
                                           double v)
{
    bool zigZag = (def->flags & mtwn_roughZigZag) != 0;
    double x = ((double)k + (zigZag ? 0.5 : u)) / m;
    double side = zigZag ? (double)((k & 1u) == 0) : v;
    double y = mtwnSampleEasingWork(&def->shape, x, false) +
               def->strength * Taper(def->taper, x) * (2.0 * side - 1.0);
    return (mtwnLinearPoint){x, y};
}

static MTWN_NOINLINE mtwnResult mtwnMakeRoughEasingWork(const mtwnRoughDef* def,
                                                        mtwnLinearPoint* points, uint32_t count,
                                                        mtwnEasing* easingOut)
{
    if (def == NULL || !mtwnValidEasing(&def->shape) || points == NULL || easingOut == NULL ||
        count < 2 || !(def->strength >= 0.0 && isfinite(def->strength)) ||
        def->taper > mtwn_roughTaperBoth || (def->flags & ~s_roughFlags) != 0)
    {
        return mtwn_errorInvalid;
    }
    bool rising = (def->flags & mtwn_roughRising) != 0;
    double low = rising || (def->flags & mtwn_roughClamp) != 0 ? 0.0 : -HUGE_VAL;
    double high = low == 0.0 ? 1.0 : HUGE_VAL;
    // Rising, the bound follows each output; otherwise it stays. A
    // comparison against an infinity, not a test of the flag, so the
    // compiler keeps one loop.
    double follow = rising ? -HUGE_VAL : HUGE_VAL;
    uint64_t state = def->seed;
    for (uint32_t k = 0; k + 2 < count; ++k)
    {
        // Both draws are taken whatever the flags, so a zig-zag's seed
        // is unused but its stream the same.
        double u = Draw(&state);
        mtwnLinearPoint point = Point(def, k, (double)(count - 2), u, Draw(&state));
        if (!isfinite(point.output))
        {
            return mtwn_errorInvalid;
        }
        point.output = point.output < low ? low : (point.output > high ? high : point.output);
        low = point.output < follow ? low : point.output;
        points[k + 1] = point;
    }
    points[0] = (mtwnLinearPoint){0.0, 0.0};
    points[count - 1] = (mtwnLinearPoint){1.0, 1.0};
    return mtwnMakeLinearPointsEasingWork(points, count, easingOut);
}

mtwnResult mtwnMakeRoughEasing(const mtwnRoughDef* def, mtwnLinearPoint* points, uint32_t count,
                               mtwnEasing* easingOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnMakeRoughEasingWork(def, points, count, easingOut);
    mtwnFloatEnvLeave(env);
    return result;
}
