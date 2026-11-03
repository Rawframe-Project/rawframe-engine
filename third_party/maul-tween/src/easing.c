// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The easings. Each kind has one
// native curve: Penner's in curve for most, the out curve for elastic
// and bounce, which GSAP and Penner define by it, and CSS's curves from
// src/css_easing.c. A curve not exact at
// both ends is normalized by its values there, computed when it is made
// by the same code that samples it, so (g(p) - g(0)) / (g(1) - g(0)) is
// exactly 0 and 1 at the ends. The modes are reflections of the native
// curve, taken so that each half lands where the other starts.

#include "maul-tween/easing.h"

#include "css_easing.h"
#include "easing_internal.h"
#include "elementary.h"
#include "fpenv.h"
#include "spring_easing.h"
#include "valid.h"
#include "work.h"

#include "generated/math_constants.h"

#include <float.h>
#include <math.h>

// Where the normalization's numbers sit in parameters[].
enum
{
    s_start = MTWN_EASING_START,
    s_range = MTWN_EASING_RANGE,
};

// The largest exponent a power computes by multiplication.
static const uint32_t s_largestIntegerExponent = 8;

// The normalizations of the curves without parameters whose raw ends are
// not 0 and 1, as Normalize computes them with the library's own
// functions, so the same everywhere: sine's 1 - cos(pi / 2) at 1, expo's
// 2^-10 at 0.
static const double s_sineRange = 0x1.fffffffffffffp-1;
static const double s_expoStart = 0x1.0p-10;
static const double s_expoRange = 0x1.ff8p-1;

// The elastic's largest amplitude, and the angular frequency of its
// shortest period, 2^-10, once an amplitude below 1 has stretched it (2 pi
// / P, as its maker computes it). The curve ends at 1 + a 2^-10 sin(...), which
// it is normalized by: up to an amplitude of 512 that stays from 1/2 to
// 3/2. Below the period, an oscillation is finer than MTWN_MAX_FIT_POINTS
// points can follow: at 2^-10, a fit of that many has 64 points to each,
// which trace a sine to about 0.1% of its height.
static const double s_largestAmplitude = 512.0;
static const double s_largestAngular = 4.0 * MTWN_PIO2_HI * 0x1.0p10;

static bool IsMode(mtwnEaseMode mode)
{
    return (uint32_t)mode <= mtwn_easeOutIn;
}

static double IntegerPower(double x, uint32_t n)
{
    return mtwnIntegerPower(x, n);
}

// Penner's bounce out, before normalization.
static double BounceOut(double q)
{
    if (q < 1.0 / 2.75)
    {
        return 7.5625 * q * q;
    }
    if (q < 2.0 / 2.75)
    {
        q -= 1.5 / 2.75;
        return 7.5625 * q * q + 0.75;
    }
    if (q < 2.5 / 2.75)
    {
        q -= 2.25 / 2.75;
        return 7.5625 * q * q + 0.9375;
    }
    q -= 2.625 / 2.75;
    return 7.5625 * q * q + 0.984375;
}

// Circ's 1 - x^2 and 2x - x^2 = 1 - (1 - x)^2 on [0, 1], never rising and
// never falling to the bit, as a timeline's easing must, each step one
// rounding of a value moving one way, and with their digits kept. 1 - x^2:
// below 1/2, x x rises, and 1 - (x x) falls; from 1/2, 1 - x is exact and
// falls by 2^-53 a step, while 1 + x, rounded, rises by at most 2^-52, so
// (1 - x)(1 + x) changes by at most 2^-53 (1 - 3x) < 0 before its
// rounding. 2x - x^2: below 1/2, 2x rises by 2 ulp(x) a step and x x,
// rounded, by less than ulp(x) + ulp(x^2) <= 2 ulp(x); from 1/2, 1 - x is
// exact and falls, and so does its square, rounded. At 1/2 both forms give
// 0.75 exactly, and below it 1 - x x is at least 0.75, x x being at most
// 1/4, and 2x - x x at most 0.75, so the step between the forms keeps the
// way too.
static double CircSquare(double x)
{
    return x < 0.5 ? 1.0 - x * x : (1.0 - x) * (1.0 + x);
}

