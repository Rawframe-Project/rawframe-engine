// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The exponential reduces x to r = x - k ln 2 with |r| <= ln 2 / 2 by
// Cody and Waite's split of ln 2; e^r is its Taylor series to degree
// 13, whose remainder is under 4e-18 relative there. The logarithm
// writes x = 2^k m with m in [sqrt(1/2), sqrt(2)), f = m - 1 exactly,
// s = f / (2 + f), and ln m = f - f^2/2 + s (f^2/2 + R(s^2)), R the
// atanh series to s^22, whose remainder is under 1e-17 relative for
// |s| <= 0.172. Sine and cosine reduce x to r = x - k pi / 2 with
// |r| <= pi / 4 by a three-part split of pi / 2, keeping r as a pair of
// doubles, and sum their Taylor series to degrees 17 and 18. The
// arctangent reduces to z in [0, 1], then to u = (z - c) / (1 + z c)
// with c the nearest multiple of 1/8, |u| <= 1/16, and sums its series
// to u^13, whose remainder is under 1e-18 relative, onto atan(c). In
// each, the leading terms are added exactly as pairs of doubles (Knuth's
// two-sum, Dekker's product) and the result is rounded once, so the
// error stays near half an ulp.

#include "elementary.h"

#include "generated/math_constants.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// The largest x whose exponential is finite, the double just below
// ln(DBL_MAX), and the smallest whose exponential rounds above 0.
static const double s_expOverflow = 0x1.62e42fefa39efp+9;
static const double s_expUnderflow = -0x1.74910d52d3051p+9;
// sqrt(1/2), where the logarithm moves its mantissa into the upper half.
static const double s_sqrtHalf = 0x1.6a09e667f3bcdp-1;

// Evaluates c[0] + c[1] x + ... + c[count - 1] x^(count - 1) by Horner's
// rule.
static double Polynomial(const double* c, int count, double x)
{
    double sum = c[count - 1];
    for (int i = count - 2; i >= 0; --i)
    {
        sum = sum * x + c[i];
    }
    return sum;
}

// a + b = sum + *errorOut exactly, whatever their magnitudes (Knuth).
static double TwoSum(double a, double b, double* errorOut)
{
    double sum = a + b;
    double bVirtual = sum - a;
    double aVirtual = sum - bVirtual;
    *errorOut = (a - aVirtual) + (b - bVirtual);
    return sum;
}

// a + b = sum + *errorOut exactly when |a| >= |b| (Dekker).
static double FastTwoSum(double a, double b, double* errorOut)
{
    double sum = a + b;
    *errorOut = b - (sum - a);
    return sum;
}

// a times b = product + *errorOut exactly, for operands whose product and
// halves stay in range (Veltkamp's split, Dekker's product).
static double TwoProduct(double a, double b, double* errorOut)
{
    double product = a * b;
    double aSplit = a * 134217729.0;
    double aHigh = aSplit - (aSplit - a);
    double aLow = a - aHigh;
    double bSplit = b * 134217729.0;
    double bHigh = bSplit - (bSplit - b);
    double bLow = b - bHigh;
    *errorOut = ((aHigh * bHigh - product) + aHigh * bLow + aLow * bHigh) + aLow * bLow;
    return product;
}

// e to the x + xLow, for xLow far below an ulp of x, within the range
// where the result is finite and above 0.
static double ExpPair(double x, double xLow)
{
    double k = floor(x * MTWN_INV_LN2 + 0.5);
    // x - k times the high part is exact; r + rLow is the reduced
    // argument to well beyond double precision.
    double reduced = x - k * MTWN_LN2_HI;
    double rLow = 0.0;
    double r = TwoSum(reduced, -k * MTWN_LN2_LO, &rLow);
    rLow += xLow;
    // e^r = 1 + r + r^2 (1/2! + r / 3! + ... + r^11 / 13!), the 1 + r
    // added exactly; e^(r + rLow) = e^r (1 + rLow) to first order, and
    // rLow e^r matters when rLow carries a caller's low part.
    double tail = r * r * Polynomial(s_expTaylor, 12, r);
    double sumLow = 0.0;
    double sum = FastTwoSum(1.0, r, &sumLow);
    double scaledLow = rLow + rLow * (r + tail);
    return ldexp(sum + (sumLow + (scaledLow + tail)), (int)k);
}

