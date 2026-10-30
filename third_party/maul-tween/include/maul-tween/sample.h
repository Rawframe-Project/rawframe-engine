// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Sampling: an easing turned into points in caller
// memory, as an even table for a GPU or a host lookup, or as the points
// of a CSS linear() fitted to a tolerance. Nothing is allocated.

#ifndef MAUL_TWEEN_SAMPLE_H
#define MAUL_TWEEN_SAMPLE_H

#include "maul-tween/base.h"
#include "maul-tween/easing.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // The most points a fit makes: a tolerance the curve cannot meet (below
    // its rounding, or an oscillation finer than 2^-24) would otherwise take
    // every segment down to that width, about 10^8 points.
#define MTWN_MAX_FIT_POINTS 65536u

    /// Samples an easing evenly over progress 0 to 1: valuesOut[i] is the
    /// eased progress at i / (count - 1), rounded to float.
    ///
    /// @param easing     An easing made by this library.
    /// @param count      The number of samples, at least 2.
    /// @param valuesOut  Receives count values; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, an
    ///         easing no maker produced or a count below 2.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnSampleEasingTable(const mtwnEasing* easing,
                                                             uint32_t count, float* valuesOut);

    /// Fits CSS linear() points to an easing over progress 0 to 1: their
    /// interpolation is within the tolerance of the eased progress at every
    /// probe, 32 inside each segment, and is not proven so between them;
    /// the points lie on the curve, the first at 0 and the last at 1. A segment narrower than 2^-24
    /// is kept as it is, so a host function's jump costs points and may stay over the tolerance
    /// within 2^-24 of it. Steps are refused: CSS writes them as steps().
    ///
    /// @param easing     An easing made by this library, not steps.
    /// @param tolerance  The largest error allowed, finite and above 0.
    /// @param pointsOut  Receives the points, with their inputs; may be NULL
    ///                   when capacity is 0; unspecified on failure.
    /// @param capacity   The number of points pointsOut holds.
    /// @param countOut   Receives the number of points the fit has, also
    ///                   when they do not fit; unchanged on the other
    ///                   failures.
    /// @return mtwn_success; mtwn_errorCapacity when more than capacity
    ///         points are needed, the contents of pointsOut then
    ///         unspecified; mtwn_errorLimit when more than
    ///         MTWN_MAX_FIT_POINTS are, the fit stopped there, countOut
    ///         0 and the contents of pointsOut unspecified;
    ///         mtwn_errorInvalid for a null easing or
    ///         countOut, an easing no maker produced, a NULL pointsOut with
    ///         a capacity, steps, a
    ///         tolerance outside the above, or an easing not finite at a
    ///         point the fit keeps.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnFitLinearEasing(const mtwnEasing* easing,
                                                           double tolerance,
                                                           mtwnLinearPoint* pointsOut,
                                                           uint32_t capacity, uint32_t* countOut);

#ifdef __cplusplus
}
#endif

#endif