static double CircRise(double x)
{
    if (x < 0.5)
    {
        return 2.0 * x - x * x;
    }
    double rest = 1.0 - x;
    return 1.0 - rest * rest;
}

// The native curve of a Penner kind at x in [0, 1], before normalization.
static double RawCurve(const mtwnEasing* easing, double x)
{
    const double* parameter = easing->parameters;
    switch (easing->kind)
    {
    case mtwn_easingPower:
        return easing->count > 0 ? IntegerPower(x, easing->count) : mtwnPow(x, parameter[0]);
    case mtwn_easingSine:
    {
        double sine = 0.0;
        double cosine = 0.0;
        mtwnSinCos(x * MTWN_PIO2_HI, &sine, &cosine);
        return 1.0 - cosine;
    }
    case mtwn_easingExpo:
        return mtwnExp2(10.0 * (x - 1.0));
    case mtwn_easingCirc:
        return 1.0 - sqrt(CircSquare(x));
    case mtwn_easingBack:
        // (s + 1) x - s as x + s (x - 1): exactly 1 at x = 1 for any s,
        // where s + 1 rounds to s from 2^53 on, and with no cancellation
        // near the end.
        return x * x * (x + parameter[0] * (x - 1.0));
    case mtwn_easingElastic:
    {
        double sine = 0.0;
        double cosine = 0.0;
        mtwnSinCos((x - parameter[2]) * parameter[1], &sine, &cosine);
        return parameter[0] * mtwnExp2(-10.0 * x) * sine + 1.0;
    }
    default:
        return BounceOut(x);
    }
}

static bool IsNativeOut(mtwnEasingKind kind)
{
    return kind == mtwn_easingElastic || kind == mtwn_easingBounce;
}

static bool IsCss(mtwnEasingKind kind)
{
    return kind == mtwn_easingCubicBezier || kind == mtwn_easingSteps ||
           kind == mtwn_easingLinearPoints;
}

// A host function's value at x. A NaN breaks its contract, and is taken
// once, here, as x, the function linear there, which keeps every mode's
// reflection of it moving one way; hostNan, when given, is set to tell
// the context to count it as misuse.
static double HostCurve(const mtwnEasing* easing, double x, bool* hostNan)
{
    double value = easing->function(easing->context, x);
    if (isnan(value))
    {
        if (hostNan != NULL)
        {
            *hostNan = true;
        }
        return x;
    }
    return value;
}

// The native curve, Penner's normalized to land exactly on 0 and 1.
// before reaches the curve only where x is progress unreflected.
static double NativeCurve(const mtwnEasing* easing, double x, bool before, bool* hostNan)
{
    switch (easing->kind)
    {
    case mtwn_easingHost:
        return HostCurve(easing, x, hostNan);
    case mtwn_easingCubicBezier:
        return mtwnSampleCubicBezier(easing->parameters, x);
    case mtwn_easingSteps:
        return mtwnSampleSteps(easing->count, easing->position, x, before);
    case mtwn_easingLinearPoints:
        return mtwnSampleLinearPoints(easing->points, easing->count, x);
    case mtwn_easingSpring:
        return mtwnSampleSpringEasing(easing, x);
    default:
    {
        // A curve whose raw ends are already 0 and 1 (the integer powers)
        // skips the division, which would give the same bits.
        double raw = RawCurve(easing, x);
        double start = easing->parameters[s_start];
        double range = easing->parameters[s_range];
        return start == 0.0 && range == 1.0 ? raw : (raw - start) / range;
    }
    }
}

// The in curve f(x) and the out curve 1 - f(1 - x), one of which is
// native; circ has both.
static double InCurve(const mtwnEasing* easing, double x, bool before, bool* hostNan)
{
    return IsNativeOut(easing->kind) ? 1.0 - NativeCurve(easing, 1.0 - x, false, hostNan)
                                     : NativeCurve(easing, x, before, hostNan);
}

static double OutCurve(const mtwnEasing* easing, double x, bool* hostNan)
{
    if (easing->kind == mtwn_easingCirc)
    {
        // sqrt(1 - (1 - x)^2), its digits kept where the curve is steepest.
        return sqrt(CircRise(x));
    }
    return IsNativeOut(easing->kind) ? NativeCurve(easing, x, false, hostNan)
                                     : 1.0 - NativeCurve(easing, 1.0 - x, false, hostNan);
}

