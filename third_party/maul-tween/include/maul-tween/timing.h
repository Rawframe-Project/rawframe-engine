// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Timing: Web Animations Level 1's
// calculation of a node's phase, iteration and progress from its local
// time, with Level 2's effect playback rate, a repeat delay between
// iterations and a choice of how reverse iterations ease.
// Staggers: offsets for items in a grid by their
// distance from an origin.

#ifndef MAUL_TWEEN_TIMING_H
#define MAUL_TWEEN_TIMING_H

#include "maul-tween/base.h"
#include "maul-tween/easing.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // Where a node's local time falls.
    typedef uint8_t mtwnPhase;
    enum
    {
        mtwn_phaseBefore = 0,
        mtwn_phaseActive = 1,
        mtwn_phaseAfter = 2,
    };

    // Whether a node holds its value outside its active interval.
    typedef uint8_t mtwnFill;
    enum
    {
        mtwn_fillNone = 0,
        mtwn_fillForwards = 1,
        mtwn_fillBackwards = 2,
        mtwn_fillBoth = 3,
    };

    // The direction of each iteration.
    typedef uint8_t mtwnDirection;
    enum
    {
        mtwn_directionNormal = 0,
        mtwn_directionReverse = 1,
        mtwn_directionAlternate = 2,
        mtwn_directionAlternateReverse = 3,
    };

    // How a reverse iteration eases.
    typedef uint8_t mtwnAlternateEasing;
    enum
    {
        // Time runs back: ease(1 - p), so an ease-in looks like an ease-out
        // on the way back. Web Animations'.
        mtwn_alternateRunsBack = 0,
        // The shape repeats: 1 - ease(p), the same curve on the way back.
        mtwn_alternateRepeatsShape = 1,
    };

    // A node's timing: Web Animations' effect timing and Maul Tween's
    // additions. Start from mtwnDefaultTiming and set what differs; check
    // it with mtwnValidateTiming. Times are seconds.
    typedef struct mtwnTiming
    {
        // From the node's start to its active interval; may be negative.
        double delay;
        // From the active interval's end to the node's end; may be
        // negative.
        double endDelay;
        // Where in its iterations the node starts, from 0.
        double iterationStart;
        // How many iterations, from 0, infinity allowed.
        double iterations;
        // One iteration's length, from 0, infinity allowed.
        double duration;
        // Between iterations, not after the last, from 0.
        double repeatDelay;
        // The node's own speed, finite and not 0; negative plays it
        // backwards.
        double playbackRate;
        // The easing of each iteration's progress.
        mtwnEasing easing;
        mtwnFill fill;
        mtwnDirection direction;
        mtwnAlternateEasing alternateEasing;
    } mtwnTiming;

    // A node's timing at one local time. Values a phase leaves undefined
    // (Web Animations' unresolved) are NaN.
    typedef struct mtwnTimingResult
    {
        // The active interval's length, and the node's end time.
        double activeDuration;
        double endTime;
        // The time into the active interval, NaN outside it without fill.
        double activeTime;
        // Iterations completed, partial ones included, iteration start
        // added.
        double overallProgress;
        // Progress within the current iteration, from 0 to 1.
        double simpleProgress;
        // The current iteration's index; infinity after an infinite run.
        double iteration;
        // The progress in the iteration's direction, and eased.
        double directedProgress;
        double transformedProgress;
        mtwnPhase phase;
        // The before flag the easing was given.
        bool before;
        // Whether the time lies in a repeat delay, the progress held at
        // its iteration's end.
        bool inRepeatDelay;
        // Whether the current iteration runs forwards, by the direction.
        bool forwards;
    } mtwnTimingResult;

    /// Returns Web Animations' default timing: no delays, fill none, one
    /// iteration from 0, a duration of 0, normal direction, linear easing,
    /// no repeat delay, time running back in reverse iterations, rate 1.
    ///
    /// @return The timing.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_API mtwnTiming mtwnDefaultTiming(void);

    /// Checks a timing: delays finite, iteration start finite and from 0,
    /// iterations and duration from 0 (infinity allowed, NaN not), repeat
    /// delay finite and from 0, playback rate finite and not 0, every
    /// enumeration known. The active duration and the end time (delay plus
    /// active duration plus end delay) must be finite, unless the
    /// iterations or the duration are infinite, making the active duration
    /// infinite, and the rate is positive:
    /// a backward play starts from the active interval's end, and finite
    /// fields that overflow a double there are refused. With a repeat
    /// delay, the duration plus the repeat delay must be finite.
    ///
    /// @param timing  The timing.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer or a
    ///         timing outside the above.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnValidateTiming(const mtwnTiming* timing);

    /// Computes a node's timing at a local time, by Web Animations' steps.
    ///
    /// @param timing     A timing mtwnValidateTiming accepts.
    /// @param localTime  The node's local time, seconds, finite or positive
    ///                   infinity: the limit, where a finite active interval
    ///                   lies behind and an endless one is still active, an
    ///                   infinite duration's progress at its iteration start
    ///                   (a child of a reversed endless timeline is there).
    /// @param backwards  Whether the time is running backwards (the
    ///                   playing animation's rate is negative), which moves
    ///                   the active interval's boundaries.
    /// @param resultOut  Receives the result; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, a timing
    ///         mtwnValidateTiming refuses, or a local time that is NaN or
    ///         negative infinity.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnComputeTiming(const mtwnTiming* timing, double localTime,
                                                         bool backwards,
                                                         mtwnTimingResult* resultOut);

    // Where a stagger begins: the items nearest it get the first offsets.
    typedef uint8_t mtwnStaggerOrigin;
    enum
    {
        // The first item.
        mtwn_staggerFirst = 0,
        // The last item.
        mtwn_staggerLast = 1,
        // The grid's center.
        mtwn_staggerCenter = 2,
        // The nearest edge, so the outermost items begin.
        mtwn_staggerEdges = 3,
        // The item at index.
        mtwn_staggerIndex = 4,
        // The point at pointX and pointY, ratios of the last column and
        // row: 0 the first, 1 the last, any value within 2^20.
        mtwn_staggerPoint = 5,
        // An order from the seed, each item its own step, every order
        // equally likely.
        mtwn_staggerRandom = 6,
    };

    // Which distance a stagger measures.
    typedef uint8_t mtwnStaggerAxis;
    enum
    {
        // Straight across the grid.
        mtwn_staggerBoth = 0,
        // Along the columns only.
        mtwn_staggerX = 1,
        // Along the rows only.
        mtwn_staggerY = 2,
    };

    // How a stagger's spacing spreads its offsets.
    typedef uint8_t mtwnStaggerSpacing;
    enum
    {
        // The spacing lies between items one cell apart.
        mtwn_staggerEach = 0,
        // The spacing is the whole spread, from the first offset to the
        // last.
        mtwn_staggerAmount = 1,
    };

    // A stagger: offsets for items in a grid, in reading order, by their
    // distance from an origin, eased. Start from mtwnDefaultStagger and
    // set what differs. The offsets are seconds when they are delays, or
    // any unit when they are values.
    typedef struct mtwnStagger
    {
        // The nearest items' offset.
        double start;
        // Each or the whole spread, by spacingKind; negative reverses the
        // order.
        double spacing;
        // The point's ratios, for mtwn_staggerPoint.
        double pointX;
        double pointY;
        // The easing of the distances, from 0 for the nearest items to 1
        // for the farthest.
        mtwnEasing easing;
        // The order's seed, for mtwn_staggerRandom.
        uint64_t seed;
        // Items per row; 0 puts them all in one row. A short last row
        // keeps the grid this wide: the center and the edges are the full
        // rectangle's, and the short row lies on its bottom edge.
        uint32_t columns;
        // The origin's item, for mtwn_staggerIndex.
        uint32_t index;
        mtwnStaggerOrigin origin;
        mtwnStaggerAxis axis;
        mtwnStaggerSpacing spacingKind;
    } mtwnStagger;

    /// Returns the default stagger: from the first item, 0.1 between
    /// neighbors in one row, from 0, linear, both axes.
    ///
    /// @return The stagger.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_API mtwnStagger mtwnDefaultStagger(void);

    /// Computes a stagger's offsets. Item i sits in
    /// column i mod columns and row i / columns; its distance d from the
    /// origin is Euclidean, or along one axis, or to the nearest edge;
    /// t = (d - min) / (max - min), 0 when all are equal, and the offset
    /// is start + span * ease(t), where span is the spacing for
    /// mtwn_staggerAmount and the spacing times (max - min) for
    /// mtwn_staggerEach. The offsets are the same bits on every platform.
    ///
    /// @param stagger     The stagger; its easing made by this library.
    /// @param count       The items; 0 writes nothing.
    /// @param offsetsOut  Receives count offsets; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer with a
    ///         count, an unknown origin, axis or spacing, an index not below a count above 0
    ///         for mtwn_staggerIndex, a start or spacing not finite, a
    ///         point beyond 2^20, or a spread (the spacing, or for
    ///         mtwn_staggerEach the spacing times the distances' range)
    ///         not finite, writing nothing.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnComputeStagger(const mtwnStagger* stagger,
                                                          uint32_t count, double* offsetsOut);

#ifdef __cplusplus
}
#endif

#endif
