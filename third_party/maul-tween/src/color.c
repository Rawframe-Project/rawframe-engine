// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Colors as CSS Color 4 converts and interpolates them,
// in double: sRGB's transfer extended to
// negative values by reflection, linear sRGB to LMS and back by one
// matrix each (src/generated/color_matrices.h), the signed cube root to
// Oklab, OkLCh with its hue in radians. A mix holds both ends converted and
// premultiplied, so a sample costs one conversion back.

#include "bounds_internal.h"
#include "color_internal.h"
#include "elementary.h"
#include "fpenv.h"
#include "mix_internal.h"
#include "work.h"

#include "generated/color_matrices.h"
#include "generated/math_constants.h"
#include "maul-tween/interpolate.h"

#include <math.h>
#include <stdbool.h>

// Where an OkLCh hue is powerless (conversions.js's epsilon).
static const double s_powerlessChroma = 0.000004;
// A turn and half a turn, in radians.
static const double s_turn = 4.0 * MTWN_PIO2_HI;
static const double s_halfTurn = 2.0 * MTWN_PIO2_HI;

static double ToLinear(double value)
{
    double magnitude = fabs(value);
    if (magnitude <= 0.04045)
    {
        return value / 12.92;
    }
    return copysign(mtwnPow((magnitude + 0.055) / 1.055, 2.4), value);
}

static double ToGamma(double value)
{
    double magnitude = fabs(value);
    if (magnitude > 0.0031308)
    {
        return copysign(1.055 * mtwnPow(magnitude, 1.0 / 2.4) - 0.055, value);
    }
    return 12.92 * value;
}

static void Multiply(const double m[3][3], const double in[3], double out[3])
{
    for (int i = 0; i < 3; ++i)
    {
        out[i] = m[i][0] * in[0] + m[i][1] * in[1] + m[i][2] * in[2];
    }
}

// A color's coordinates in a space, alpha last.
static void ToSpace(mtwnColor color, mtwnColorSpace space, double out[4])
{
    double gamma[3] = {(double)color.red, (double)color.green, (double)color.blue};
    out[3] = (double)color.alpha;
    if (space == mtwn_colorSrgb)
    {
        out[0] = gamma[0];
        out[1] = gamma[1];
        out[2] = gamma[2];
        return;
    }
    double linear[3] = {ToLinear(gamma[0]), ToLinear(gamma[1]), ToLinear(gamma[2])};
    if (space == mtwn_colorLinearSrgb)
    {
        out[0] = linear[0];
        out[1] = linear[1];
        out[2] = linear[2];
        return;
    }
    double lms[3];
    Multiply(s_linearSrgbToLms, linear, lms);
    double roots[3] = {mtwnCbrt(lms[0]), mtwnCbrt(lms[1]), mtwnCbrt(lms[2])};
    Multiply(s_lmsToOklab, roots, out);
    if (space == mtwn_colorOklch)
    {
        double a = out[1];
        double b = out[2];
        double chroma = sqrt(a * a + b * b);
        double hue = mtwnAtan2(b, a);
        hue = hue < 0.0 ? hue + s_turn : hue;
        out[1] = chroma;
        out[2] = chroma <= s_powerlessChroma ? (double)NAN : hue;
    }
}

// Coordinates in a space back to a color, alpha clamped.
static mtwnColor FromSpace(const double in[4], mtwnColorSpace space)
{
    double gamma[3] = {in[0], in[1], in[2]};
    if (space != mtwn_colorSrgb)
    {
        double linear[3] = {in[0], in[1], in[2]};
        if (space != mtwn_colorLinearSrgb)
        {
            double lab[3] = {in[0], in[1], in[2]};
            if (space == mtwn_colorOklch)
            {
                double hue = isnan(in[2]) ? 0.0 : in[2];
                double sine = 0.0;
                double cosine = 0.0;
                mtwnSinCos(hue, &sine, &cosine);
                lab[1] = in[1] * cosine;
                lab[2] = in[1] * sine;
            }
            double roots[3];
            Multiply(s_oklabToLms, lab, roots);
            double lms[3] = {roots[0] * roots[0] * roots[0], roots[1] * roots[1] * roots[1],
                             roots[2] * roots[2] * roots[2]};
            Multiply(s_lmsToLinearSrgb, lms, linear);
        }
        gamma[0] = ToGamma(linear[0]);
        gamma[1] = ToGamma(linear[1]);
        gamma[2] = ToGamma(linear[2]);
    }
    double alpha = in[3] < 0.0 ? 0.0 : (in[3] > 1.0 ? 1.0 : in[3]);
    // Held within the floats as a target holds a color.
    mtwnVec4 held = mtwnNumbersHeld(
        (mtwnVec4){(float)gamma[0], (float)gamma[1], (float)gamma[2], (float)alpha});
    return (mtwnColor){held.x, held.y, held.z, held.w};
}

