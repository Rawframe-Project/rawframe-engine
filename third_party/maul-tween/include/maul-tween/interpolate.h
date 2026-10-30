// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Interpolation of values that are not plain numbers:
// colors in a
// named space as CSS Color 4 interpolates them, angles with CSS's hue
// methods, rotations by slerp, integers and discrete values, and punch
// and shake, the signals a host adds to a value.
//
// A mix's sampler checks its fields by their ranges, not by making the mix
// again, which would cost as much as the sample. A mix that passes the
// check gives no undefined behavior and no NaN; it is not promised to
// equal what a maker would give. A color or rotation is finite, held
// within the floats as a target holds it; a scale or zoom reaches
// infinity or 0 only past the doubles' range, as a maker's mix does.

#ifndef MAUL_TWEEN_INTERPOLATE_H
#define MAUL_TWEEN_INTERPOLATE_H

#include "maul-tween/base.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // A color: gamma-encoded sRGB with straight alpha, as CSS's rgb(),
    // each channel nominally from 0 to 1. Channels outside that range are
    // extended sRGB, for wide-gamut hosts; alpha is from 0 to 1.
    typedef struct mtwnColor
    {
        float red;
        float green;
        float blue;
        float alpha;
    } mtwnColor;

    // A space colors interpolate in, and its coordinates.
    typedef uint8_t mtwnColorSpace;
    enum
    {
        // Oklab: lightness, a, b. CSS's default.
        mtwn_colorOklab = 0,
        // OkLCh: lightness, chroma, hue in radians.
        mtwn_colorOklch = 1,
        // Linear sRGB: red, green, blue in linear light, as light mixes.
        mtwn_colorLinearSrgb = 2,
        // Gamma sRGB: the channels as given, as legacy web content mixes.
        mtwn_colorSrgb = 3,
    };

    // How OkLCh hues interpolate, CSS Color 4's four methods.
    typedef uint8_t mtwnHueMethod;
    enum
    {
        // The shorter arc. CSS's default.
        mtwn_hueShorter = 0,
        // The longer arc.
        mtwn_hueLonger = 1,
        // Always increasing.
        mtwn_hueIncreasing = 2,
        // Always decreasing.
        mtwn_hueDecreasing = 3,
    };

    // Two colors ready to interpolate: converted to their space and
    // premultiplied. Make one with mtwnMakeColorMix; its fields are the
    // library's.
    typedef struct mtwnColorMix
    {
        double from[4];
        double to[4];
        mtwnColor fromColor;
        mtwnColor toColor;
        mtwnColorSpace space;
    } mtwnColorMix;

    /// Makes the interpolation between two colors in a space, following CSS
    /// Color 4: a powerless OkLCh hue (chroma at most 0.000004) takes the
    /// other color's, the hues are fixed up by the method, and the colors
    /// are premultiplied, the hue excepted.
    ///
    /// @param from    The color at progress 0: finite, alpha from 0 to 1.
    /// @param to      The color at progress 1, likewise.
    /// @param space   The space to interpolate in.
    /// @param hue     The hue method, read for OkLCh only.
    /// @param mixOut  Receives the mix; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, an
    ///         unknown space or method, or a color outside the above.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMakeColorMix(mtwnColor from, mtwnColor to,
                                                        mtwnColorSpace space, mtwnHueMethod hue,
                                                        mtwnColorMix* mixOut);

    /// Samples a mix: its colors exactly at 0 and 1, between them the
    /// premultiplied coordinates interpolated and divided by the
    /// interpolated alpha, or kept as they are where that alpha is 0 or
    /// below. Progress outside 0 to 1 extends the line, with alpha clamped
    /// to 0 to 1; the channels are not clamped.
    ///
    /// @param mix       A mix made by this library, or one whose fields pass
    ///                  its check (this header's opening).
    /// @param progress  The progress, finite.
    /// @param colorOut  Receives the color; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, a
    ///         progress not finite, or a mix whose space is unknown, whose
    ///         padding is not 0, whose colors mtwnMakeColorMix would
    ///         refuse, or whose ends have a coordinate not finite, an
    ///         alpha not their color's, or an OkLCh hue outside 0 to 4 pi.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMixColor(const mtwnColorMix* mix, double progress,
                                                    mtwnColor* colorOut);

    /// Converts a color to a space's coordinates and its alpha, for a host
    /// that springs or composes in the space.
    ///
    /// @param color           Finite, alpha from 0 to 1.
    /// @param space           The space.
    /// @param coordinatesOut  Receives the three coordinates and the alpha; an
    ///                        OkLCh hue in radians from 0 to 2 pi (CSS's
    ///                        degrees times pi / 180), NaN where it is
    ///                        powerless; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, an
    ///         unknown space or a color outside the above.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnConvertColorToSpace(mtwnColor color,
                                                               mtwnColorSpace space,
                                                               double coordinatesOut[4]);

    /// Converts a space's coordinates and an alpha back to a color.
    ///
    /// @param coordinates  The three coordinates and the alpha; a NaN OkLCh
    ///                     hue counts as 0, as CSS's missing hue does.
    /// @param space        The space.
    /// @param colorOut     Receives the color, alpha clamped to 0 to 1;
    ///                     unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, an
    ///         unknown space, or a coordinate or alpha not finite, a NaN
    ///         hue excepted.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnConvertColorFromSpace(const double coordinates[4],
                                                                 mtwnColorSpace space,
                                                                 mtwnColor* colorOut);

    // How an angle interpolates: raw, or by CSS's hue methods with a
    // period of a full turn.
    typedef uint8_t mtwnAngleMethod;
    enum
    {
        // As given, turns kept: 0 to 4 pi turns twice.
        mtwn_angleRaw = 0,
        // The shorter arc, at most half a turn.
        mtwn_angleShorter = 1,
        // The longer arc; a full turn between equal angles.
        mtwn_angleLonger = 2,
        // Always increasing, less than a turn.
        mtwn_angleIncreasing = 3,
        // Always decreasing, less than a turn.
        mtwn_angleDecreasing = 4,
    };

    /// Interpolates an angle in radians: the end at progress 1 is moved by
    /// whole turns as the method asks, then the two are interpolated
    /// linearly. At 0 the result is from exactly, and at 1 to exactly
    /// when no turn was added.
    ///
    /// @param from      The angle at progress 0, finite.
    /// @param to        The angle at progress 1, finite.
    /// @param method    The method.
    /// @param progress  The progress, finite; outside 0 to 1 extends.
    /// @param angleOut  Receives the angle; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null angleOut, an
    ///         unknown method, or an angle or progress not finite.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMixAngle(double from, double to, mtwnAngleMethod method,
                                                    double progress, double* angleOut);

    // A rotation as a unit quaternion (x, y, z, w): the axis times the sine
    // of half the angle, then its cosine. q and -q are the same rotation.
    typedef struct mtwnQuat
    {
        float x;
        float y;
        float z;
        float w;
    } mtwnQuat;

    // Which way a rotation turns between its ends.
    typedef uint8_t mtwnRotationPath;
    enum
    {
        // The shorter way, at most half a turn.
        mtwn_rotationShortest = 0,
        // The other way, the rest of the turn.
        mtwn_rotationLongest = 1,
    };

    // Two rotations ready to interpolate. Make one with
    // mtwnMakeRotationMix; its fields are the library's.
    typedef struct mtwnRotationMix
    {
        double from[4];
        double to[4];
        double angle;
        mtwnQuat fromRotation;
        mtwnQuat toRotation;
    } mtwnRotationMix;

    /// Makes the spherical interpolation between two rotations, at constant
    /// angular speed, the shortest way or the longest.
    ///
    /// @param from    The rotation at progress 0: finite, not zero; it is
    ///                normalized.
    /// @param to      The rotation at progress 1, likewise.
    /// @param path    Which way.
    /// @param mixOut  Receives the mix; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, an
    ///         unknown path, a rotation outside the above, or the longest
    ///         way between rotations so close (|q0 + q1| under 1e-6 once
    ///         the path's sign is taken) that it turns a full circle about
    ///         no defined axis.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMakeRotationMix(mtwnQuat from, mtwnQuat to,
                                                           mtwnRotationPath path,
                                                           mtwnRotationMix* mixOut);

    /// Samples a rotation mix: its rotations normalized exactly at 0 and 1
    /// (the second negated when the path needed it), between and beyond
    /// them slerp, of unit length to a float's rounding.
    ///
    /// @param mix          A mix made by this library, or one whose fields
    ///                     pass its check (this header's opening).
    /// @param progress     The progress, finite.
    /// @param rotationOut  Receives the rotation; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, a
    ///         progress not finite, or a mix whose ends are not of unit
    ///         length to within their normalization's rounding (12 times
    ///         2^-53 in the squared length) or not stored as their floats,
    ///         or whose angle is outside 0 to pi.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMixRotation(const mtwnRotationMix* mix, double progress,
                                                       mtwnQuat* rotationOut);

    // A scale: a positive number moved in its logarithm, so equal steps of
    // progress multiply it by equal ratios, where a linear mix from 1 to
    // 100 seems to slow down. Make one with
    // mtwnMakeScaleMix; its fields are the library's.
    typedef struct mtwnScaleMix
    {
        double from;
        double to;
        double logFrom;
        double logRatio;
    } mtwnScaleMix;

    /// Makes the geometric interpolation between two scales.
    ///
    /// @param from    The scale at progress 0, finite and above 0.
    /// @param to      The scale at progress 1, likewise.
    /// @param mixOut  Receives the mix; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer or a
    ///         scale outside the above.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMakeScaleMix(double from, double to,
                                                        mtwnScaleMix* mixOut);

    /// Samples a scale mix: from (to / from)^progress, exactly from at 0 and
    /// to at 1; outside 0 to 1 it extends, to infinity or 0 past the
    /// doubles' range.
    ///
    /// @param mix       A mix made by this library, or one whose fields pass
    ///                  its check (this header's opening).
    /// @param progress  The progress, finite.
    /// @param scaleOut  Receives the scale; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, a
    ///         progress not finite, or a mix whose ends are not finite and
    ///         above 0 or whose logarithms are not finite.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMixScale(const mtwnScaleMix* mix, double progress,
                                                    double* scaleOut);

    // A view of a plane: the point at its center and the width it shows.
    typedef struct mtwnZoomView
    {
        double x;
        double y;
        double width;
    } mtwnZoomView;

    // Two views ready to interpolate by van Wijk and Nuij's smooth zoom and
    // pan. Make one with mtwnMakeZoomMix; its fields are the library's.
    typedef struct mtwnZoomMix
    {
        mtwnZoomView from;
        mtwnZoomView to;
        double rho;
        double length;
        double r0;
        double coshR0;
        double panScale;
        mtwnScaleMix widths;
    } mtwnZoomMix;

    /// Makes the zoom and pan between two views that van Wijk and Nuij
    /// found optimal ("Smooth and efficient zooming and panning", 2003):
    /// it zooms out as far as the pan needs and back in, so the eye can
    /// follow; with the centers equal it is a geometric zoom.
    ///
    /// @param from    The view at progress 0: finite, its width above 0.
    /// @param to      The view at progress 1, likewise.
    /// @param rho     How much it zooms out against panning, finite and
    ///                above 0: the paper's measured sqrt(2) (d3's default),
    ///                1.42 (Mapbox's); toward 0 it pans with little zoom.
    /// @param mixOut  Receives the mix; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, a view
    ///         or rho outside the above, or views whose path leaves the
    ///         doubles' range (widths, or a pan against the width, about
    ///         1e150 times apart).
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMakeZoomMix(mtwnZoomView from, mtwnZoomView to,
                                                       double rho, mtwnZoomMix* mixOut);

    /// Samples a zoom mix: exactly from at 0 and to at 1, between them the
    /// path at that share of its length; outside 0 to 1 it extends, its
    /// width going to 0 as its center settles on a point either way (an
    /// infinity where that point is past the doubles' range), never NaN.
    ///
    /// @param mix       A mix made by this library, or one whose fields pass
    ///                  its check (this header's opening).
    /// @param progress  The progress, finite.
    /// @param viewOut   Receives the view; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, a
    ///         progress not finite, or a mix outside its check: views as
    ///         mtwnMakeZoomMix takes them with a finite difference between
    ///         their centers, rho finite and above 0, a length finite and
    ///         not below 0, r0 within 700 of 0, cosh r0 finite and at
    ///         least 1, a finite pan's scale, and with a pan's scale of 0
    ///         widths whose scale mix passes its check and runs from one
    ///         view's width to the other's.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMixZoom(const mtwnZoomMix* mix, double progress,
                                                   mtwnZoomView* viewOut);

    /// A zoom mix's path length, the paper's S: a zoom by a factor of e with
    /// no pan is 1 / rho long. The measure a duration is taken from (d3
    /// takes S rho / sqrt(2) seconds); 0 for equal views.
    ///
    /// @param mix        A mix made by this library, or one whose fields
    ///                   pass its check (this header's opening).
    /// @param lengthOut  Receives the length; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer or a mix
    ///         mtwnMixZoom refuses.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnComputeZoomLength(const mtwnZoomMix* mix,
                                                             double* lengthOut);

    /// Interpolates an integer: the interpolation in double, rounded to
    /// nearest with ties away from zero, so it passes through every integer
    /// between. Exact at the ends, and between them for values within 2^53.
    ///
    /// @param from      The value at progress 0.
    /// @param to        The value at progress 1.
    /// @param progress  The progress, finite; outside 0 to 1 extends,
    ///                  clamped to the type's range.
    /// @param valueOut  Receives the value; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null valueOut or a
    ///         progress not finite.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMixInteger(int64_t from, int64_t to, double progress,
                                                      int64_t* valueOut);

    /// Interpolates a discrete value the host indexes (an enumeration, a
    /// flag): from below progress 1/2, to from it, as Web Animations
    /// steps values it cannot interpolate.
    ///
    /// @param from      The value at progress 0.
    /// @param to        The value at progress 1.
    /// @param progress  The progress, finite.
    /// @param valueOut  Receives from or to; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null valueOut or a
    ///         progress not finite.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMixDiscrete(uint32_t from, uint32_t to, double progress,
                                                       uint32_t* valueOut);

    // A punch: a sine fading out over a duration, sin(2 pi f t) (1 - t /
    // duration)^exponent. Make one with mtwnMakePunch; its fields are the
    // maker's arguments, and every call taking one refuses fields the maker
    // would.
    typedef struct mtwnPunch
    {
        double frequency;
        double duration;
        double exponent;
    } mtwnPunch;

    // A shake: gradient noise fading out over the same envelope, the same
    // for a seed on every platform. Make one with mtwnMakeShake; its fields
    // are the maker's arguments, and every call taking one refuses fields
    // the maker would.
    typedef struct mtwnShake
    {
        double frequency;
        double duration;
        double exponent;
        uint64_t seed;
    } mtwnShake;

    /// Makes a punch.
    ///
    /// @param frequency  Cycles a second, finite and above 0 (DOTween's
    ///                   vibrato over the duration).
    /// @param duration   Seconds, finite and above 0.
    /// @param exponent   The envelope's, finite and above 0: 1 fades
    ///                   linearly, above 1 sooner, below 1 later.
    /// @param punchOut   Receives the punch; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer or a
    ///         parameter outside the above.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMakePunch(double frequency, double duration,
                                                     double exponent, mtwnPunch* punchOut);

    /// Samples a punch: a factor from -1 to 1 the host scales by each
    /// channel's amplitude and adds to the value; exactly 0 at 0, at the
    /// duration and outside them.
    ///
    /// @param punch      A punch made by this library.
    /// @param seconds    The time since its start, not NaN.
    /// @param factorOut  Receives the factor; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, a punch
    ///         whose fields mtwnMakePunch would refuse, or a NaN time.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnSamplePunch(const mtwnPunch* punch, double seconds,
                                                       double* factorOut);

    /// Makes a shake.
    ///
    /// @param frequency  Lattice points a second, finite and above 0: how
    ///                   often the noise changes direction.
    /// @param duration   Seconds, finite and above 0, with the frequency
    ///                   fewer than 2^53 lattice points.
    /// @param exponent   The envelope's, finite and above 0.
    /// @param seed       Any value; the same seed shakes the same way.
    /// @param shakeOut   Receives the shake; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer or a
    ///         parameter outside the above.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnMakeShake(double frequency, double duration,
                                                     double exponent, uint64_t seed,
                                                     mtwnShake* shakeOut);

    /// Samples a shake for one channel: a factor from -1 to 1, each
    /// channel its own noise; exactly 0 at 0, at the duration and outside
    /// them.
    ///
    /// @param shake      A shake made by this library.
    /// @param channel    Which channel, any value.
    /// @param seconds    The time since its start, not NaN.
    /// @param factorOut  Receives the factor; unchanged on failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, a shake
    ///         whose fields mtwnMakeShake would refuse, or a NaN time.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnSampleShake(const mtwnShake* shake, uint32_t channel,
                                                       double seconds, double* factorOut);

    /// Samples a shake's noise alone, its envelope left to the host:
    /// for a shake driven by "trauma" (Eiserloh's camera shake:
    /// an amount the host raises at each hit and lets decay, the shake its
    /// square), on a clock that runs on through the hits, so the noise never
    /// jumps where the amplitude changes. Each channel its own noise.
    ///
    /// @param shake      A shake made by this library; its duration and
    ///                   exponent are checked but not used.
    /// @param channel    Which channel, any value.
    /// @param seconds    The clock, from 0 while the frequency times it is
    ///                   below 2^53; not NaN.
    /// @param factorOut  Receives a factor from -1 to 1: 0 on the lattice
    ///                   points and outside the clock's range; unchanged on
    ///                   failure.
    /// @return mtwn_success; mtwn_errorInvalid for a null pointer, a shake
    ///         whose fields mtwnMakeShake would refuse, or a NaN clock.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_NODISCARD MTWN_API mtwnResult mtwnSampleShakeNoise(const mtwnShake* shake,
                                                            uint32_t channel, double seconds,
                                                            double* factorOut);

#ifdef __cplusplus
}
#endif

#endif
