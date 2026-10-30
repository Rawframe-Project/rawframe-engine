// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Easing: curves that map a timing's progress to eased progress.
// An easing is a value made by a function that validates it and sampled
// by mtwnSampleEasing: Penner's families, which land exactly on 0 and 1, and CSS
// Easing Level 2 entire. Every kind has the same four modes.

#ifndef MAUL_TWEEN_EASING_H
#define MAUL_TWEEN_EASING_H

#include "maul-tween/base.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // What an easing is.
    typedef uint8_t mtwnEasingKind;
    enum
    {
        // The identity: eased progress is progress.
        mtwn_easingLinear = 0,
        // p^k for an exponent k above 0: quad is 2, cubic 3, quart 4,
        // quint 5.
        mtwn_easingPower = 1,
        // 1 - cos(p pi / 2).
        mtwn_easingSine = 2,
        // 2^(10 (p - 1)), normalized to start at 0.
        mtwn_easingExpo = 3,
        // 1 - sqrt(1 - p^2).
        mtwn_easingCirc = 4,
        // p^2 ((s + 1) p - s), dipping below 0 by an overshoot s.
        mtwn_easingBack = 5,
        // A decaying sine, from GSAP's elastic with an amplitude and a
        // period.
        mtwn_easingElastic = 6,
        // Penner's four parabolas.
        mtwn_easingBounce = 7,
        // A function of the host's.
        mtwn_easingHost = 8,
        // CSS's cubic-bezier(x1, y1, x2, y2).
        mtwn_easingCubicBezier = 9,
        // CSS's steps(n, position).
        mtwn_easingSteps = 10,
        // CSS's linear() with points.
        mtwn_easingLinearPoints = 11,
        // A spring over a duration (maul-tween/spring.h).
        mtwn_easingSpring = 12,
    };

    // Where CSS's steps() jumps.
    typedef uint8_t mtwnStepPosition;
    enum
    {
        // At the start of each step: CSS's jump-start and start.
        mtwn_jumpStart = 0,
        // At the end of each step: jump-end and end.
        mtwn_jumpEnd = 1,
        // Between steps only, holding 0 and 1 at the ends: jump-none.
        mtwn_jumpNone = 2,
        // At both ends and between: jump-both.
        mtwn_jumpBoth = 3,
    };

    // CSS's easing keywords.
    typedef uint8_t mtwnCssKeyword;
    enum
    {
        // linear.
        mtwn_cssLinear = 0,
        // ease: cubic-bezier(0.25, 0.1, 0.25, 1).
        mtwn_cssEase = 1,
        // ease-in: cubic-bezier(0.42, 0, 1, 1).
        mtwn_cssEaseIn = 2,
        // ease-out: cubic-bezier(0, 0, 0.58, 1).
        mtwn_cssEaseOut = 3,
        // ease-in-out: cubic-bezier(0.42, 0, 0.58, 1).
        mtwn_cssEaseInOut = 4,
        // step-start: steps(1, jump-start).
        mtwn_cssStepStart = 5,
        // step-end: steps(1, jump-end).
        mtwn_cssStepEnd = 6,
    };

    // A point of CSS's linear(): an output at an input progress. An input
    // of NaN is a stop given without a percentage; a stop with two
    // percentages is two points with the same output.
    typedef struct mtwnLinearPoint
    {
        double input;
        double output;
    } mtwnLinearPoint;

    // How an easing's curve f is applied to progress p. The in mode is
    // the curve as defined; the others are its reflections, the same for
    // every kind.
    typedef uint8_t mtwnEaseMode;
    enum
    {
        // f(p).
        mtwn_easeIn = 0,
        // 1 - f(1 - p): the curve reversed.
        mtwn_easeOut = 1,
        // f(2p) / 2 for p below 1/2, then 1 - f(2 - 2p) / 2: in, then out.
        mtwn_easeInOut = 2,
        // The out mode's curve over the first half, then f's over the
        // second.
        mtwn_easeOutIn = 3,
    };

    // A host's easing curve: eased progress for a progress from 0 to 1.
    typedef double (*mtwnEaseFunction)(void* context, double progress);

    // An easing: a curve and its mode. Make one with a mtwnMake...
    // function or mtwnMakeLinearEasing; its fields are the library's, set by
    // the maker, and a host reads none of them. Every call taking one
    // refuses an easing whose fields its kind reads are not as its maker
    // sets them, or for an elastic and a spring easing outside the ranges
    // its maker gives them. It is a plain value, copied freely; what it
    // references stays the host's: a host function's context must outlive
    // every copy, and linear() points every copy the host keeps, since a
    // transition or a clip copies the points in when it takes the easing.
    typedef struct mtwnEasing
    {
        // The curve's numbers, precomputed by its maker.
        double parameters[5];
        // A host function and its context.
        mtwnEaseFunction function;
        void* context;
        // The points of linear().
        const mtwnLinearPoint* points;
        // An integer the kind needs: a power's integer exponent, the steps,
        // the points.
        uint32_t count;
        mtwnEasingKind kind;
        mtwnEaseMode mode;
        mtwnStepPosition position;
    } mtwnEasing;

    // How a rough easing's offsets grow and shrink with progress x
    // (GSAP's RoughEase tapers).
    typedef uint8_t mtwnRoughTaper;
    enum
    {
        // The same everywhere.
        mtwn_roughTaperNone = 0,
        // From 0 at the start: x^2.
        mtwn_roughTaperIn = 1,
        // To 0 at the end: (1 - x)^2.
        mtwn_roughTaperOut = 2,
        // 0 at both ends, full in the middle: (1 - |2x - 1|)^2.
        mtwn_roughTaperBoth = 3,
    };

    // A rough easing's flags, combined.
    enum
    {
        // Each output kept in [0, 1] (GSAP's clamp).
        mtwn_roughClamp = 1,
        // Each output at least the one before and at most 1, so the
        // easing never decreases (anime.js's irregular).
        mtwn_roughRising = 2,
        // No randomness: the inner points at their ranges' middles,
        // offset by the full strength alternately up and down, starting
        // up (GSAP's RoughEase unrandomized).
        mtwn_roughZigZag = 4,
    };

    // A rough easing: a shape wandered from at points by a seed.
    typedef struct mtwnRoughDef
    {
        // The easing the points wander about; a zeroed one is linear.
        mtwnEasing shape;
        // The largest offset either side, in eased progress, at least 0.
        double strength;
        // Any value; the same seed gives the same points.
        uint64_t seed;
        // How the offsets grow and shrink.
        mtwnRoughTaper taper;
        // mtwn_rough flags, combined.
        uint8_t flags;
    } mtwnRoughDef;

    /// Makes the linear easing, whose eased progress is its progress.
    ///
    /// @return The easing.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_API mtwnEasing mtwnMakeLinearEasing(void);

    /// Makes the power easing p^k: quad for k = 2, cubic 3, quart 4, quint
    /// 5. GSAP's power(n) is k = n + 1. Integer exponents up to 8 are
    /// computed by multiplication.
    ///
    /// @param mode        The mode.
    /// @param exponent    k, finite and above 0.
    /// @param easingOut   Receives the easing; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, an
    ///         unknown mode or an exponent outside the above.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMakePowerEasing(mtwnEaseMode mode, double exponent,
                                                           mtwnEasing* easingOut);

    /// Makes one of Penner's easings without parameters: sine, expo, circ
    /// or bounce. Each lands exactly on 0 at 0 and on 1 at 1; expo is
    /// normalized to do so and differs from Penner's by at most 2^-10.
    /// GSAP 3's expo is not Penner's but a blend of its own, 2^(10 (p -
    /// 1)) p + p^6 (1 - p), up to 2^-7 from this one.
    ///
    /// @param kind       mtwn_easingSine, mtwn_easingExpo, mtwn_easingCirc
    ///                   or mtwn_easingBounce.
    /// @param mode       The mode.
    /// @param easingOut  Receives the easing; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, another
    ///         kind or an unknown mode.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMakePennerEasing(mtwnEasingKind kind, mtwnEaseMode mode,
                                                            mtwnEasing* easingOut);

    /// Makes the back easing p^2 ((s + 1) p - s), normalized to land on 1.
    /// GSAP's back(s) and Penner's in and out use s as given, 1.70158 by
    /// default; Penner's and tween.js's in-out use 1.525 s, which is this
    /// easing with that s.
    ///
    /// @param mode       The mode.
    /// @param overshoot  s, finite and at least 0; 0 is the cubic.
    /// @param easingOut  Receives the easing; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, an
    ///         unknown mode or an overshoot outside the above.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMakeBackEasing(mtwnEaseMode mode, double overshoot,
                                                          mtwnEasing* easingOut);

    /// Makes the elastic easing, GSAP's elastic(amplitude, period): out is
    /// 1 + a 2^(-10 p) sin((p - c) 2 pi / P) with c = P asin(1 / a) /
    /// (2 pi), normalized to land on 0 and 1, and in its reflection. An
    /// amplitude below 1 is 1 with the period divided by it, as GSAP does.
    /// GSAP's defaults are amplitude 1 with period 0.3, or 0.45 in-out;
    /// Penner's and tween.js's are amplitude 1, period 0.4. GSAP and
    /// tween.js miss 1 at the end by up to a 2^-10 and set it there, so
    /// they differ from this by at most a (1 + a) 2^-10.
    ///
    /// The curve ends at 1 + a 2^-10 sin(...) before it is normalized by
    /// it, which an amplitude up to 512 keeps from 1/2 to 3/2. A period,
    /// once an amplitude below 1 has stretched it, below 2^-10 is
    /// refused: a fit of MTWN_MAX_FIT_POINTS points has 64 points to
    /// each oscillation there, which trace a sine to about 0.1% of its
    /// height, and finer ones it cannot follow.
    ///
    /// @param mode       The mode.
    /// @param amplitude  a, above 0 and at most 512.
    /// @param period     P, finite, in units of progress; P, or P / a for
    ///                   an amplitude below 1, at least 2^-10.
    /// @param easingOut  Receives the easing; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, an
    ///         unknown mode or a parameter outside the above.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMakeElasticEasing(mtwnEaseMode mode, double amplitude,
                                                             double period, mtwnEasing* easingOut);

    /// Makes an easing of a host function, called with progress clamped to
    /// 0 to 1, under the mode's reflections. The library neither checks
    /// nor forces its ends.
    ///
    /// @param mode       The mode.
    /// @param function   The function; it must be safe to call from every
    ///                   thread that samples the easing, and calls no
    ///                   function of a context sampling it.
    /// @param context    Passed to the function; may be NULL.
    /// @param easingOut  Receives the easing; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null function or
    ///         easingOut or an unknown mode.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMakeHostEasing(mtwnEaseMode mode,
                                                          mtwnEaseFunction function, void* context,
                                                          mtwnEasing* easingOut);

    /// Makes CSS's cubic-bezier(x1, y1, x2, y2), solved by safeguarded
    /// Newton to |x(t) - x| <= 1e-14: exactly 0 at 0 and
    /// 1 at 1, and outside 0 to 1 extended along CSS's end tangents.
    ///
    /// @param x1         The first control point's x, from 0 to 1.
    /// @param y1         Its y, at most 2^1000 in magnitude, which keeps the
    ///                   curve's polynomial finite.
    /// @param x2         The second control point's x, from 0 to 1.
    /// @param y2         Its y, likewise.
    /// @param easingOut  Receives the easing, in mode mtwn_easeIn; unchanged on
    ///                   failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer or a
    ///         point outside the above.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMakeCubicBezierEasing(double x1, double y1, double x2,
                                                                 double y2, mtwnEasing* easingOut);

    /// Makes CSS's steps(count, position), with CSS's before flag: in the
    /// before phase, a progress landing exactly on a step takes the step
    /// below.
    ///
    /// @param count      The steps: at least 1, or 2 for mtwn_jumpNone.
    /// @param position   Where it jumps.
    /// @param easingOut  Receives the easing, in mode mtwn_easeIn; unchanged on
    ///                   failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, an
    ///         unknown position or a count outside the above.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMakeStepsEasing(uint32_t count,
                                                           mtwnStepPosition position,
                                                           mtwnEasing* easingOut);

    /// Makes the easing a CSS keyword names.
    ///
    /// @param keyword    The keyword.
    /// @param easingOut  Receives the easing, in mode mtwn_easeIn; unchanged on
    ///                   failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer or an
    ///         unknown keyword.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMakeCssEasing(mtwnCssKeyword keyword,
                                                         mtwnEasing* easingOut);

    /// Makes CSS's linear() from its points, canonicalizing them in place
    /// as CSS does: a first input not given is 0 and a last one 1 or the
    /// largest before it; an input below an earlier one is raised to it;
    /// runs not given are spread evenly between their neighbors. The
    /// easing references the points, which must outlive it and should not
    /// change: changed, even out of order, while finite they give an
    /// unspecified finite value. Outside the points' inputs it extends
    /// their end segments, a value past the doubles' range given as the
    /// largest finite double of its sign.
    ///
    /// @param points     The points, in order: at least 2, each output
    ///                   finite and each input finite or NaN.
    /// @param count      The number of points.
    /// @param easingOut  Receives the easing, in mode mtwn_easeIn; unchanged on
    ///                   failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer or points
    ///         outside the above, leaving the points as they were.
    /// @par Thread safety
    /// Safe from any thread; the points are used by one thread at a time.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMakeLinearPointsEasing(mtwnLinearPoint* points,
                                                                  uint32_t count,
                                                                  mtwnEasing* easingOut);

    /// Makes a rough easing: linear() through points
    /// that wander from a shape by a seed, as GSAP's RoughEase and
    /// anime.js's irregular, the same curve for a seed on every platform.
    /// The first point is (0, 0) and the last (1, 1); the k-th of the m
    /// inner points falls at a seeded input in [k/m, (k+1)/m), so they
    /// stay in order, and its output is the shape there plus a seeded
    /// offset of at most the strength either side, by the taper.
    ///
    /// @param def        The rough easing.
    /// @param points     Receives the points, which the easing references:
    ///                   they must outlive it and not change.
    /// @param count      The number of points, at least 2.
    /// @param easingOut  Receives the easing, in mode mtwn_easeIn; unchanged on
    ///                   failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, a shape
    ///         no maker produced, fewer than 2 points, a strength below 0
    ///         or not finite, an unknown taper or flag, or a shape giving a
    ///         value not finite.
    /// @par Thread safety
    /// Safe from any thread; the points are used by one thread at a time.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMakeRoughEasing(const mtwnRoughDef* def,
                                                           mtwnLinearPoint* points, uint32_t count,
                                                           mtwnEasing* easingOut);

    /// Changes an easing's mode, keeping its curve.
    ///
    /// @param easing  An easing made by this library.
    /// @param mode    The mode.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, an
    ///         easing no maker produced or an unknown mode, leaving the
    ///         easing as it was.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnSetEaseMode(mtwnEasing* easing, mtwnEaseMode mode);

    /// Tells whether an easing never decreases, as a timeline's easing
    /// must: linear, power, sine, expo, circ and steps in every mode, a
    /// cubic bezier with both y from 0 to 1, linear() with outputs that
    /// never decrease; not back, elastic, bounce or a spring, and not a
    /// host function, which the library cannot inspect.
    ///
    /// @param easing        An easing made by this library.
    /// @param monotonicOut  Receives whether it never decreases; unchanged
    ///                      on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer or an
    ///         easing no maker produced.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnIsEasingMonotonic(const mtwnEasing* easing,
                                                             bool* monotonicOut);

    /// Samples an easing. CSS's curves follow CSS Easing Level 2 outside 0
    /// to 1; Penner's curves, springs and host functions see progress
    /// clamped to 0 to 1. The modes reflect the curve, and the before flag reaches it
    /// where it samples progress unreflected.
    ///
    /// Every curve but a host function's gives a finite value: CSS's, past
    /// the doubles' range, the largest finite double of its sign.
    ///
    /// @param easing    An easing made by this library.
    /// @param progress  The progress, finite.
    /// @param before    Whether the timing is in its before phase; it moves
    ///                  only curves that jump at their start.
    /// @param easedOut  Receives the eased progress: exactly 0 at 0 and 1 at
    ///                  1 for Penner's curves, springs, linear and cubic
    ///                  bezier; what CSS defines for steps and linear().
    ///                  Unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, an
    ///         easing no maker produced or a progress not finite.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnSampleEasing(const mtwnEasing* easing, double progress,
                                                        bool before, double* easedOut);

    /// Samples a cubic bezier easing as mtwnSampleEasing does, to the bit, in each
    /// mode, reaching none of the other curves. With link-time optimization,
    /// or with -ffunction-sections and --gc-sections, a program that
    /// samples cubic beziers only through it links none of mtwnSampleEasing's
    /// dispatch, Penner's curves or the spring easing; without either, the
    /// makers and mtwnSetEaseMode bring them along.
    ///
    /// @param easing    An easing made by mtwnMakeCubicBezierEasing or
    ///                  mtwnMakeCssEasing, its mode set by mtwnSetEaseMode,
    ///                  or the linear easing.
    /// @param progress  The progress, finite; outside 0 to 1 the curve
    ///                  follows CSS Easing Level 2.
    /// @param easedOut  Receives what mtwnSampleEasing gives with before false: the
    ///                  progress for the linear easing. Unchanged on
    ///                  failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, an
    ///         easing of another kind or no maker produced, or a progress
    ///         not finite.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnSampleCubicBezierEasing(const mtwnEasing* easing,
                                                                   double progress,
                                                                   double* easedOut);

#ifdef __cplusplus
}
#endif

#endif
