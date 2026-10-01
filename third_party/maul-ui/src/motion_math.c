// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The exponential and the trigonometry reduce their argument by a
// constant split in two parts, the first with few enough bits that its
// product with the quotient is exact (Cody and Waite; the splits are
// fdlibm's), and evaluate a Taylor polynomial on what is left, whose next
// term is below 1e-17. The logarithm splits off the exponent and sums the
// series of atanh on the mantissa near 1.

#include "motion_math.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

// ln 2 = LN2_HIGH + LN2_LOW; LN2_HIGH has 32 significant bits.
#define LN2_HIGH 6.93147180369123816490e-01
#define LN2_LOW  1.90821492927058770002e-10
#define INV_LN2  1.44269504088896338700e+00

// pi / 2 = HALF_PI_HIGH + HALF_PI_LOW; HALF_PI_HIGH has 33 significant
// bits.
#define HALF_PI_HIGH 1.57079632673412561417e+00
#define HALF_PI_LOW  6.07710050650619224932e-11
#define INV_HALF_PI  6.36619772367581382433e-01

// 2^k for k from -1022 to 1023, built from its bits: exact.
static double PowerOfTwo(int k)
{
    uint64_t bits = (uint64_t)(k + 1023) << 52;
    double value = 0.0;
    memcpy(&value, &bits, sizeof value);
    return value;
}

// e^r for |r| <= ln 2 / 2, by Horner's rule on the series to r^13 / 13!.
static double ExpReduced(double r)
{
    double sum = 1.0;
    for (int n = 13; n >= 1; n--)
    {
        sum = 1.0 + sum * r / (double)n;
    }
    return sum;
}

double muiExp(double x)
{
    if (isnan(x))
    {
        return x;
    }
    // Past ln(DBL_MAX) the result is not a finite double.
    if (x > 7.09782712893383973096e+02)
    {
        return (double)INFINITY;
    }
    if (x < -745.0)
    {
        return 0.0;
    }
    double k = floor(x * INV_LN2 + 0.5);
    double r = (x - k * LN2_HIGH) - k * LN2_LOW;
    double value = ExpReduced(r);
    int exponent = (int)k;
    // Scaled in two steps where 2^k alone is not a normal double.
    if (exponent < -1021)
    {
        return value * PowerOfTwo(exponent + 1000) * PowerOfTwo(-1000);
    }
    if (exponent > 1023)
    {
        return value * PowerOfTwo(exponent - 1) * 2.0;
    }
    return value * PowerOfTwo(exponent);
}

// sin r and cos r for |r| <= pi / 4, on the series to r^15 / 15! and
// r^16 / 16!.
static void SinCosReduced(double r, double* sineOut, double* cosineOut)
{
    double square = r * r;
    double sine = 1.0;
    double cosine = 1.0;
    for (int n = 16; n >= 2; n -= 2)
    {
        cosine = 1.0 - cosine * square / (double)(n * (n - 1));
        sine = 1.0 - sine * square / (double)((n + 1) * n);
    }
    *sineOut = sine * r;
    *cosineOut = cosine;
}

void muiSinCos(double x, double* sineOut, double* cosineOut)
{
    double k = floor(x * INV_HALF_PI + 0.5);
    double r = (x - k * HALF_PI_HIGH) - k * HALF_PI_LOW;
    double sine = 0.0;
    double cosine = 0.0;
    SinCosReduced(r, &sine, &cosine);
    // The quadrant, from k modulo 4, rotates the reduced pair.
    double quarter = k - 4.0 * floor(k / 4.0);
    switch ((int)quarter)
    {
    case 1:
        *sineOut = cosine;
        *cosineOut = -sine;
        break;
    case 2:
        *sineOut = -sine;
        *cosineOut = -cosine;
        break;
    case 3:
        *sineOut = -cosine;
        *cosineOut = sine;
        break;
    default:
        *sineOut = sine;
        *cosineOut = cosine;
        break;
    }
}

double muiLog(double x)
{
    if (isnan(x) || x < 0.0)
    {
        return (double)NAN;
    }
    if (x == 0.0)
    {
        return -(double)INFINITY;
    }
    if (isinf(x))
    {
        return x;
    }
    int exponent = 0;
    // A subnormal is scaled to a normal first.
    if (x < 2.2250738585072014e-308)
    {
        x *= PowerOfTwo(54);
        exponent = -54;
    }
    uint64_t bits = 0;
    memcpy(&bits, &x, sizeof bits);
    exponent += (int)((bits >> 52) & 0x7FF) - 1023;
    // The mantissa in [1, 2), then in [sqrt(1/2), sqrt(2)).
    bits = (bits & 0x000FFFFFFFFFFFFFull) | 0x3FF0000000000000ull;
    double m = 0.0;
    memcpy(&m, &bits, sizeof m);
    if (m > 1.41421356237309504880)
    {
        m *= 0.5;
        exponent++;
    }
    // ln m = 2 atanh(s) = 2 (s + s^3 / 3 + s^5 / 5 + ...) with s below
    // 0.172, so s^2 is below 0.0295 and the term past s^25 is below 1e-17.
    double f = m - 1.0;
    double s = f / (2.0 + f);
    double s2 = s * s;
    double sum = 0.0;
    for (int n = 12; n >= 1; n--)
    {
        sum = s2 * (1.0 / (double)(2 * n + 1) + sum);
    }
    double k = (double)exponent;
    return k * LN2_HIGH + (2.0 * s * (1.0 + sum) + k * LN2_LOW);
}

double muiPow(double x, double y)
{
    return muiExp(y * muiLog(x));
}

double muiCbrt(double x)
{
    if (x == 0.0 || !isfinite(x))
    {
        return x;
    }
    double a = fabs(x);
    double y = muiExp(muiLog(a) / 3.0);
    // One Newton step on y^3 = a corrects the guess's last bits, written
    // as a small correction so that its own rounding barely counts.
    y += (a / (y * y) - y) / 3.0;
    return x < 0.0 ? -y : y;
}