// Fills the normalization from the native curve's raw ends.
static void Normalize(mtwnEasing* easing)
{
    double start = RawCurve(easing, 0.0);
    easing->parameters[s_start] = start;
    easing->parameters[s_range] = RawCurve(easing, 1.0) - start;
}

// Whether a curve's normalization is the one given.
static bool IsNormalized(const mtwnEasing* easing, double start, double range)
{
    return easing->parameters[s_start] == start && easing->parameters[s_range] == range;
}

// An elastic's fields in the ranges its maker gives them: then its phase
// in radians, asin(1 / a) at most pi / 2, and its sine's angle are finite,
// its raw curve within a of 1, and less its start over its range at most
// 2056 in magnitude.
static bool ValidElastic(const mtwnEasing* easing)
{
    const double* parameter = easing->parameters;
    double range = parameter[s_range];
    return parameter[0] >= 1.0 && parameter[0] <= s_largestAmplitude && parameter[1] > 0.0 &&
           parameter[1] <= s_largestAngular && parameter[2] >= 0.0 &&
           parameter[2] * parameter[1] <= 2.0 && fabs(parameter[s_start]) <= 1.0 && range >= 0.25 &&
           range <= 2.0;
}

// Whether a Penner kind's fields are its maker's: those it computes
// exactly by their values, the elastic's by their ranges.
static bool ValidPenner(const mtwnEasing* easing)
{
    double first = easing->parameters[0];
    switch (easing->kind)
    {
    case mtwn_easingPower:
        // A count is the integer exponent multiplied out.
        return first > 0.0 && first <= DBL_MAX && easing->count <= s_largestIntegerExponent &&
               (easing->count == 0 || (double)easing->count == first) &&
               IsNormalized(easing, 0.0, 1.0);
    case mtwn_easingSine:
        return IsNormalized(easing, 0.0, s_sineRange);
    case mtwn_easingExpo:
        return IsNormalized(easing, s_expoStart, s_expoRange);
    case mtwn_easingBack:
        return first >= 0.0 && first <= DBL_MAX && IsNormalized(easing, 0.0, 1.0);
    case mtwn_easingElastic:
        return ValidElastic(easing);
    default:
        // Circ and bounce, whose raw ends are 0 and 1.
        return IsNormalized(easing, 0.0, 1.0);
    }
}

MTWN_NOINLINE bool mtwnValidEasing(const mtwnEasing* easing)
{
    if (easing == NULL || !IsMode(easing->mode))
    {
        return false;
    }
    const double* parameter = easing->parameters;
    switch (easing->kind)
    {
    case mtwn_easingLinear:
        return true;
    case mtwn_easingHost:
        return easing->function != NULL;
    case mtwn_easingCubicBezier:
        return mtwnValidCubicBezier(parameter[0], parameter[1], parameter[2], parameter[3]);
    case mtwn_easingSteps:
        return mtwnValidSteps(easing->count, easing->position);
    case mtwn_easingLinearPoints:
        return easing->points != NULL && easing->count >= 2;
    case mtwn_easingSpring:
        return mtwnValidSpringEasing(easing);
    default:
        return (uint32_t)easing->kind <= mtwn_easingBounce && ValidPenner(easing);
    }
}

static mtwnEasing Blank(mtwnEasingKind kind, mtwnEaseMode mode)
{
    mtwnEasing easing = {0};
    easing.kind = kind;
    easing.mode = mode;
    return easing;
}

mtwnEasing mtwnMakeLinearEasing(void)
{
    return Blank(mtwn_easingLinear, mtwn_easeIn);
}

static MTWN_NOINLINE mtwnResult mtwnMakePowerEasingWork(mtwnEaseMode mode, double exponent,
                                                        mtwnEasing* easingOut)
{
    if (easingOut == NULL || !IsMode(mode) || !isfinite(exponent) || exponent <= 0.0)
    {
        return mtwn_errorInvalid;
    }
    mtwnEasing easing = Blank(mtwn_easingPower, mode);
    easing.parameters[0] = exponent;
    if (exponent == floor(exponent) && exponent <= (double)s_largestIntegerExponent)
    {
        easing.count = (uint32_t)exponent;
    }
    Normalize(&easing);
    *easingOut = easing;
    return mtwn_success;
}

