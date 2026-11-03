// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Springs in one closed form. With
// decay rate a = zeta w0 and damped frequency w = w0 sqrt(|1 - zeta^2|),
// a start (x0, v0) moves as x = e^(-a t) (x0 C + (v0 + a x0) S) and
// v = e^(-a t) (v0 C - (a v0 + w0^2 x0) S), where C and S are cos(w t)
// and sin(w t) / w below critical damping, cosh(w t) and sinh(w t) / w
// from it, where w is 0 at critical damping and S is t. sin(w t) / w
// keeps its digits however small w is, as the sine does; sinh(w t) / w
// is t times a series below w t = 1/2, so the regimes meet without
// cancelling, and above it an overdamped spring is written in its two
// rates, the slow one as w0^2 / (a + w), so nothing cancels or
// overflows however heavy the damping.

#include "maul-tween/spring.h"

#include "elementary.h"
#include "fpenv.h"
#include "invariant.h"
#include "spring_easing.h"
#include "valid.h"
#include "work.h"

#include "generated/math_constants.h"

#include <float.h>
#include <math.h>
#include <string.h>

enum
{
    s_frequency = 0,
    s_ratio = 1,
    s_decay = 2,
    s_damped = 3,
    s_squared = 4,
};

enum
{
    s_underdamped = 0,
    // Critical damping and above.
    s_overdamped = 1,
};

// Where the series of sinh(y) / y takes over.
static const double s_seriesBelow = 0.5;
// Newton's steps for a settling time, and when one is small enough.
static const int s_newtonSteps = 64;
static const double s_newtonStop = 0x1.0p-50;
// The unit roundoff, half an ulp of 1.
static const double s_unit = 0x1.0p-53;
// Rate times time past which a spring's exponentials are 0, mtwnExp being
// 0 below ln(2^-1075) = -745.13 even with the product rounded, so that its
// sample is exactly 0 from then on.
static const double s_zeroRates = 747.0;
// The rounds a settling time's margins may take to cover the time they give.
static const int s_marginRounds = 4;

// sinh(y) / y for 0 <= y < 1/2: the series to y^12, whose next term is
// under 5e-17.
static double SinhcSeries(double y)
{
    double z = y * y;
    double sum = 1.0 / 6227020800.0;
    sum = sum * z + 1.0 / 39916800.0;
    sum = sum * z + 1.0 / 362880.0;
    sum = sum * z + 1.0 / 5040.0;
    sum = sum * z + 1.0 / 120.0;
    sum = sum * z + 1.0 / 6.0;
    return 1.0 + z * sum;
}

static mtwnResult Finish(double frequency, double ratio, mtwnSpring* springOut)
{
    if (springOut == NULL || !isfinite(frequency) || frequency <= 0.0 || !isfinite(ratio) ||
        ratio < 0.0)
    {
        return mtwn_errorInvalid;
    }
    double decay = ratio * frequency;
    double damped = frequency * sqrt(fabs((1.0 - ratio) * (1.0 + ratio)));
    double squared = frequency * frequency;
    if (!isfinite(decay + damped) || !isfinite(squared) || squared == 0.0)
    {
        return mtwn_errorInvalid;
    }
    mtwnSpring spring = {{0.0}, 0};
    spring.parameters[s_frequency] = frequency;
    spring.parameters[s_ratio] = ratio;
    spring.parameters[s_decay] = decay;
    spring.parameters[s_damped] = damped;
    spring.parameters[s_squared] = squared;
    spring.regime = ratio < 1.0 ? s_underdamped : s_overdamped;
    *springOut = spring;
    return mtwn_success;
}

static MTWN_NOINLINE mtwnResult mtwnMakeSpringWork(double stiffness, double damping, double mass,
                                                   mtwnSpring* springOut)
{
    if (!isfinite(stiffness) || stiffness <= 0.0 || !isfinite(damping) || damping < 0.0 ||
        !isfinite(mass) || mass <= 0.0)
    {
        return mtwn_errorInvalid;
    }
    // sqrt(k m) as a product of roots, which cannot overflow where k m does.
    double frequency = sqrt(stiffness / mass);
    double ratio = damping / (2.0 * sqrt(stiffness) * sqrt(mass));
    return Finish(frequency, ratio, springOut);
}

mtwnResult mtwnMakeSpring(double stiffness, double damping, double mass, mtwnSpring* springOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnMakeSpringWork(stiffness, damping, mass, springOut);
    mtwnFloatEnvLeave(env);
    return result;
}

static MTWN_NOINLINE mtwnResult mtwnMakeSpringFromFrequencyWork(double hertz, double dampingRatio,
                                                                mtwnSpring* springOut)
{
    if (!isfinite(hertz) || hertz <= 0.0)
    {
        return mtwn_errorInvalid;
    }
    return Finish(4.0 * MTWN_PIO2_HI * hertz, dampingRatio, springOut);
}

mtwnResult mtwnMakeSpringFromFrequency(double hertz, double dampingRatio, mtwnSpring* springOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnMakeSpringFromFrequencyWork(hertz, dampingRatio, springOut);
    mtwnFloatEnvLeave(env);
    return result;
}