double mtwnExp(double x)
{
    if (isnan(x))
    {
        return x;
    }
    if (x > s_expOverflow)
    {
        return HUGE_VAL;
    }
    if (x < s_expUnderflow)
    {
        return 0.0;
    }
    return ExpPair(x, 0.0);
}

double mtwnExp2(double x)
{
    if (isnan(x))
    {
        return x;
    }
    if (x >= 1024.0)
    {
        return HUGE_VAL;
    }
    if (x < -1075.0)
    {
        return 0.0;
    }
    double k = floor(x + 0.5);
    // Exact: x and k are within one half of each other.
    double f = x - k;
    // 2^f = 1 + f ln 2 + f^2 ((ln 2)^2 / 2! + ... + f^11 (ln 2)^13 / 13!),
    // the 1 + f ln 2 added exactly, ln 2 carried in two parts.
    double productLow = 0.0;
    double product = TwoProduct(f, MTWN_LN2, &productLow);
    productLow += f * MTWN_LN2_TAIL;
    double tail = f * f * Polynomial(s_exp2Taylor + 1, 12, f);
    double sumLow = 0.0;
    double sum = FastTwoSum(1.0, product, &sumLow);
    return ldexp(sum + (sumLow + (productLow + tail)), (int)k);
}

// The natural logarithm of a finite x above 0, as a pair: *lowOut
// carries what the returned double cannot, a few ulps of it at most.
static double LogPair(double x, double* lowOut)
{
    int e = 0;
    double m = frexp(x, &e);
    if (m < s_sqrtHalf)
    {
        m *= 2.0;
        e -= 1;
    }
    // Exact by Sterbenz's lemma, m being within a factor of two of 1.
    double f = m - 1.0;
    // s = f / (2 + f) as a pair, from 2 + f kept exactly as a pair.
    double denominatorLow = 0.0;
    double denominator = FastTwoSum(2.0, f, &denominatorLow);
    double s = f / denominator;
    double backLow = 0.0;
    double back = TwoProduct(s, denominator, &backLow);
    double sLow = ((f - back) - backLow - s * denominatorLow) / denominator;
    double z = s * s;
    double r = z * Polynomial(s_logSeries, 11, z);
    // f^2 / 2 exactly as a pair, then f - f^2/2 exactly as a pair.
    double squareLow = 0.0;
    double halfSquare = 0.5 * TwoProduct(f, f, &squareLow);
    squareLow *= 0.5;
    double headLow = 0.0;
    double head = FastTwoSum(f, -halfSquare, &headLow);
    // s (f^2/2 + R), up to 4 percent of the result near m = sqrt(2), as a
    // pair too.
    double innerLow = 0.0;
    double inner = TwoSum(halfSquare, r, &innerLow);
    innerLow += squareLow;
    double termLow = 0.0;
    double term = TwoProduct(s, inner, &termLow);
    termLow += s * innerLow + sLow * inner;
    // k times MTWN_LN2_HI is exact for every exponent a double has; adding
    // it to the head exactly keeps the cancellation near x = 1/2 exact.
    double k = (double)e;
    double totalLow = 0.0;
    double total = TwoSum(k * MTWN_LN2_HI, head, &totalLow);
    // The term is too large to round with the low parts, so it joins the
    // high part exactly too; what is left is a few ulps of the high part
    // at most, which the exponential takes as a low part.
    double withTermLow = 0.0;
    double withTerm = TwoSum(total, term, &withTermLow);
    *lowOut = (totalLow + withTermLow) + (headLow + (termLow - squareLow) + k * MTWN_LN2_LO);
    return withTerm;
}