mtwnResult mtwnMakePowerEasing(mtwnEaseMode mode, double exponent, mtwnEasing* easingOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnMakePowerEasingWork(mode, exponent, easingOut);
    mtwnFloatEnvLeave(env);
    return result;
}

static MTWN_NOINLINE mtwnResult mtwnMakePennerEasingWork(mtwnEasingKind kind, mtwnEaseMode mode,
                                                         mtwnEasing* easingOut)
{
    bool known = kind == mtwn_easingSine || kind == mtwn_easingExpo || kind == mtwn_easingCirc ||
                 kind == mtwn_easingBounce;
    if (easingOut == NULL || !known || !IsMode(mode))
    {
        return mtwn_errorInvalid;
    }
    mtwnEasing easing = Blank(kind, mode);
    Normalize(&easing);
    *easingOut = easing;
    return mtwn_success;
}

mtwnResult mtwnMakePennerEasing(mtwnEasingKind kind, mtwnEaseMode mode, mtwnEasing* easingOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnMakePennerEasingWork(kind, mode, easingOut);
    mtwnFloatEnvLeave(env);
    return result;
}

static MTWN_NOINLINE mtwnResult mtwnMakeBackEasingWork(mtwnEaseMode mode, double overshoot,
                                                       mtwnEasing* easingOut)
{
    if (easingOut == NULL || !IsMode(mode) || !isfinite(overshoot) || overshoot < 0.0)
    {
        return mtwn_errorInvalid;
    }
    mtwnEasing easing = Blank(mtwn_easingBack, mode);
    easing.parameters[0] = overshoot;
    Normalize(&easing);
    *easingOut = easing;
    return mtwn_success;
}

mtwnResult mtwnMakeBackEasing(mtwnEaseMode mode, double overshoot, mtwnEasing* easingOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnMakeBackEasingWork(mode, overshoot, easingOut);
    mtwnFloatEnvLeave(env);
    return result;
}

static MTWN_NOINLINE mtwnResult mtwnMakeElasticEasingWork(mtwnEaseMode mode, double amplitude,
                                                          double period, mtwnEasing* easingOut)
{
    if (easingOut == NULL || !IsMode(mode))
    {
        return mtwn_errorInvalid;
    }
    // GSAP's _configElastic: an amplitude below 1 stretches the period.
    if (amplitude < 1.0)
    {
        period /= amplitude;
        amplitude = 1.0;
    }
    double angular = 4.0 * MTWN_PIO2_HI / period;
    mtwnEasing easing = Blank(mtwn_easingElastic, mode);
    easing.parameters[0] = amplitude;
    easing.parameters[1] = angular;
    // asin(1 / a) as atan2(1, sqrt(a^2 - 1)), exactly pi / 2 at a = 1.
    easing.parameters[2] = mtwnAtan2(1.0, sqrt((amplitude - 1.0) * (amplitude + 1.0))) / angular;
    // The checks of every easing taken in refuse the rest: an amplitude
    // or a period not above 0 or NaN leaves no amplitude of at least 1 or
    // no frequency above 0, and they refuse an amplitude or a period past
    // their ranges, a period so long, given or stretched, that its
    // frequency vanishes, and one whose phase overflows.
    Normalize(&easing);
    if (!ValidElastic(&easing))
    {
        return mtwn_errorInvalid;
    }
    *easingOut = easing;
    return mtwn_success;
}

mtwnResult mtwnMakeElasticEasing(mtwnEaseMode mode, double amplitude, double period,
                                 mtwnEasing* easingOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnMakeElasticEasingWork(mode, amplitude, period, easingOut);
    mtwnFloatEnvLeave(env);
    return result;
}

mtwnResult mtwnMakeHostEasing(mtwnEaseMode mode, mtwnEaseFunction* function, void* context,
                              mtwnEasing* easingOut)
{
    if (easingOut == NULL || function == NULL || !IsMode(mode))
    {
        return mtwn_errorInvalid;
    }
    mtwnEasing easing = Blank(mtwn_easingHost, mode);
    easing.function = function;
    easing.context = context;
    *easingOut = easing;
    return mtwn_success;
}