static MTWN_NOINLINE mtwnResult mtwnMakeSpringFromDurationWork(double duration, double bounce,
                                                               mtwnSpring* springOut)
{
    if (!isfinite(duration) || duration <= 0.0 || !(bounce > -1.0 && bounce <= 1.0))
    {
        return mtwn_errorInvalid;
    }
    double ratio = bounce >= 0.0 ? 1.0 - bounce : 1.0 / (1.0 + bounce);
    return Finish(4.0 * MTWN_PIO2_HI / duration, ratio, springOut);
}

mtwnResult mtwnMakeSpringFromDuration(double duration, double bounce, mtwnSpring* springOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnMakeSpringFromDurationWork(duration, bounce, springOut);
    mtwnFloatEnvLeave(env);
    return result;
}

// The motion from e^(-a t), C and S.
static mtwnSpringMotion FromTerms(const mtwnSpring* spring, double envelope, double c, double s)
{
    double decay = spring->parameters[s_decay];
    double scaledSine = envelope * s;
    mtwnSpringMotion motion;
    motion.offsetFromOffset = envelope * c + decay * scaledSine;
    motion.offsetFromVelocity = scaledSine;
    motion.velocityFromOffset = -spring->parameters[s_squared] * scaledSine;
    motion.velocityFromVelocity = envelope * c - decay * scaledSine;
    return motion;
}

// The overdamped motion in its two rates, for w t >= 1/2.
static mtwnSpringMotion Overdamped(const mtwnSpring* spring, double seconds)
{
    double decay = spring->parameters[s_decay];
    double damped = spring->parameters[s_damped];
    double fast = decay + damped;
    double slow = spring->parameters[s_squared] / fast;
    double p = mtwnExp(-slow * seconds);
    double q = mtwnExp(-fast * seconds);
    double twice = 2.0 * damped;
    mtwnSpringMotion motion;
    motion.offsetFromOffset = (fast * p - slow * q) / twice;
    motion.offsetFromVelocity = (p - q) / twice;
    motion.velocityFromOffset = -spring->parameters[s_squared] * ((p - q) / twice);
    motion.velocityFromVelocity = (fast * q - slow * p) / twice;
    return motion;
}

MTWN_NOINLINE mtwnSpringMotion mtwnComputeSpringMotionWork(const mtwnSpring* spring, double seconds)
{
    double decay = spring->parameters[s_decay];
    double damped = spring->parameters[s_damped];
    if (isinf(seconds))
    {
        double rest = decay > 0.0 ? 0.0 : (double)NAN;
        return (mtwnSpringMotion){rest, rest, rest, rest};
    }
    double envelope = mtwnExp(-decay * seconds);
    double y = damped * seconds;
    if (spring->regime == s_underdamped)
    {
        // The sine reduces any finite phase exactly, its result within
        // [-1, 1]; an infinite one holds no angle and is taken as none.
        y = isinf(y) ? 0.0 : y;
        double sine = 0.0;
        double cosine = 0.0;
        mtwnSinCos(y, &sine, &cosine);
        return FromTerms(spring, envelope, cosine, sine / damped);
    }
    if (y >= s_seriesBelow)
    {
        return Overdamped(spring, seconds);
    }
    return FromTerms(spring, envelope, 0.5 * (mtwnExp(y) + mtwnExp(-y)), seconds * SinhcSeries(y));
}

static MTWN_NOINLINE mtwnResult mtwnComputeSpringMotionChecked(const mtwnSpring* spring,
                                                               double seconds,
                                                               mtwnSpringMotion* motionOut)
{
    if (!mtwnValidSpring(spring) || !(seconds >= 0.0) || motionOut == NULL)
    {
        return mtwn_errorInvalid;
    }
    *motionOut = mtwnComputeSpringMotionWork(spring, seconds);
    return mtwn_success;
}

mtwnResult mtwnComputeSpringMotion(const mtwnSpring* spring, double seconds,
                                   mtwnSpringMotion* motionOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnComputeSpringMotionChecked(spring, seconds, motionOut);
    mtwnFloatEnvLeave(env);
    return result;
}

MTWN_NOINLINE void mtwnSampleSpringWork(const mtwnSpring* spring, double offset, double velocity,
                                        double seconds, double* offsetOut, double* velocityOut)
{
    mtwnSpringMotion motion = mtwnComputeSpringMotionWork(spring, seconds);
    if (offsetOut != NULL)
    {
        *offsetOut = motion.offsetFromOffset * offset + motion.offsetFromVelocity * velocity;
    }
    if (velocityOut != NULL)
    {
        *velocityOut = motion.velocityFromOffset * offset + motion.velocityFromVelocity * velocity;
    }
}

static MTWN_NOINLINE mtwnResult mtwnSampleSpringChecked(const mtwnSpring* spring, double offset,
                                                        double velocity, double seconds,
                                                        double* offsetOut, double* velocityOut)
{
    if (!mtwnValidSpring(spring) || !isfinite(offset) || !isfinite(velocity) || !(seconds >= 0.0))
    {
        return mtwn_errorInvalid;
    }
    mtwnSampleSpringWork(spring, offset, velocity, seconds, offsetOut, velocityOut);
    return mtwn_success;
}