double mtwnLog(double x)
{
    if (isnan(x) || x < 0.0)
    {
        return (double)NAN;
    }
    if (x == 0.0)
    {
        return -HUGE_VAL;
    }
    if (isinf(x))
    {
        return x;
    }
    double low = 0.0;
    double high = LogPair(x, &low);
    return high + low;
}

double mtwnCbrt(double x)
{
    if (!isfinite(x) || x == 0.0)
    {
        return x;
    }
    // |x| = m 2^e with e a multiple of 3 and m in [1/2, 4).
    int e = 0;
    double m = frexp(fabs(x), &e);
    int rest = e % 3;
    if (rest < 0)
    {
        rest += 3;
    }
    m = ldexp(m, rest);
    e -= rest;
    // Three steps of Halley's iteration, which cubes the relative error,
    // take it from 13 percent at a line through the range to about 1e-20;
    // then one Newton step from the residual m - y^3, computed exactly,
    // squares it and rounds y once.
    double y = 0.6 + 0.25 * m;
    for (int i = 0; i < 3; ++i)
    {
        double cube = y * y * y;
        y = y * (cube + 2.0 * m) / (2.0 * cube + m);
    }
    double squareLow = 0.0;
    double square = TwoProduct(y, y, &squareLow);
    double cubeLow = 0.0;
    double cube = TwoProduct(square, y, &cubeLow);
    cubeLow += squareLow * y;
    // m - cube is exact (Sterbenz): they agree to many digits.
    double residual = (m - cube) - cubeLow;
    y += residual / (3.0 * square);
    return copysign(ldexp(y, e / 3), x);
}

// pow where x or y is 0, 1 or infinite, or x is negative: true, with the
// result in *resultOut; false for every other pair.
static bool PowSpecial(double x, double y, double* resultOut)
{
    if (y == 0.0 || x == 1.0)
    {
        *resultOut = 1.0;
    }
    else if (isnan(x) || isnan(y) || x < 0.0)
    {
        *resultOut = (double)NAN;
    }
    else if (x == 0.0 || isinf(x))
    {
        // 0 to a positive power and infinity to a negative one are 0.
        *resultOut = (x == 0.0) == (y > 0.0) ? 0.0 : HUGE_VAL;
    }
    else if (isinf(y))
    {
        *resultOut = (x > 1.0) == (y > 0.0) ? HUGE_VAL : 0.0;
    }
    else
    {
        return false;
    }
    return true;
}

double mtwnPow(double x, double y)
{
    double special = 0.0;
    if (PowSpecial(x, y, &special))
    {
        return special;
    }
    // e^(y ln x), with ln x and the product carried as pairs.
    double logLow = 0.0;
    double logHigh = LogPair(x, &logLow);
    double product = y * logHigh;
    if (product > s_expOverflow)
    {
        return HUGE_VAL;
    }
    if (product < s_expUnderflow)
    {
        return 0.0;
    }
    double productLow = 0.0;
    product = TwoProduct(y, logHigh, &productLow);
    productLow += y * logLow;
    return ExpPair(product, productLow);
}

// Below this, sine is x and cosine is 1 to within half an ulp.
static const double s_tinyAngle = 0x1.0p-27;

// The sine and cosine of r + rLow for |r| <= pi / 4 and rLow far below
// an ulp of r.
static void SinCosKernel(double r, double rLow, double* sineOut, double* cosineOut)
{
    double zLow = 0.0;
    double z = TwoProduct(r, r, &zLow);
    // sin = r + r^3 (-1/3! + z / 5! - ...), with rLow's first-order term.
    double sineTail = r * z * Polynomial(s_sinTaylor, 8, z);
    *sineOut = r + (sineTail + rLow * (1.0 - 0.5 * z));
    // cos = 1 - z / 2 + z^2 (1/4! - z / 6! + ...), 1 - z / 2 exactly as a
    // pair, minus rLow's first-order term r rLow.
    double halfLow = 0.5 * zLow;
    double headLow = 0.0;
    double head = FastTwoSum(1.0, -0.5 * z, &headLow);
    double cosineTail = z * z * Polynomial(s_cosTaylor, 8, z);
    *cosineOut = head + (headLow - halfLow + (cosineTail - r * rLow));
}

