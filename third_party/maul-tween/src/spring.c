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
// ln tolerance.
typedef struct Bound
{
    double a;
    double b;
    double rate;
    double cap;
    double logTolerance;
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

// A bound's time; NaN where it overflowed, as a decaying bound ends: a
// coefficient, two of its products with opposite signs, or the bound on
// the way; or the time itself is past the doubles. For a start not yet
// scaled, lost tells whether the bound lost digits to underflow: its b,
// with a product underflowed in it, or its value at its time below the
// normal doubles.
MTWN_NOINLINE static double TimeOf(const Bound* bound, bool underflowed, bool* lost)
{
    if (!(bound->b < HUGE_VAL))
    {
        return (double)NAN;
    }
    double time = BoundTime(bound);
    *lost = (underflowed && bound->b < DBL_MIN) || Lost(bound, time);
    return bound->rate > 0.0 && !(time < HUGE_VAL) ? (double)NAN : time;
}

// The times the two bounds of a start, its offset's and its velocity's,
// stay within their tolerances, given by their logarithms, for each time
// still NaN; true when one overflowed. A start not yet scaled leaves NaN
// for a bound that overflowed or lost digits to underflow; a scaled one
// gives infinity for a time still past the doubles.
MTWN_NOINLINE static bool SettleFrom(const mtwnSpring* spring, const Bound* bound, bool scaled,
                                     double offset, double velocity, double logOffsetTolerance,
                                     double logVelocityTolerance, double times[2])
{
    double decay = spring->parameters[s_decay];
    double squared = spring->parameters[s_squared];
    Bound bounds[2] = {*bound, *bound};
    bounds[0].a = fabs(offset);
    bounds[0].b = fabs(velocity + decay * offset);
    bounds[0].logTolerance = logOffsetTolerance;
    bounds[1].a = fabs(velocity);
    bounds[1].b = fabs(decay * velocity + squared * offset);
    bounds[1].logTolerance = logVelocityTolerance;
    bool underflowed[2] = {Underflows(decay, offset),
                           Underflows(decay, velocity) || Underflows(squared, offset)};
    bool overflowed = false;
    for (int k = 0; k < 2; ++k)
    {
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
    Bound bound = {0.0, 0.0, decay, HUGE_VAL, 0.0};
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
    double logOffsetTolerance = mtwnLog(offsetTolerance);
    double logVelocityTolerance = mtwnLog(velocityTolerance);
    double times[2] = {(double)NAN, (double)NAN};
    if (SettleFrom(spring, &bound, false, offset, velocity, logOffsetTolerance,
                   logVelocityTolerance, times))
    {
        // An overflow redoes both bounds, an underflow only its own.
        times[0] = (double)NAN;
        times[1] = (double)NAN;
    }
    if (!isnan(times[0]) && !isnan(times[1]))
    {
        return times[0] > times[1] ? times[0] : times[1];
    }
    if (!isfinite(offset) || !isfinite(velocity))
    {
        return HUGE_VAL;
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
    // ldexp, as 2^-e overflows for a start below the normal doubles.
    (void)SettleFrom(spring, &bound, true, ldexp(offset, -exponent), ldexp(velocity, -exponent),
                     logOffsetTolerance - shift, logVelocityTolerance - shift, times);
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
    if (!mtwnValidSpring(spring) || easingOut == NULL || mode > mtwn_easeOutIn || !(duration > 0.0))
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