mtwnResult mtwnSampleSpring(const mtwnSpring* spring, double offset, double velocity,
                            double seconds, double* offsetOut, double* velocityOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result =
        mtwnSampleSpringChecked(spring, offset, velocity, seconds, offsetOut, velocityOut);
    mtwnFloatEnvLeave(env);
    return result;
}

// The bound e^(-r t) (a + b min(t, cap)) on |x| for x = e^(-alpha t) (x0 C
// + B S), with a = |x0| and b = |B|, and its logarithm's distance above
// ln tolerance; logError bounds how far logTolerance, rounded, is from the
// logarithm it stands for, and certify asks for a time certified within.
typedef struct Bound
{
    double a;
    double b;
    double rate;
    double cap;
    double logTolerance;
    double logError;
    bool certify;
} Bound;

// The bound's logarithm above ln tolerance at a time t at most the cap.
static double Excess(const Bound* bound, double t)
{
    return mtwnLog(bound->a + bound->b * t) - bound->rate * t - bound->logTolerance;
}

// The time of the bound's largest value, where its logarithm's slope is 0.
// A rate below the normal doubles can overflow both 1 / rate and a / b,
// whose difference is then NaN: a rate a at least b is falling from 0,
// else rising past any time a double holds, so to the cap.
static double Peak(const Bound* bound)
{
    double peak = 0.0;
    if (bound->b > 0.0 && bound->rate > 0.0)
    {
        peak = 1.0 / bound->rate - bound->a / bound->b;
        if (isnan(peak))
        {
            peak = bound->a * bound->rate >= bound->b ? 0.0 : bound->cap;
        }
        peak = peak < 0.0 ? 0.0 : (peak > bound->cap ? bound->cap : peak);
    }
    else if (bound->b > 0.0)
    {
        peak = bound->cap;
    }
    return peak;
}

// The time after which the bound stays at most the tolerance. Its
// logarithm is concave, so it crosses the tolerance last where it is
// falling; Newton's first step from anywhere past the peak lands at or
// after that time, as the tangent lies above the curve, and its iterates
// then approach it from the right. Where the bound is nearly flat at the
// tolerance, rounding can still send a step to before the peak, even
// before 0; such a step goes halfway to the peak instead.
// A bound of 0 is within at once; one that never decays, past its cap,
// never.
static double BoundTime(const Bound* bound)
{
    double peak = Peak(bound);
    if (Excess(bound, peak) <= 0.0)
    {
        return 0.0;
    }
    if (isfinite(bound->cap))
    {
        // Past the cap the bound is a pure exponential. NaN, a bound that
        // overflowed at the cap against an infinite tolerance, is the
        // caller's to take, at a rate of 0 too.
        double flat =
            (mtwnLog(bound->a + bound->b * bound->cap) - bound->logTolerance) / bound->rate;
        if (!(flat < bound->cap))
        {
            return flat;
        }
    }
    // Twice the peak, where the slope is at most -rate / 4 for a peak of at
    // least 1 / (3 rate); below that, 1 / rate past it, where the slope is
    // at most -rate / 2 (twice a peak rounded to a tiny time sat on the
    // peak's slope of 0, a NaN step).
    double t = bound->cap;
    if (!isfinite(t))
    {
        t = peak >= 1.0 / (3.0 * bound->rate) ? 2.0 * peak : peak + 1.0 / bound->rate;
    }
    // A rate of 0 has a finite cap, past which the flat time is infinite
    // and returned above; an infinite cap is critical damping's, whose
    // rate is w0. NaN, a bound overflowing on the way, is the caller's to
    // take, and passes each check here.
    MTWN_ASSERT(bound->rate > 0.0 && !(t < peak));
    for (int i = 0; i < s_newtonSteps; ++i)
    {
        double slope = bound->b / (bound->a + bound->b * t) - bound->rate;
        double step = Excess(bound, t) / slope;
        if (t - step < peak)
        {
            // Halfway to the peak instead, at or after it however rounded:
            // the peak plus a share at least 0, where halving t and the peak
            // apart would round a subnormal half below it.
            double half = peak + 0.5 * (t - peak);
            step = t - half;
            t = half;
        }
        else
        {
            t -= step;
        }
        MTWN_ASSERT(!(t < peak));
        if (fabs(step) <= s_newtonStop * t)
        {
            break;
        }
    }
    return t;
}

// Whether a product of nonzero factors fell below the normal doubles,
// rounded by up to half the least subnormal or to 0. A sum of such
// products at least the least normal double is still within an ulp.
static bool Underflows(double factor, double other)
{
    return fabs(factor * other) < DBL_MIN && factor != 0.0 && other != 0.0;
}

// Whether a bound's time rests on its value a + b t below the normal
// doubles with b t rounded there: at its peak for a time of 0, else at the
// time, or the cap past it.
MTWN_NOINLINE static bool Lost(const Bound* bound, double time)
{
    double t = time == 0.0 ? Peak(bound) : (time < bound->cap ? time : bound->cap);
    return Underflows(bound->b, t) && bound->a + bound->b * t < DBL_MIN;
}