// From here k reaches 2^20 and the reduction by three parts of pi / 2
// stops being exact; past it ReduceFar takes over.
static const double s_farAngle = 0x1.8p+20;

// A quiet NaN with its sign clear, the same bits on every target (an
// arithmetic NaN's sign differs between x86 and ARM).
static double QuietNan(void)
{
    const uint64_t bits = 0x7FF8000000000000u;
    double out = 0.0;
    memcpy(&out, &bits, sizeof out);
    return out;
}

// 2^e for a normal result.
static double PowerOfTwo(int e)
{
    uint64_t bits = (uint64_t)(e + 1023) << 52;
    double out = 0.0;
    memcpy(&out, &bits, sizeof out);
    return out;
}

// a b as a 128-bit product: its low word, the high in *highOut.
static uint64_t Multiply64(uint64_t a, uint64_t b, uint64_t* highOut)
{
    uint64_t aLow = a & 0xFFFFFFFFu;
    uint64_t aHigh = a >> 32;
    uint64_t bLow = b & 0xFFFFFFFFu;
    uint64_t bHigh = b >> 32;
    uint64_t lowLow = aLow * bLow;
    uint64_t lowHigh = aLow * bHigh;
    uint64_t highLow = aHigh * bLow;
    uint64_t middle = (lowLow >> 32) + (lowHigh & 0xFFFFFFFFu) + (highLow & 0xFFFFFFFFu);
    *highOut = aHigh * bHigh + (lowHigh >> 32) + (highLow >> 32) + (middle >> 32);
    return middle << 32 | (lowLow & 0xFFFFFFFFu);
}

// The 64 bits of 2 / pi from bit `from` on (bit 1 the first after the
// point), the first of them the most significant.
static uint64_t TwoOverPiBits(int from)
{
    int word = (from - 1) / 64;
    int offset = (from - 1) % 64;
    uint64_t out = s_twoOverPi[word] << offset;
    return offset == 0 ? out : out | s_twoOverPi[word + 1] >> (64 - offset);
}

// The 64 bits of p (five words, least significant first, the last 0)
// from bit `from` up, for -64 < from < 256; bits below
// 0 read as 0.
static uint64_t ProductBits(const uint64_t* p, int from)
{
    if (from < 0)
    {
        return p[0] << -from;
    }
    int word = from / 64;
    int offset = from % 64;
    return offset == 0 ? p[word] : p[word] >> offset | p[word + 1] << (64 - offset);
}