static MTWN_NOINLINE mtwnResult mtwnMakeCubicBezierEasingWork(double x1, double y1, double x2,
                                                              double y2, mtwnEasing* easingOut)
{
    if (easingOut == NULL || !mtwnValidCubicBezier(x1, y1, x2, y2))
    {
        return mtwn_errorInvalid;
    }
    mtwnEasing easing = Blank(mtwn_easingCubicBezier, mtwn_easeIn);
    easing.parameters[0] = x1;
    easing.parameters[1] = y1;
    easing.parameters[2] = x2;
    easing.parameters[3] = y2;
    *easingOut = easing;
    return mtwn_success;
}

mtwnResult mtwnMakeCubicBezierEasing(double x1, double y1, double x2, double y2,
                                     mtwnEasing* easingOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnMakeCubicBezierEasingWork(x1, y1, x2, y2, easingOut);
    mtwnFloatEnvLeave(env);
    return result;
}

mtwnResult mtwnMakeStepsEasing(uint32_t count, mtwnStepPosition position, mtwnEasing* easingOut)
{
    if (easingOut == NULL || !mtwnValidSteps(count, position))
    {
        return mtwn_errorInvalid;
    }
    mtwnEasing easing = Blank(mtwn_easingSteps, mtwn_easeIn);
    easing.count = count;
    easing.position = position;
    *easingOut = easing;
    return mtwn_success;
}

static MTWN_NOINLINE mtwnResult mtwnMakeCssEasingWork(mtwnCssKeyword keyword, mtwnEasing* easingOut)
{
    // The keywords' curves, CSS Easing Level 2 section 2.
    static const double s_beziers[4][4] = {
        {0.25, 0.1, 0.25, 1.0},
        {0.42, 0.0, 1.0, 1.0},
        {0.0, 0.0, 0.58, 1.0},
        {0.42, 0.0, 0.58, 1.0},
    };
    if (easingOut == NULL || (uint32_t)keyword > mtwn_cssStepEnd)
    {
        return mtwn_errorInvalid;
    }
    if (keyword == mtwn_cssLinear)
    {
        *easingOut = mtwnMakeLinearEasing();
        return mtwn_success;
    }
    if (keyword == mtwn_cssStepStart || keyword == mtwn_cssStepEnd)
    {
        return mtwnMakeStepsEasing(1, keyword == mtwn_cssStepStart ? mtwn_jumpStart : mtwn_jumpEnd,
                                   easingOut);
    }
    const double* c = s_beziers[keyword - mtwn_cssEase];
    return mtwnMakeCubicBezierEasing(c[0], c[1], c[2], c[3], easingOut);
}

mtwnResult mtwnMakeCssEasing(mtwnCssKeyword keyword, mtwnEasing* easingOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnMakeCssEasingWork(keyword, easingOut);
    mtwnFloatEnvLeave(env);
    return result;
}

MTWN_NOINLINE mtwnResult mtwnMakeLinearPointsEasingWork(mtwnLinearPoint* points, uint32_t count,
                                                        mtwnEasing* easingOut)
{
    if (easingOut == NULL || !mtwnCanonicalizeLinearPoints(points, count))
    {
        return mtwn_errorInvalid;
    }
    mtwnEasing easing = Blank(mtwn_easingLinearPoints, mtwn_easeIn);
    easing.points = points;
    easing.count = count;
    *easingOut = easing;
    return mtwn_success;
}

mtwnResult mtwnMakeLinearPointsEasing(mtwnLinearPoint* points, uint32_t count,
                                      mtwnEasing* easingOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnMakeLinearPointsEasingWork(points, count, easingOut);
    mtwnFloatEnvLeave(env);
    return result;
}

static MTWN_NOINLINE mtwnResult mtwnSetEaseModeWork(mtwnEasing* easing, mtwnEaseMode mode)
{
    if (!mtwnValidEasing(easing) || !IsMode(mode))
    {
        return mtwn_errorInvalid;
    }
    easing->mode = mode;
    return mtwn_success;
}

mtwnResult mtwnSetEaseMode(mtwnEasing* easing, mtwnEaseMode mode)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnSetEaseModeWork(easing, mode);
    mtwnFloatEnvLeave(env);
    return result;
}