// The bound's logarithm above ln tolerance at any time t, past the cap a
// pure exponential's, plus how far it can be from the true excess,
// rounded: a + b t within 2 roundings, or an absolute half of the least
// subnormal below the normal doubles; the logarithm within an ulp, 2 u of
// it; r t, the differences and the tolerance's logarithm within a rounding
// each of what they hold. A start at rest has a bound of exactly 0.
static double CertainExcess(const Bound* bound, double t)
{
    double reach = t < bound->cap ? t : bound->cap;
    double value = bound->a + bound->b * reach;
    double logValue = mtwnLog(value);
    double excess = logValue - bound->rate * t - bound->logTolerance;
    if (value == 0.0)
    {
        return excess;
    }
    // a alone is exact, with no half subnormal to fear.
    double lost = bound->b == 0.0 || reach == 0.0 ? 0.0 : 0x1.0p-1072 / value;
    return excess +
           16.0 * s_unit * (1.0 + fabs(logValue) + bound->rate * t + fabs(bound->logTolerance)) +
           bound->logError + lost;
}

// A time at or after t at which the bound is within its tolerance for
// sure, its excess plus the excess's error at most 0: t, or a time found
// stepping on from it, for t at or after the bound's peak, where its
// logarithm only falls. Each step falls by at least the excess left: by
// its slope, or where that is nearly flat, by its fall past the peak, at
// least r d - ln(1 + r d) over a time d. NaN if none is found in so many
// steps; infinity for a bound that never decays.
MTWN_NOINLINE static double Certify(const Bound* bound, double t)
{
    for (int i = 0; i < s_newtonSteps; ++i)
    {
        double excess = CertainExcess(bound, t);
        if (excess <= 0.0)
        {
            return t;
        }
        if (!(bound->rate > 0.0))
        {
            return HUGE_VAL;
        }
        double fall = bound->rate - (t < bound->cap ? bound->b / (bound->a + bound->b * t) : 0.0);
        double curved = (sqrt(2.0 * excess) + 2.0 * excess) / bound->rate;
        double step = fall > 0.0 ? excess / fall : curved;
        t += (step < curved ? step : curved) + 0x1.0p-50 * t;
    }
    return (double)NAN;
}

// A bound's time; NaN where it overflowed, as a decaying bound ends: a
// coefficient, two of its products with opposite signs, or the bound on
// the way; or the time itself is past the doubles. For a start not yet
// scaled, lost tells whether the bound lost digits to underflow: its b,
// with a product underflowed in it, or its value at its time below the
// normal doubles. A finite time asked to be certified is within for sure
// at it, or for a time of 0 at the bound's peak, its largest value.
MTWN_NOINLINE static double TimeOf(const Bound* bound, bool underflowed, bool* lost)
{
    if (!(bound->b < HUGE_VAL))
    {
        return (double)NAN;
    }
    double time = BoundTime(bound);
    if (bound->certify && isfinite(time) && bound->logTolerance < HUGE_VAL)
    {
        // A time of 0 is certified at the peak, the bound's largest value.
        double peak = Peak(bound);
        double certified = Certify(bound, time == 0.0 ? peak : time);
        time = time == 0.0 && certified == peak ? 0.0 : certified;
    }
    *lost = (underflowed && bound->b < DBL_MIN) || Lost(bound, time);
    return bound->rate > 0.0 && !(time < HUGE_VAL) ? (double)NAN : time;
}

// The times the two bounds of a start, its offset's and its velocity's,
// stay within their tolerances, given by their logarithms and those
// logarithms' errors, for each time still NaN; true when one overflowed.
// A start not yet scaled leaves NaN for a bound that overflowed or lost
// digits to underflow; a scaled one gives infinity for a time still past
// the doubles.
MTWN_NOINLINE static bool SettleFrom(const mtwnSpring* spring, const Bound* bound, bool scaled,
                                     double offset, double velocity, const double logs[2],
                                     const double errors[2], double times[2])
{
    double decay = spring->parameters[s_decay];
    double squared = spring->parameters[s_squared];
    Bound bounds[2] = {*bound, *bound};
    bounds[0].a = fabs(offset);
    bounds[0].b = fabs(velocity + decay * offset);
    bounds[1].a = fabs(velocity);
    bounds[1].b = fabs(decay * velocity + squared * offset);
    bool underflowed[2] = {Underflows(decay, offset),
                           Underflows(decay, velocity) || Underflows(squared, offset)};
    bool overflowed = false;
    for (int k = 0; k < 2; ++k)
    {
        bounds[k].logTolerance = logs[k];
        bounds[k].logError = errors[k];
        if (!isnan(times[k]))
        {
            continue;
        }
        bool lost = false;
        double time = TimeOf(&bounds[k], underflowed[k], &lost);
        overflowed = overflowed || isnan(time);
        times[k] = scaled ? (isnan(time) ? HUGE_VAL : time) : (lost ? (double)NAN : time);
    }
    return overflowed;
}

// A logarithm's error, the tolerance's within an ulp, less a margin and a
// shift, each difference and the shift rounded.
static double LogError(double logTolerance, double margin, double shift)
{
    return 5.0 * s_unit * (fabs(logTolerance) + margin + fabs(shift));
}