// Payne and Hanek's reduction for x >= s_farAngle, exact for every
// finite double: x 2 / pi = n + f with |f| <= 1/2, from the window of
// 192 bits of 2 / pi that the bits of x reach (those before it make
// multiples of 4, those after it change f by under 2^-137). Returns n
// modulo 4; r + rLow is f pi / 2.
static uint32_t ReduceFar(double x, double* rOut, double* rLowOut)
{
    uint64_t bits = 0;
    memcpy(&bits, &x, sizeof bits);
    uint64_t mantissa = (bits & 0xFFFFFFFFFFFFFu) | 0x10000000000000u;
    int e = (int)(bits >> 52) - 1075; // x = mantissa 2^e
    int start = e > 2 ? e - 1 : 1;
    // x (2 / pi) = mantissa C 2^-point, C the window from bit start.
    int point = start + 191 - e;
    uint64_t c2 = TwoOverPiBits(start);
    uint64_t c1 = TwoOverPiBits(start + 64);
    uint64_t c0 = TwoOverPiBits(start + 128);
    uint64_t h0 = 0;
    uint64_t h1 = 0;
    uint64_t h2 = 0;
    uint64_t l0 = Multiply64(mantissa, c0, &h0);
    uint64_t l1 = Multiply64(mantissa, c1, &h1);
    uint64_t l2 = Multiply64(mantissa, c2, &h2);
    uint64_t p[5];
    p[0] = l0;
    p[1] = h0 + l1;
    uint64_t carry = p[1] < h0;
    uint64_t sum = h1 + carry;
    carry = sum < h1;
    p[2] = sum + l2;
    carry += p[2] < l2;
    p[3] = h2 + carry;
    p[4] = 0;
    uint32_t n = (uint32_t)(ProductBits(p, point) & 3u);
    uint64_t high = ProductBits(p, point - 64);
    uint64_t low = ProductBits(p, point - 128);
    // f at or past 1/2 rounds n up and is taken as f - 1.
    bool negative = (high >> 63) != 0;
    if (negative)
    {
        n = (n + 1) & 3u;
        low = ~low + 1;
        high = ~high + (low == 0 ? 1u : 0u);
    }
    // Normalized, the fraction's top 53 bits and the rest.
    int shift = 0;
    while (shift < 128 && (high >> 63) == 0)
    {
        high = high << 1 | low >> 63;
        low <<= 1;
        shift++;
    }
    double fraction = (double)(high >> 11) * PowerOfTwo(-53 - shift);
    double fractionLow =
        (double)(high & 0x7FFu) * PowerOfTwo(-64 - shift) + (double)low * PowerOfTwo(-128 - shift);
    double productLow = 0.0;
    double product = TwoProduct(fraction, MTWN_PIO2_HI, &productLow);
    productLow += fraction * MTWN_PIO2_LO + fractionLow * MTWN_PIO2_HI;
    double r = FastTwoSum(product, productLow, rLowOut);
    *rOut = negative ? -r : r;
    *rLowOut = negative ? -*rLowOut : *rLowOut;
    return n;
}

void mtwnSinCos(double x, double* sineOut, double* cosineOut)
{
    if (!isfinite(x))
    {
        *sineOut = QuietNan();
        *cosineOut = QuietNan();
        return;
    }
    if (fabs(x) < s_tinyAngle)
    {
        *sineOut = x;
        *cosineOut = 1.0;
        return;
    }
    double r = 0.0;
    double rLow = 0.0;
    uint32_t quadrant = 0;
    if (fabs(x) < s_farAngle)
    {
        double k = floor(x * MTWN_INV_PIO2 + 0.5);
        // x - k p1 is exact (Sterbenz), k p2 and k p3 are exact products
        // for |k| < 2^20, and the sums carry their errors.
        double first = x - k * MTWN_PIO2_1;
        double secondLow = 0.0;
        double second = TwoSum(first, -k * MTWN_PIO2_2, &secondLow);
        r = FastTwoSum(second, secondLow - k * MTWN_PIO2_3, &rLow);
        // The quadrant, from k modulo 4 computed exactly for any k.
        double kModulo4 = fmod(k, 4.0);
        quadrant = (uint32_t)(kModulo4 < 0.0 ? kModulo4 + 4.0 : kModulo4);
    }
    else
    {
        // -x reduces to -n and -f.
        quadrant = ReduceFar(fabs(x), &r, &rLow);
        if (x < 0.0)
        {
            quadrant = (4u - quadrant) & 3u;
            r = -r;
            rLow = -rLow;
        }
    }
    double sine = 0.0;
    double cosine = 0.0;
    SinCosKernel(r, rLow, &sine, &cosine);
    switch (quadrant)
    {
    case 0:
        *sineOut = sine;
        *cosineOut = cosine;
        break;
    case 1:
        *sineOut = cosine;
        *cosineOut = -sine;
        break;
    case 2:
        *sineOut = -sine;
        *cosineOut = -cosine;
        break;
    default:
        *sineOut = -cosine;
        *cosineOut = sine;
        break;
    }
}

