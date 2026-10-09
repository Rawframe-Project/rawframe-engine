// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The portable functions (portable_math.h). ln 2 and pi / 2 are split
// into parts whose products with the reduction's integer are exact, so
// the reduced argument is too; the polynomials are Taylor series
// truncated past double's precision on the reduced ranges (|r| up to
// ln 2 / 2 for exp, |f| up to 0.172 for log's atanh series, pi / 4 for
// sine and cosine), evaluated by Horner's rule.

#include "portable_math.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static const double LN2_HI = 6.93147180369123816490e-01;
static const double LN2_LO = 1.90821492927058770002e-10;
static const double INV_LN2 = 1.44269504088896338700e+00;
static const double LN10 = 2.30258509299404568402e+00;
static const double PIO2_1 = 1.57079632673412561417e+00;
static const double PIO2_2 = 6.07710050650619224932e-11;
static const double PIO2_3 = 2.02226624879595063154e-21;
static const double INV_PIO2 = 6.36619772367581382433e-01;

// 2^k for k from -1022 to 1023, built from its bits.
static double Power2(int k)
{
    uint64_t bits = (uint64_t)(k + 1023) << 52;
    double d;
    memcpy(&d, &bits, sizeof(d));
    return d;
}

static double Nearest(double x)
{
    return x < 0.0 ? (double)(int64_t)(x - 0.5) : (double)(int64_t)(x + 0.5);
}

double maudExp(double x)
{
    if (isnan(x))
    {
        return x;
    }
    if (x > 709.0)
    {
        return HUGE_VAL;
    }
    if (x < -708.0)
    {
        return 0.0;
    }
    double k = Nearest(x * INV_LN2);
    double r = (x - k * LN2_HI) - k * LN2_LO;
    // 1/13! down to 1/0!.
    static const double c[14] = {1.0 / 6227020800.0,
                                 1.0 / 479001600.0,
                                 1.0 / 39916800.0,
                                 1.0 / 3628800.0,
                                 1.0 / 362880.0,
                                 1.0 / 40320.0,
                                 1.0 / 5040.0,
                                 1.0 / 720.0,
                                 1.0 / 120.0,
                                 1.0 / 24.0,
                                 1.0 / 6.0,
                                 0.5,
                                 1.0,
                                 1.0};
    double p = c[0];
    for (int i = 1; i < 14; ++i)
    {
        p = p * r + c[i];
    }
    // k may reach 1023 + 1 at the top: two steps keep 2^k normal.
    int half = (int)k / 2;
    return p * Power2(half) * Power2((int)k - half);
}

double maudLog(double x)
{
    if (!(x > 0.0))
    {
        return x == 0.0 ? -HUGE_VAL : (double)NAN;
    }
    if (x == HUGE_VAL)
    {
        return HUGE_VAL;
    }
    int e = 0;
    if (x < 2.2250738585072014e-308)
    {
        // Subnormal: scale into the normal range first.
        x *= 18014398509481984.0;
        e = -54;
    }
    uint64_t bits;
    memcpy(&bits, &x, sizeof(bits));
    e += (int)((bits >> 52) & 0x7FFu) - 1023;
    bits = (bits & 0x000FFFFFFFFFFFFFu) | 0x3FF0000000000000u;
    double m;
    memcpy(&m, &bits, sizeof(m));
    if (m > 1.4142135623730951)
    {
        m *= 0.5;
        e += 1;
    }
    double f = (m - 1.0) / (m + 1.0);
    double f2 = f * f;
    double s = 1.0 / 23.0;
    for (int n = 21; n >= 1; n -= 2)
    {
        s = s * f2 + 1.0 / (double)n;
    }
    return ((double)e * LN2_HI + 2.0 * f * s) + (double)e * LN2_LO;
}

double maudLog10(double x)
{
    return maudLog(x) / LN10;
}

double maudPow10(double x)
{
    return maudExp(x * LN10);
}

double maudPow(double base, double x)
{
    return maudExp(x * maudLog(base));
}

void maudSinCos(double x, double* sine, double* cosine)
{
    if (!(fabs(x) <= 1e6))
    {
        *sine = (double)NAN;
        *cosine = (double)NAN;
        return;
    }
    double k = Nearest(x * INV_PIO2);
    double r = ((x - k * PIO2_1) - k * PIO2_2) - k * PIO2_3;
    double r2 = r * r;
    // sin: 1/17! down to -1/3!; cos: 1/16! down to -1/2!.
    static const double sc[8] = {
        1.0 / 355687428096000.0, -1.0 / 1307674368000.0, 1.0 / 6227020800.0, -1.0 / 39916800.0,
        1.0 / 362880.0,          -1.0 / 5040.0,          1.0 / 120.0,        -1.0 / 6.0};
    static const double cc[8] = {
        1.0 / 20922789888000.0, -1.0 / 87178291200.0, 1.0 / 479001600.0, -1.0 / 3628800.0,
        1.0 / 40320.0,          -1.0 / 720.0,         1.0 / 24.0,        -0.5};
    double ps = 0.0;
    double pc = 0.0;
    for (int i = 0; i < 8; ++i)
    {
        ps = ps * r2 + sc[i];
        pc = pc * r2 + cc[i];
    }
    double s = r + r * r2 * ps;
    double c = 1.0 + r2 * pc;
    switch ((int64_t)k & 3)
    {
    case 0:
        *sine = s;
        *cosine = c;
        break;
    case 1:
        *sine = c;
        *cosine = -s;
        break;
    case 2:
        *sine = -s;
        *cosine = -c;
        break;
    default:
        *sine = -c;
        *cosine = s;
        break;
    }
}