// The times a start's two bounds stay within their tolerances, given by
// their logarithms, each lowered by its margin: certified with margins,
// and without, the bounds' own times as Newton's steps leave them.
MTWN_NOINLINE static void Settle(const mtwnSpring* spring, const Bound* bound, double offset,
                                 double velocity, const double logTolerances[2],
                                 const double margins[2], double times[2])
{
    Bound asked = *bound;
    asked.certify = margins[0] > 0.0 || margins[1] > 0.0;
    bound = &asked;
    double logs[2] = {logTolerances[0] - margins[0], logTolerances[1] - margins[1]};
    double errors[2] = {LogError(logTolerances[0], margins[0], 0.0),
                        LogError(logTolerances[1], margins[1], 0.0)};
    times[0] = (double)NAN;
    times[1] = (double)NAN;
    if (SettleFrom(spring, bound, false, offset, velocity, logs, errors, times))
    {
        // An overflow redoes both bounds, an underflow only its own.
        times[0] = (double)NAN;
        times[1] = (double)NAN;
    }
    if (!isnan(times[0]) && !isnan(times[1]))
    {
        return;
    }
    if (!isfinite(offset) || !isfinite(velocity))
    {
        times[0] = HUGE_VAL;
        times[1] = HUGE_VAL;
        return;
    }
    // The time a bound crosses its tolerance is the same with both divided
    // by 2^e, for e the exponent of the start's larger part: the start is
    // then below 1 and every coefficient at most decay + w0^2. Dividing it
    // is exact but for a part too small beside the other to count, and the
    // tolerances' logarithms move by e ln 2. A w0^2 below the normal
    // doubles keeps its product with a start below 1 there too, so the
    // start is left 2^53 larger: such a spring's decay is below 2^513, as
    // its w0 is below 2^-511 and its ratio below 2^1024.
    int exponent = 0;
    (void)frexp(fabs(offset) > fabs(velocity) ? offset : velocity, &exponent);
    exponent -= spring->parameters[s_squared] < DBL_MIN ? 53 : 0;
    double shift = (double)exponent * MTWN_LN2_HI + (double)exponent * MTWN_LN2_LO;
    for (int k = 0; k < 2; ++k)
    {
        logs[k] -= shift;
        errors[k] = LogError(logTolerances[k], margins[k], shift);
    }
    // ldexp, as 2^-e overflows for a start below the normal doubles.
    (void)SettleFrom(spring, bound, true, ldexp(offset, -exponent), ldexp(velocity, -exponent),
                     logs, errors, times);
}

// The bounds hold the motion, but mtwnSampleSpring rounds it, so at the
// bounds' time its sample could be past a tolerance by its rounding. Each
// bound is solved for its tolerance less a margin that covers it, so the
// sample is within from the time on. With u = 2^-53, r the bound's rate,
// L = (a + w) min(1 / (2 w), 747 (a + w) / r) for an overdamped spring
// (w > 0) and 0 otherwise, and t a time:
//
// 1. The sample is within (1 + mu(t)) B(t) + nu of 0, B the bound with
//    the coefficients rounded as here, for mu(t) = u (c0 + c1 t),
//    c0 = 60 (1 + L)^2 and c1 = 30 (1 + L) r, and nu = 2^-1074 (1 + h0
//    |x0| + h1 |v0|), h0 and h1 from the spring's coefficients
//    (SampleErrorOf).
//    Each operation is within u of its result relatively, mtwnExp within
//    2 u, plus an absolute half of the least subnormal where a result
//    falls below the normal doubles, an addition then exact; those halves,
//    times what multiplies them on the way, make nu, and the rest mu.
//    - Below critical damping and at it, e^(-a t) is a common factor,
//      within 1.01 (u a t + 2 u), a t at most 746 where it is not 0. The
//      rest is C x0 + a S x0 + S v0 rounded, |C| at most 1 + 2 u and |S|
//      at most min(t, cap) (1 + 3 u): its size is within 3 u of the
//      bound's over e^(-a t), as v0 + a x0 is together in it, and its
//      roundings, 5 u of each term, within 5 u (1 + 2 a min(t, cap)) of
//      it, writing |v0| <= |v0 + a x0| + a |x0|. b rounded adds 3 u + 2 u a
//      min(t, cap). So mu <= u (14 + 13 a t), a = r below critical
//      damping and a <= r (1 + 3 u) at it. The velocity is alike.
//    - An overdamped spring below w t = 1/2 is alike with C = cosh(w t),
//      within 4 u, and S = t sinh(w t) / (w t), within 3.3 u, and
//      e^(-a t) cosh and e^(-a t) S under e^(-(a - w) t) and that times
//      min(t, 1 / (2 w)). The rate r = w0^2 / (a + w) is a - w within
//      (9 a + 3 r) t u there, as a, w and w0^2 are each within a few u of
//      theirs: so mu <= u (18 + 27 a t), a t <= min(L, 746) as t < 1 / (2
//      w) and e^(-a t) is not 0.
//    - From w t = 1/2, x = ((F p - r q) x0 + (p - q) v0) / (2 w), F = a + w
//      rounded, p = e^(-r t) and q = e^(-F t), and |x| <= B (1 + 13 u L),
//      writing F x0 + v0 and r x0 + v0 with v0 + a x0. The terms with p
//      are within 3.01 L B, those with q within q / p times it, p within
//      1.01 u (r t + 2) and q / p times q's error within 1.01 u (0.38 L +
//      2), as F t e^(-(F - r) t) <= F / (e (F - r)). So mu <= u (3 + L (58
//      + 3.04 r t + 1.16 L)). And p is not 0 only while r t <= 746, so
//      only while r / (2 w) <= 746 from w t = 1/2, which bounds L.
//    - Every case is within mu(t) = u (60 (1 + L)^2 + 30 (1 + L) r t).
// 2. B is within e^(-m) of the tolerance at the time found, certified as
//    CertainExcess counts, and B only falls from the bound's peak, its
//    logarithm by at least r d - ln(1 + r d) a time d past it. So for any
//    t after it, (1 + mu(t)) B(t) + nu is within the tolerance where m is
//    at least mu(T) + 2.02 nu / tolerance (nu up to a quarter of it) + k^2 +
//    5 u, for T at least the time and the peak, under 1 / r: k = u c1 / r
//    <= 1/2 is how fast mu grows against B's fall, and the most it gains
//    is k^2; the peak, found within a few u / r, adds at most 32 u^2.
// A start whose nu passes a quarter of a tolerance, or whose margin does
// not settle, takes the bounds' time or the time its exponentials reach
// 0, s_zeroRates / r, whichever is later: the sample is 0 from then on.
// A start at rest, whose sample is always exactly 0, and a bound within at
// once keep the bounds' time, 0. Any other where the exponentials never
// reach 0 within the doubles (s_zeroRates / r infinite) gets infinity.
typedef struct SampleError
{
    // mu(t) = u (c0 + grows r t), grows = 30 (1 + L).
    double c0;
    double grows;
    // nu over 2^-1074, less 1: these times |x0| and |v0|, for the offset
    // and then the velocity.
    double offsetFrom[2];
    double velocityFrom[2];
} SampleError;

