// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// COLR version 1 composite modes (record mui-0006). Porter-Duff: co = cs
// Fa + cb Fb, ao = as Fa + ab Fb, premultiplied. Plus adds, held at 1:
// the specification leaves it unclamped, but a premultiplied channel
// past 1 would make every later source over wrong. A blend mode blends
// the colours unpremultiplied and composites source over: co = cs (1 -
// ab) + cb (1 - as) + as ab B(Cb, Cs), ao = as + ab - as ab. Surfaces
// are in linear light, as all of the painter's compositing is.

#include "colr_composite.h"

#include <math.h>

// COLR's CompositeMode numbers.
enum
{
    CLEAR,
    SRC,
    DEST,
    SRC_OVER,
    DEST_OVER,
    SRC_IN,
    DEST_IN,
    SRC_OUT,
    DEST_OUT,
    SRC_ATOP,
    DEST_ATOP,
    XOR,
    PLUS,
    SCREEN,
    OVERLAY,
    DARKEN,
    LIGHTEN,
    COLOR_DODGE,
    COLOR_BURN,
    HARD_LIGHT,
    SOFT_LIGHT,
    DIFFERENCE,
    EXCLUSION,
    MULTIPLY,
    HUE,
    SATURATION,
    COLOR,
    LUMINOSITY
};

// A Porter-Duff operator's factor: 0, 1, an alpha or one less it.
typedef enum Factor
{
    ZERO,
    ONE,
    SOURCE_ALPHA,
    BACKDROP_ALPHA,
    ONE_LESS_SOURCE,
    ONE_LESS_BACKDROP
} Factor;

// Fa and Fb of the operators clear to xor.
static const Factor PORTER_DUFF[12][2] = {
    {ZERO, ZERO},
    {ONE, ZERO},
    {ZERO, ONE},
    {ONE, ONE_LESS_SOURCE},
    {ONE_LESS_BACKDROP, ONE},
    {BACKDROP_ALPHA, ZERO},
    {ZERO, SOURCE_ALPHA},
    {ONE_LESS_BACKDROP, ZERO},
    {ZERO, ONE_LESS_SOURCE},
    {BACKDROP_ALPHA, ONE_LESS_SOURCE},
    {ONE_LESS_BACKDROP, SOURCE_ALPHA},
    {ONE_LESS_BACKDROP, ONE_LESS_SOURCE},
};

static float FactorOf(Factor factor, float sourceAlpha, float backdropAlpha)
{
    switch (factor)
    {
    case ZERO:
        return 0.0f;
    case ONE:
        return 1.0f;
    case SOURCE_ALPHA:
        return sourceAlpha;
    case BACKDROP_ALPHA:
        return backdropAlpha;
    case ONE_LESS_SOURCE:
        return 1.0f - sourceAlpha;
    default:
        return 1.0f - backdropAlpha;
    }
}

static float Multiply(float b, float s)
{
    return b * s;
}

static float Screen(float b, float s)
{
    return b + s - b * s;
}

static float HardLight(float b, float s)
{
    return s <= 0.5f ? Multiply(b, 2.0f * s) : Screen(b, 2.0f * s - 1.0f);
}

static float SoftLight(float b, float s)
{
    if (s <= 0.5f)
    {
        return b - (1.0f - 2.0f * s) * b * (1.0f - b);
    }
    float d = b <= 0.25f ? ((16.0f * b - 12.0f) * b + 4.0f) * b : sqrtf(b);
    return b + (2.0f * s - 1.0f) * (d - b);
}

static float ColorDodge(float b, float s)
{
    if (b == 0.0f)
    {
        return 0.0f;
    }
    return s == 1.0f ? 1.0f : fminf(1.0f, b / (1.0f - s));
}

static float ColorBurn(float b, float s)
{
    if (b == 1.0f)
    {
        return 1.0f;
    }
    return s == 0.0f ? 0.0f : 1.0f - fminf(1.0f, (1.0f - b) / s);
}

// A separable mode's blend of one channel.
static float Separable(uint32_t mode, float b, float s)
{
    switch (mode)
    {
    case SCREEN:
        return Screen(b, s);
    case OVERLAY:
        return HardLight(s, b);
    case DARKEN:
        return fminf(b, s);
    case LIGHTEN:
        return fmaxf(b, s);
    case COLOR_DODGE:
        return ColorDodge(b, s);
    case COLOR_BURN:
        return ColorBurn(b, s);
    case HARD_LIGHT:
        return HardLight(b, s);
    case SOFT_LIGHT:
        return SoftLight(b, s);
    case DIFFERENCE:
        return fabsf(b - s);
    case EXCLUSION:
        return b + s - 2.0f * b * s;
    default:
        return Multiply(b, s);
    }
}