// CSS Color 4's hue fix-ups, in radians, on the two ends' hues.
static void FixHues(mtwnHueMethod method, double* first, double* second)
{
    double difference = *second - *first;
    switch (method)
    {
    case mtwn_hueShorter:
        if (difference > s_halfTurn)
        {
            *first += s_turn;
        }
        else if (difference < -s_halfTurn)
        {
            *second += s_turn;
        }
        break;
    case mtwn_hueLonger:
        if (difference > 0.0 && difference < s_halfTurn)
        {
            *first += s_turn;
        }
        else if (difference > -s_halfTurn && difference <= 0.0)
        {
            *second += s_turn;
        }
        break;
    case mtwn_hueIncreasing:
        if (*second < *first)
        {
            *second += s_turn;
        }
        break;
    default:
        if (*first < *second)
        {
            *first += s_turn;
        }
        break;
    }
}

MTWN_NOINLINE mtwnResult mtwnMakeColorMixWork(mtwnColor from, mtwnColor to, mtwnColorSpace space,
                                              mtwnHueMethod hue, mtwnColorMix* mixOut)
{
    if (mixOut == NULL || !mtwnIsColorSpace(space) || hue > mtwn_hueDecreasing ||
        !mtwnIsColor(from) || !mtwnIsColor(to))
    {
        return mtwn_errorInvalid;
    }
    mtwnColorMix mix = {{0.0}, {0.0}, from, to, space};
    ToSpace(from, space, mix.from);
    ToSpace(to, space, mix.to);
    if (space == mtwn_colorOklch)
    {
        // A missing hue takes the other end's; both missing, both are 0.
        bool fromMissing = isnan(mix.from[2]);
        bool toMissing = isnan(mix.to[2]);
        mix.from[2] = fromMissing ? (toMissing ? 0.0 : mix.to[2]) : mix.from[2];
        mix.to[2] = toMissing ? mix.from[2] : mix.to[2];
        FixHues(hue, &mix.from[2], &mix.to[2]);
    }
    for (int i = 0; i < 3; ++i)
    {
        if (space != mtwn_colorOklch || i != 2)
        {
            mix.from[i] *= mix.from[3];
            mix.to[i] *= mix.to[3];
        }
    }
    *mixOut = mix;
    return mtwn_success;
}

mtwnResult mtwnMakeColorMix(mtwnColor from, mtwnColor to, mtwnColorSpace space, mtwnHueMethod hue,
                            mtwnColorMix* mixOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnMakeColorMixWork(from, to, space, hue, mixOut);
    mtwnFloatEnvLeave(env);
    return result;
}

MTWN_NOINLINE mtwnColor mtwnMixColorWork(const mtwnColorMix* mix, double progress)
{
    if (progress == 0.0)
    {
        return mix->fromColor;
    }
    if (progress == 1.0)
    {
        return mix->toColor;
    }
    double at[4];
    for (int i = 0; i < 4; ++i)
    {
        at[i] = mtwnMix(mix->from[i], mix->to[i], progress);
    }
    if (at[3] > 0.0)
    {
        for (int i = 0; i < 3; ++i)
        {
            if (mix->space != mtwn_colorOklch || i != 2)
            {
                at[i] /= at[3];
            }
        }
    }
    return FromSpace(at, mix->space);
}

MTWN_NOINLINE mtwnResult mtwnConvertColorToSpaceWork(mtwnColor color, mtwnColorSpace space,
                                                     double coordinatesOut[4])
{
    if (coordinatesOut == NULL || !mtwnIsColorSpace(space) || !mtwnIsColor(color))
    {
        return mtwn_errorInvalid;
    }
    ToSpace(color, space, coordinatesOut);
    return mtwn_success;
}

mtwnResult mtwnConvertColorToSpace(mtwnColor color, mtwnColorSpace space, double coordinatesOut[4])
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnConvertColorToSpaceWork(color, space, coordinatesOut);
    mtwnFloatEnvLeave(env);
    return result;
}

static MTWN_NOINLINE mtwnResult mtwnConvertColorFromSpaceWork(const double coordinates[4],
                                                              mtwnColorSpace space,
                                                              mtwnColor* colorOut)
{
    if (coordinates == NULL || colorOut == NULL || !mtwnIsColorSpace(space))
    {
        return mtwn_errorInvalid;
    }
    for (int i = 0; i < 4; ++i)
    {
        bool missingHue = space == mtwn_colorOklch && i == 2 && isnan(coordinates[i]);
        if (!isfinite(coordinates[i]) && !missingHue)
        {
            return mtwn_errorInvalid;
        }
    }
    *colorOut = FromSpace(coordinates, space);
    return mtwn_success;
}

mtwnResult mtwnConvertColorFromSpace(const double coordinates[4], mtwnColorSpace space,
                                     mtwnColor* colorOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnConvertColorFromSpaceWork(coordinates, space, colorOut);
    mtwnFloatEnvLeave(env);
    return result;
}
