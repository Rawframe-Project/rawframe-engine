// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// sRGB's transfer function (IEC 61966, part 2.1) and Oklab's matrices from
// sRGB's linear light (Ottosson, 2020), in doubles with the library's own
// power and cube root, so that every platform gives the same bits.

#include "color.h"

#include "motion_math.h"

#include <math.h>

static double ToLinear(double c)
{
    return c <= 0.04045 ? c / 12.92 : muiPow((c + 0.055) / 1.055, 2.4);
}

static double FromLinear(double c)
{
    return c <= 0.0031308 ? 12.92 * c : 1.055 * muiPow(c, 1.0 / 2.4) - 0.055;
}

static double Unit(double value)
{
    return fmin(fmax(value, 0.0), 1.0);
}

void muiColorToChannels(muiColor color, float channelsOut[4])
{
    double r = ToLinear((double)color.r);
    double g = ToLinear((double)color.g);
    double b = ToLinear((double)color.b);
    double l = muiCbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    double m = muiCbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    double s = muiCbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    double alpha = (double)color.a;
    channelsOut[0] = (float)(alpha * (0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s));
    channelsOut[1] = (float)(alpha * (1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s));
    channelsOut[2] = (float)(alpha * (0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s));
    channelsOut[3] = color.a;
}

muiColor muiColorFromChannels(const float channels[4])
{
    double alpha = Unit((double)channels[3]);
    if (alpha == 0.0)
    {
        return (muiColor){0.0f, 0.0f, 0.0f, 0.0f};
    }
    double lightness = (double)channels[0] / alpha;
    double a = (double)channels[1] / alpha;
    double b = (double)channels[2] / alpha;
    double l = lightness + 0.3963377774 * a + 0.2158037573 * b;
    double m = lightness - 0.1055613458 * a - 0.0638541728 * b;
    double s = lightness - 0.0894841775 * a - 1.2914855480 * b;
    l = l * l * l;
    m = m * m * m;
    s = s * s * s;
    // Linear light below 0 takes the transfer function's linear part, and
    // the result is held to the gamut after it.
    double red = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
    double green = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
    double blue = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
    return (muiColor){(float)Unit(FromLinear(red)), (float)Unit(FromLinear(green)),
                      (float)Unit(FromLinear(blue)), (float)alpha};
}

void muiColorToLinearRgb(muiColor color, double rgbOut[3])
{
    rgbOut[0] = ToLinear((double)color.r);
    rgbOut[1] = ToLinear((double)color.g);
    rgbOut[2] = ToLinear((double)color.b);
}

muiLinearColor muiPremultiply(const double rgb[3], float alpha, float opacity)
{
    double scale = (double)alpha * (double)opacity;
    return (muiLinearColor){(float)(rgb[0] * scale), (float)(rgb[1] * scale),
                            (float)(rgb[2] * scale), (float)scale};
}