// Whether an easing's curve, in mode in, never decreases.
static bool InCurveMonotonic(const mtwnEasing* easing)
{
    switch (easing->kind)
    {
    case mtwn_easingLinear:
    case mtwn_easingPower:
    case mtwn_easingSine:
    case mtwn_easingExpo:
    case mtwn_easingCirc:
    case mtwn_easingSteps:
        return true;
    case mtwn_easingCubicBezier:
    {
        // With both y in [0, 1] the y polynomial never decreases in t, as x
        // does, nor do the end tangents.
        double y1 = easing->parameters[1];
        double y2 = easing->parameters[3];
        return y1 >= 0.0 && y1 <= 1.0 && y2 >= 0.0 && y2 <= 1.0;
    }
    case mtwn_easingLinearPoints:
        for (uint32_t i = 1; i < easing->count; ++i)
        {
            if (easing->points[i].output < easing->points[i - 1].output)
            {
                return false;
            }
        }
        return true;
    default:
        return false;
    }
}

MTWN_NOINLINE bool mtwnIsEasingMonotonicWork(const mtwnEasing* easing)
{
    if (!InCurveMonotonic(easing))
    {
        return false;
    }
    // Out reflects the curve, so it never decreases either. In-out and
    // out-in join its halves at 1/2: the first ends at most at 1/2 exactly
    // when the second starts at 1/2 or above, f(1) / 2 and 1 - f(1) / 2 in
    // in-out, (1 - f(0)) / 2 and (1 + f(0)) / 2 in out-in, the first's
    // limit at most that as the curve never decreases. So they meet going
    // up when the easing at 1/2 is 1/2 or above, as every kind but
    // linear() always is; a jump at the end that decides makes this
    // strict.
    return easing->mode < mtwn_easeInOut || mtwnSampleEasingNoted(easing, 0.5, false, NULL) >= 0.5;
}

static MTWN_NOINLINE mtwnResult mtwnIsEasingMonotonicChecked(const mtwnEasing* easing,
                                                             bool* monotonicOut)
{
    if (!mtwnValidEasing(easing) || monotonicOut == NULL)
    {
        return mtwn_errorInvalid;
    }
    *monotonicOut = mtwnIsEasingMonotonicWork(easing);
    return mtwn_success;
}

mtwnResult mtwnIsEasingMonotonic(const mtwnEasing* easing, bool* monotonicOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnIsEasingMonotonicChecked(easing, monotonicOut);
    mtwnFloatEnvLeave(env);
    return result;
}

MTWN_NOINLINE double mtwnSampleEasingNoted(const mtwnEasing* easing, double progress, bool before,
                                           bool* hostNan)
{
    if (easing->kind == mtwn_easingLinear)
    {
        return progress;
    }
    if (mtwnIsNormalPower(easing))
    {
        return mtwnEaseNormalPower(easing, progress);
    }
    double p = progress;
    if (!IsCss(easing->kind))
    {
        p = p < 0.0 ? 0.0 : (p > 1.0 ? 1.0 : p);
    }
    switch (easing->mode)
    {
    case mtwn_easeIn:
        return InCurve(easing, p, before, hostNan);
    case mtwn_easeOut:
        return OutCurve(easing, p, hostNan);
    case mtwn_easeInOut:
        return p < 0.5 ? 0.5 * InCurve(easing, 2.0 * p, before, hostNan)
                       : 0.5 + 0.5 * OutCurve(easing, 2.0 * p - 1.0, hostNan);
    default:
        return p < 0.5 ? 0.5 * OutCurve(easing, 2.0 * p, hostNan)
                       : 0.5 + 0.5 * InCurve(easing, 2.0 * p - 1.0, before, hostNan);
    }
}

static MTWN_NOINLINE mtwnResult mtwnSampleEasingChecked(const mtwnEasing* easing, double progress,
                                                        bool before, double* easedOut)
{
    if (!mtwnValidEasing(easing) || !isfinite(progress) || easedOut == NULL)
    {
        return mtwn_errorInvalid;
    }
    *easedOut = mtwnSampleEasingWork(easing, progress, before);
    return mtwn_success;
}

mtwnResult mtwnSampleEasing(const mtwnEasing* easing, double progress, bool before,
                            double* easedOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnSampleEasingChecked(easing, progress, before, easedOut);
    mtwnFloatEnvLeave(env);
    return result;
}