static float Lum(const float c[3])
{
    return 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2];
}

static void ClipColor(float c[3])
{
    float l = Lum(c);
    float n = fminf(fminf(c[0], c[1]), c[2]);
    float x = fmaxf(fmaxf(c[0], c[1]), c[2]);
    for (int i = 0; i < 3 && n < 0.0f; i++)
    {
        c[i] = l + (c[i] - l) * l / (l - n);
    }
    for (int i = 0; i < 3 && x > 1.0f; i++)
    {
        c[i] = l + (c[i] - l) * (1.0f - l) / (x - l);
    }
}

static void SetLum(float c[3], float l)
{
    float d = l - Lum(c);
    for (int i = 0; i < 3; i++)
    {
        c[i] += d;
    }
    ClipColor(c);
}

static float Sat(const float c[3])
{
    return fmaxf(fmaxf(c[0], c[1]), c[2]) - fminf(fminf(c[0], c[1]), c[2]);
}

static void SetSat(float c[3], float s)
{
    // The channels' order, smallest first.
    int lo = 0;
    int mid = 1;
    int hi = 2;
    if (c[lo] > c[mid])
    {
        int t = lo;
        lo = mid;
        mid = t;
    }
    if (c[mid] > c[hi])
    {
        int t = mid;
        mid = hi;
        hi = t;
    }
    if (c[lo] > c[mid])
    {
        int t = lo;
        lo = mid;
        mid = t;
    }
    if (c[hi] > c[lo])
    {
        c[mid] = (c[mid] - c[lo]) * s / (c[hi] - c[lo]);
        c[hi] = s;
    }
    else
    {
        c[mid] = 0.0f;
        c[hi] = 0.0f;
    }
    c[lo] = 0.0f;
}

// A non-separable mode's blend of the three channels into out.
static void NonSeparable(uint32_t mode, const float b[3], const float s[3], float out[3])
{
    const float* base = mode == HUE || mode == COLOR ? s : b;
    for (int i = 0; i < 3; i++)
    {
        out[i] = base[i];
    }
    if (mode == HUE)
    {
        SetSat(out, Sat(b));
    }
    else if (mode == SATURATION)
    {
        SetSat(out, Sat(s));
    }
    SetLum(out, mode == LUMINOSITY ? Lum(s) : Lum(b));
}

// A blend mode on one pixel: the colours unpremultiplied, blended, and
// composited source over.
static void Blend(uint32_t mode, const float* s, float* b)
{
    float as = s[3];
    float ab = b[3];
    float source[3];
    float backdrop[3];
    for (int i = 0; i < 3; i++)
    {
        source[i] = as > 0.0f ? s[i] / as : 0.0f;
        backdrop[i] = ab > 0.0f ? b[i] / ab : 0.0f;
    }
    float blended[3];
    if (mode >= HUE)
    {
        NonSeparable(mode, backdrop, source, blended);
    }
    else
    {
        for (int i = 0; i < 3; i++)
        {
            blended[i] = Separable(mode, backdrop[i], source[i]);
        }
    }
    for (int i = 0; i < 3; i++)
    {
        b[i] = s[i] * (1.0f - ab) + b[i] * (1.0f - as) + as * ab * blended[i];
    }
    b[3] = as + ab - as * ab;
}

void muiComposite(uint32_t mode, const float* source, float* backdrop, size_t count)
{
    mode = mode > LUMINOSITY ? SRC_OVER : mode;
    for (size_t p = 0; p < count; p++)
    {
        const float* s = &source[p * 4];
        float* b = &backdrop[p * 4];
        if (mode == PLUS)
        {
            for (int i = 0; i < 4; i++)
            {
                b[i] = fminf(1.0f, s[i] + b[i]);
            }
        }
        else if (mode < PLUS)
        {
            float fa = FactorOf(PORTER_DUFF[mode][0], s[3], b[3]);
            float fb = FactorOf(PORTER_DUFF[mode][1], s[3], b[3]);
            for (int i = 0; i < 4; i++)
            {
                b[i] = s[i] * fa + b[i] * fb;
            }
        }
        else
        {
            Blend(mode, s, b);
        }
    }
}