// atan(z + zLow) for z in [0, 1], as a pair: *lowOut carries what the
// returned double cannot.
static double ArctanPair(double z, double zLow, double* lowOut)
{
    double index = floor(z * 8.0 + 0.5);
    double c = index * 0.125;
    // z - c is exact (Sterbenz, or c = 0); 1 + z c is kept as a pair.
    double numerator = z - c;
    double productLow = 0.0;
    double product = TwoProduct(z, c, &productLow);
    double denominatorLow = 0.0;
    double denominator = FastTwoSum(1.0, product, &denominatorLow);
    denominatorLow += productLow;
    double u = numerator / denominator;
    double backLow = 0.0;
    double back = TwoProduct(u, denominator, &backLow);
    double uLow = ((numerator - back) - backLow - u * denominatorLow) / denominator;
    // zLow moves atan by zLow / (1 + z^2) to first order.
    uLow += zLow / (1.0 + z * z);
    double v = u * u;
    double tail = u * v * Polynomial(s_atanSeries, 6, v);
    int i = (int)index;
    double sumLow = 0.0;
    double sum = TwoSum(s_atanEighths[i], u, &sumLow);
    *lowOut = sumLow + (s_atanEighthsLow[i] + uLow + tail);
    return sum;
}

// The C standard's atan2 where x or y is zero or infinite: true, with the
// angle in *angleOut; false for every other pair.
static bool Atan2Special(double y, double x, double* angleOut)
{
    double ax = fabs(x);
    double ay = fabs(y);
    double angle = 0.0;
    if (isinf(ax) || isinf(ay))
    {
        // Infinities of both, of y alone, or of x alone.
        double quarter = MTWN_PIO2_HI * 0.5;
        angle = isinf(ay) ? (isinf(ax) ? quarter : MTWN_PIO2_HI) : 0.0;
        if (signbit(x))
        {
            angle = isinf(ay) ? (isinf(ax) ? 3.0 * quarter : MTWN_PIO2_HI) : MTWN_PI_HI;
        }
    }
    else if (ay == 0.0)
    {
        angle = signbit(x) ? MTWN_PI_HI : 0.0;
    }
    else if (ax == 0.0)
    {
        angle = MTWN_PIO2_HI;
    }
    else
    {
        return false;
    }
    *angleOut = copysign(angle, y);
    return true;
}

double mtwnAtan2(double y, double x)
{
    if (isnan(x) || isnan(y))
    {
        return (double)NAN;
    }
    double special = 0.0;
    if (Atan2Special(y, x, &special))
    {
        return special;
    }
    double ax = fabs(x);
    double ay = fabs(y);
    // The ratio of the smaller to the larger, as a pair, then its angle.
    // Both are first scaled by one power of two, which leaves the angle as
    // it is and keeps Dekker's product in range for any finite pair.
    double small = ay <= ax ? ay : ax;
    double large = ay <= ax ? ax : ay;
    int exponent = 0;
    large = frexp(large, &exponent);
    small = ldexp(small, -exponent);
    double z = small / large;
    double backLow = 0.0;
    double back = TwoProduct(z, large, &backLow);
    double zLow = ((small - back) - backLow) / large;
    double angleLow = 0.0;
    double angle = ArctanPair(z, zLow, &angleLow);
    if (ay > ax)
    {
        double low = 0.0;
        angle = TwoSum(MTWN_PIO2_HI, -angle, &low);
        angleLow = low + (MTWN_PIO2_LO - angleLow);
    }
    if (signbit(x))
    {
        double low = 0.0;
        angle = TwoSum(MTWN_PI_HI, -angle, &low);
        angleLow = low + (MTWN_PI_LO - angleLow);
    }
    return copysign(angle + angleLow, y);
}