// The sample's error against the bounds, as above. The halves of the
// least subnormal make nu: each exponential's, each product's, carried
// through what multiplies them on the way. Where e^(-a t) is not 0, a t
// is at most 746, so a |S| is at most 754 and |S| at most 1.01 min(746 /
// a, cap); p is not 0 only from w t = 1/2, where 1 / (2 w) is the cap.
MTWN_NOINLINE static SampleError SampleErrorOf(const mtwnSpring* spring, const Bound* bound)
{
    double decay = spring->parameters[s_decay];
    double damped = spring->parameters[s_damped];
    double squared = spring->parameters[s_squared];
    double swing = (s_zeroRates - 1.0) / decay;
    swing = 1.01 * (swing < bound->cap ? swing : bound->cap);
    double decayed = decay > 0.0 ? 754.0 : 0.0;
    double lift = 0.0;
    SampleError error = {0.0,
                         0.0,
                         {3.13 + decay + decayed, 1.0 + swing},
                         {1.0 + squared + squared * swing, 3.13 + decay + decayed}};
    if (spring->regime == s_overdamped && damped > 0.0)
    {
        double fast = decay + damped;
        double flat = fast * bound->cap;
        double reach = s_zeroRates * fast / bound->rate;
        lift = flat < reach ? flat : reach;
        double both = (fast + bound->rate + 2.0) * bound->cap + 1.0;
        error.offsetFrom[0] += both;
        error.offsetFrom[1] += 2.0 * bound->cap + 1.0;
        error.velocityFrom[0] += squared * (2.0 * bound->cap + 1.0) + 1.0;
        error.velocityFrom[1] += both;
    }
    error.c0 = 60.0 * (1.0 + lift) * (1.0 + lift);
    error.grows = 30.0 * (1.0 + lift);
    return error;
}

// 2^-1074 factor value over a tolerance, by exponents, so that no step
// overflows or loses digits before the end: past the doubles it is
// infinity, far over a quarter, and below them 0, far under an ulp.
MTWN_NOINLINE static double UnderflowShare(double factor, double value, double tolerance)
{
    int factorExponent = 0;
    int valueExponent = 0;
    int toleranceExponent = 0;
    double factorDigits = frexp(factor, &factorExponent);
    double valueDigits = frexp(value, &valueExponent);
    double toleranceDigits = frexp(tolerance, &toleranceExponent);
    return ldexp(factorDigits * valueDigits / toleranceDigits,
                 factorExponent + valueExponent - toleranceExponent - 1074);
}

// nu over a tolerance, for a bound's factors of |x0| and |v0|.
static double UnderflowOver(const double from[2], double offset, double velocity, double tolerance)
{
    return UnderflowShare(1.0, 1.0, tolerance) + UnderflowShare(from[0], fabs(offset), tolerance) +
           UnderflowShare(from[1], fabs(velocity), tolerance);
}

// The margin a bound needs at r t, t at least the time and the bound's
// peak, given nu over its tolerance.
MTWN_NOINLINE static double MarginFor(const SampleError* error, double rateTime, double nu)
{
    double grows = s_unit * error->grows;
    return s_unit * (error->c0 + error->grows * rateTime) + 2.02 * nu + grows * grows +
           5.0 * s_unit;
}

// r t for a bound's time t, at least 1, the peak being under 1 / r; 0
// for a bound that never decays, whose error never grows.
static double RateTime(const Bound* bound, double t)
{
    double rateTime = bound->rate * t;
    return bound->rate > 0.0 ? (rateTime > 1.0 ? rateTime : 1.0) : 0.0;
}

// A bound's time where no margin is sure, from its time without one and
// the time its exponentials reach 0. A start at rest samples exactly 0 at
// every time, and a bound within at once is settled at once, as promised,
// within a subnormal step through rounding ties. Otherwise, where the
// exponentials never reach 0 within the doubles, an undamped spring's or
// one decaying slower still, no later time serves, and it never settles
// for sure: the sample's steps below the normal doubles, multiplied by the
// start, are uncovered. Else the later of the two, from which the sample
// is 0.
static double Unsure(double plain, double zero, bool rest)
{
    if (rest || plain == 0.0)
    {
        return plain;
    }
    if (!isfinite(zero))
    {
        return HUGE_VAL;
    }
    return plain > zero ? plain : zero;
}

MTWN_NOINLINE double mtwnComputeSpringSettlingTimeWork(const mtwnSpring* spring, double offset,
                                                       double velocity, double offsetTolerance,
                                                       double velocityTolerance)
{
    if (!(offsetTolerance > 0.0) || !(velocityTolerance > 0.0))
    {
        return HUGE_VAL;
    }
    double decay = spring->parameters[s_decay];
    double damped = spring->parameters[s_damped];
    Bound bound = {0.0, 0.0, decay, HUGE_VAL, 0.0, 0.0, false};
    if (spring->regime == s_underdamped)
    {
        // |sin(w t) / w| <= min(t, 1 / w).
        bound.cap = 1.0 / damped;
    }
    else
    {
        // e^(-a t) cosh(w t) <= e^(-(a - w) t), and e^(-a t) sinh(w t) / w
        // <= e^(-(a - w) t) min(t, 1 / (2 w)).
        bound.rate = spring->parameters[s_squared] / (decay + damped);
        bound.cap = 0.5 / damped;
    }
    double logTolerances[2] = {mtwnLog(offsetTolerance), mtwnLog(velocityTolerance)};
    double margins[2] = {0.0, 0.0};
    double plain[2];
    Settle(spring, &bound, offset, velocity, logTolerances, margins, plain);
    SampleError error = SampleErrorOf(spring, &bound);
    double nus[2] = {UnderflowOver(error.offsetFrom, offset, velocity, offsetTolerance),
                     UnderflowOver(error.velocityFrom, offset, velocity, velocityTolerance)};
    double times[2] = {plain[0], plain[1]};
    bool sure[2] = {false, false};
    for (int round = 0; round < s_marginRounds && !(sure[0] && sure[1]); ++round)
    {
        // Each margin for twice its r t, past where its time will land.
        for (int k = 0; k < 2; ++k)
        {
            margins[k] = MarginFor(&error, 2.0 * RateTime(&bound, times[k]), nus[k]);
        }
        Settle(spring, &bound, offset, velocity, logTolerances, margins, times);
        for (int k = 0; k < 2; ++k)
        {
            sure[k] = isfinite(margins[k]) &&
                      MarginFor(&error, RateTime(&bound, times[k]), nus[k]) <= margins[k];
        }
    }
    bool rest = offset == 0.0 && velocity == 0.0;
    for (int k = 0; k < 2; ++k)
    {
        if (!sure[k] || !(nus[k] <= 0.25))
        {
            times[k] = Unsure(plain[k], s_zeroRates / bound.rate, rest);
        }
    }
    return times[0] > times[1] ? times[0] : times[1];
}

static MTWN_NOINLINE mtwnResult mtwnComputeSpringSettlingTimeChecked(const mtwnSpring* spring,
                                                                     double offset, double velocity,
                                                                     double offsetTolerance,
                                                                     double velocityTolerance,
                                                                     double* secondsOut)
{
    if (!mtwnValidSpring(spring) || !isfinite(offset) || !isfinite(velocity) ||
        !(offsetTolerance > 0.0) || !(velocityTolerance > 0.0) || secondsOut == NULL)
    {
        return mtwn_errorInvalid;
    }
    *secondsOut = mtwnComputeSpringSettlingTimeWork(spring, offset, velocity, offsetTolerance,
                                                    velocityTolerance);
    return mtwn_success;
}

mtwnResult mtwnComputeSpringSettlingTime(const mtwnSpring* spring, double offset, double velocity,
                                         double offsetTolerance, double velocityTolerance,
                                         double* secondsOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnComputeSpringSettlingTimeChecked(
        spring, offset, velocity, offsetTolerance, velocityTolerance, secondsOut);
    mtwnFloatEnvLeave(env);
    return result;
}

// The spring easing's parameters: the spring's rates and velocity scaled
// to progress (a T, w T, (w0 T)^2, v T), and its value at progress 1.
enum
{
    s_easingDecay = 0,
    s_easingDamped = 1,
    s_easingSquared = 2,
    s_easingVelocity = 3,
    s_easingRange = 4,
};

// The spring easing's fastest oscillation, in radians a unit of progress:
// a period of 2^-10, as the elastic's shortest (src/easing.c).
static const double s_largestEasingDamped = 4.0 * MTWN_PIO2_HI * 0x1.0p10;

// The spring in progress time an easing holds, and its unnormalized
// value, 1 plus its offset, at p.
static double SpringEasingRaw(const double* parameters, uint32_t regime, double p)
{
    mtwnSpring scaled = {{0.0}, regime};
    scaled.parameters[s_decay] = parameters[s_easingDecay];
    scaled.parameters[s_damped] = parameters[s_easingDamped];
    scaled.parameters[s_squared] = parameters[s_easingSquared];
    mtwnSpringMotion motion = mtwnComputeSpringMotionWork(&scaled, p);
    return 1.0 - motion.offsetFromOffset + motion.offsetFromVelocity * parameters[s_easingVelocity];
}

static MTWN_NOINLINE mtwnResult mtwnMakeSpringEasingWork(const mtwnSpring* spring, double duration,
                                                         double velocity, mtwnEaseMode mode,
                                                         mtwnEasing* easingOut)
{
    // A duration or velocity NaN or infinite leaves a scaled parameter
    // that is not finite, refused below.
    if (!mtwnValidSpring(spring) || easingOut == NULL || (uint32_t)mode > mtwn_easeOutIn ||
        !(duration > 0.0))
    {
        return mtwn_errorInvalid;
    }
    double frequency = spring->parameters[s_frequency] * duration;
    mtwnEasing easing = {{0.0}, NULL, NULL, NULL, 0, mtwn_easingSpring, mode, 0};
    easing.parameters[s_easingDecay] = spring->parameters[s_decay] * duration;
    easing.parameters[s_easingDamped] = spring->parameters[s_damped] * duration;
    easing.parameters[s_easingSquared] = frequency * frequency;
    easing.parameters[s_easingVelocity] = velocity * duration;
    easing.count = spring->regime;
    // A spring that has not moved by the duration, or is back where it
    // started, has no range to normalize by; the checks refuse it, and a
    // scaled parameter that is not finite.
    easing.parameters[s_easingRange] = SpringEasingRaw(easing.parameters, spring->regime, 1.0);
    if (!mtwnValidSpringEasing(&easing))
    {
        return mtwn_errorInvalid;
    }
    *easingOut = easing;
    return mtwn_success;
}

mtwnResult mtwnMakeSpringEasing(const mtwnSpring* spring, double duration, double velocity,
                                mtwnEaseMode mode, mtwnEasing* easingOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnMakeSpringEasingWork(spring, duration, velocity, mode, easingOut);
    mtwnFloatEnvLeave(env);
    return result;
}

double mtwnSampleSpringEasing(const mtwnEasing* easing, double p)
{
    return SpringEasingRaw(easing->parameters, easing->count, p) /
           easing->parameters[s_easingRange];
}

bool mtwnValidSpringEasing(const mtwnEasing* easing)
{
    const double* parameter = easing->parameters;
    double decay = parameter[s_easingDecay];
    double damped = parameter[s_easingDamped];
    double fast = decay + damped;
    bool oscillates = easing->count == s_underdamped;
    // With these, over progress 0 to 1 the motion's offset from its offset
    // is within 1 + 1/e of 0 underdamped, and 3 fast overdamped (its slow
    // rate at most twice its fast); its offset from its velocity within 1:
    // the raw value within bound, and over the range finite (a bound that
    // is not finite, or a range of 0 or NaN, leaves a ratio that is not).
    // Twice the fast rate's square may pass the doubles, which leaves every
    // finite squared frequency within it, but not an infinite one.
    double bound = 4.0 + 3.0 * fast + 2.0 * fabs(parameter[s_easingVelocity]);
    double range = fabs(parameter[s_easingRange]);
    return easing->count <= s_overdamped && decay >= 0.0 &&
           (oscillates ? damped > 0.0 && damped <= s_largestEasingDamped : damped >= 0.0) &&
           parameter[s_easingSquared] >= 0.0 && parameter[s_easingSquared] <= DBL_MAX &&
           parameter[s_easingSquared] <= 2.0 * fast * fast && range <= DBL_MAX &&
           bound / range <= DBL_MAX;
}

// Whether two doubles have the same bits: a sign of zero or a NaN no
// maker gives is refused with the rest.
static bool SameBits(double a, double b)
{
    uint64_t x = 0;
    uint64_t y = 0;
    memcpy(&x, &a, sizeof x);
    memcpy(&y, &b, sizeof y);
    return x == y;
}

MTWN_NOINLINE bool mtwnValidSpring(const mtwnSpring* spring)
{
    // The maker's own arithmetic from the two fields it was made from: a
    // frequency or ratio no maker takes is refused there, and the fields
    // it derives from them must be what it gives.
    mtwnSpring made;
    return spring != NULL &&
           Finish(spring->parameters[s_frequency], spring->parameters[s_ratio], &made) ==
               mtwn_success &&
           SameBits(made.parameters[s_decay], spring->parameters[s_decay]) &&
           SameBits(made.parameters[s_damped], spring->parameters[s_damped]) &&
           SameBits(made.parameters[s_squared], spring->parameters[s_squared]) &&
           made.regime == spring->regime;
}
