// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The methods of src/elementary.c in binary32: the exponential reduces
// x to r = x - k ln 2 with |r| <= ln 2 / 2 by a split of ln 2 whose
// high part has 16 bits, and sums its Taylor series to degree 7, whose
// remainder is under 6e-9 relative there; the logarithm writes
// x = 2^k m with m in [sqrt(1/2), sqrt(2)) and sums the atanh series of
// s = (m - 1) / (m + 1) to s^11, whose remainder is under 2^-34
// relative, as pow needs: it multiplies the logarithm's error by
// |y ln x|.
// Sine and cosine reduce by pi / 2 held in seven parts, six of 8 bits
// whose products with k are exact for |k| < 2^16, about 72 bits in all,
// as floats within 2^-27.8 of a multiple of pi / 2 need; they sum their
// Taylor series to degrees 9 and 10; the arctangent reduces around atan(k / 8) to |u| <= 1/16 and
// sums its series to u^7. The leading terms are added exactly as pairs
// of floats, so the result is rounded once.

#include "elementary_float.h"

#include "generated/math_constants_float.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// sqrt(1/2), where the logarithm moves its mantissa into the upper half.
static const float s_sqrtHalfF = 0x1.6a09e6p-1f;

// Evaluates c[0] + c[1] x + ... + c[count - 1] x^(count - 1) by Horner's
// rule.
static float PolynomialF(const float* c, int count, float x)
{
    float sum = c[count - 1];
    for (int i = count - 2; i >= 0; --i)
    {
        sum = sum * x + c[i];
    }
    return sum;
}

// a + b = sum + *errorOut exactly, whatever their magnitudes (Knuth).
static float TwoSumF(float a, float b, float* errorOut)
{
    float sum = a + b;
    float bVirtual = sum - a;
    float aVirtual = sum - bVirtual;
    *errorOut = (a - aVirtual) + (b - bVirtual);
    return sum;
}

// a + b = sum + *errorOut exactly when |a| >= |b| (Dekker).
static float FastTwoSumF(float a, float b, float* errorOut)
{
    float sum = a + b;
    *errorOut = b - (sum - a);
    return sum;
}

// a times b = product + *errorOut exactly, for operands whose product and
// halves stay in range (Veltkamp's split at 2^12 + 1, Dekker's product).
static float TwoProductF(float a, float b, float* errorOut)
{
    float product = a * b;
    float aSplit = a * 4097.0f;
    float aHigh = aSplit - (aSplit - a);
    float aLow = a - aHigh;
    float bSplit = b * 4097.0f;
    float bHigh = bSplit - (bSplit - b);
    float bLow = b - bHigh;
    *errorOut = ((aHigh * bHigh - product) + aHigh * bLow + aLow * bHigh) + aLow * bLow;
    return product;
}

// e to the x + xLow, for xLow a few ulps of x at most, within the range
// where the result is finite and above 0.
static float ExpPairF(float x, float xLow)
{
    float k = floorf(x * MTWN_INV_LN2_F + 0.5f);
    // x - k times the high part is exact for |k| < 2^8; r + rLow is the
    // reduced argument to beyond float precision.
    float reduced = x - k * MTWN_LN2_HI_F;
    float rLow = 0.0f;
    float r = TwoSumF(reduced, -k * MTWN_LN2_LO_F, &rLow);
    rLow += xLow;
    // e^r = 1 + r + r^2 (1/2! + ... + r^5 / 7!), the 1 + r added exactly;
    // e^(r + rLow) = e^r (1 + rLow) to first order.
    float tail = r * r * PolynomialF(s_expTaylorF, 6, r);
    float sumLow = 0.0f;
    float sum = FastTwoSumF(1.0f, r, &sumLow);
    float scaledLow = rLow + rLow * (r + tail);
    return ldexpf(sum + (sumLow + (scaledLow + tail)), (int)k);
}

float mtwnExpF(float x)
{
    if (isnan(x))
    {
        return x;
    }
    if (x > MTWN_EXP_OVERFLOW_F)
    {
        return HUGE_VALF;
    }
    if (x < MTWN_EXP_UNDERFLOW_F)
    {
        return 0.0f;
    }
    return ExpPairF(x, 0.0f);
}

float mtwnExp2F(float x)
{
    if (isnan(x))
    {
        return x;
    }
    if (x >= 128.0f)
    {
        return HUGE_VALF;
    }
    if (x < -150.0f)
    {
        return 0.0f;
    }
    float k = floorf(x + 0.5f);
    // Exact: x and k are within one half of each other.
    float f = x - k;
    // 2^f = 1 + f ln 2 + f^2 ((ln 2)^2 / 2! + ... + f^5 (ln 2)^7 / 7!), the
    // 1 + f ln 2 added exactly, ln 2 carried in two parts.
    float productLow = 0.0f;
    float product = TwoProductF(f, MTWN_LN2_F, &productLow);
    productLow += f * MTWN_LN2_TAIL_F;
    float tail = f * f * PolynomialF(s_exp2TaylorF + 1, 6, f);
    float sumLow = 0.0f;
    float sum = FastTwoSumF(1.0f, product, &sumLow);
    return ldexpf(sum + (sumLow + (productLow + tail)), (int)k);
}

// The natural logarithm of a finite x above 0, as a pair: *lowOut
// carries what the returned float cannot, a few ulps of it at most.
static float LogPairF(float x, float* lowOut)
{
    int e = 0;
    float m = frexpf(x, &e);
    if (m < s_sqrtHalfF)
    {
        m *= 2.0f;
        e -= 1;
    }
    // Exact by Sterbenz's lemma, m being within a factor of two of 1.
    float f = m - 1.0f;
    // s = f / (2 + f) as a pair, from 2 + f kept exactly as a pair.
    float denominatorLow = 0.0f;
    float denominator = FastTwoSumF(2.0f, f, &denominatorLow);
    float s = f / denominator;
    float backLow = 0.0f;
    float back = TwoProductF(s, denominator, &backLow);
    float sLow = ((f - back) - backLow - s * denominatorLow) / denominator;
    // R = 2 z / 3 + z^2 (2/5 + ...), z = s^2: its leading term carried as
    // a pair, from z exact as a pair and 2/3 in two parts, since pow
    // multiplies the logarithm's error by |y ln x|.
    float zLow = 0.0f;
    float z = TwoProductF(s, s, &zLow);
    zLow += 2.0f * s * sLow;
    float rLow = 0.0f;
    float r = TwoProductF(s_logSeriesF[0], z, &rLow);
    rLow += s_logSeriesF[0] * zLow + MTWN_TWO_THIRDS_LO_F * z;
    // The higher terms are 2 percent of R, too large for its low part.
    float higherLow = 0.0f;
    r = TwoSumF(r, z * z * PolynomialF(s_logSeriesF + 1, 4, z), &higherLow);
    rLow += higherLow;
    // f - f^2/2 + s (f^2/2 + R), the leading terms as exact pairs.
    float squareLow = 0.0f;
    float halfSquare = 0.5f * TwoProductF(f, f, &squareLow);
    squareLow *= 0.5f;
    float headLow = 0.0f;
    float head = FastTwoSumF(f, -halfSquare, &headLow);
    float innerLow = 0.0f;
    float inner = TwoSumF(halfSquare, r, &innerLow);
    innerLow += squareLow + rLow;
    float termLow = 0.0f;
    float term = TwoProductF(s, inner, &termLow);
    termLow += s * innerLow + sLow * inner;
    // k times MTWN_LN2_HI_F is exact for every exponent a float has.
    float k = (float)e;
    float totalLow = 0.0f;
    float total = TwoSumF(k * MTWN_LN2_HI_F, head, &totalLow);
    float withTermLow = 0.0f;
    float withTerm = TwoSumF(total, term, &withTermLow);
    *lowOut = (totalLow + withTermLow) + (headLow + (termLow - squareLow) + k * MTWN_LN2_LO_F);
    return withTerm;
}

float mtwnLogF(float x)
{
    if (isnan(x) || x < 0.0f)
    {
        return NAN;
    }
    if (x == 0.0f)
    {
        return -HUGE_VALF;
    }
    if (isinf(x))
    {
        return x;
    }
    float low = 0.0f;
    float high = LogPairF(x, &low);
    return high + low;
}

float mtwnCbrtF(float x)
{
    if (!isfinite(x) || x == 0.0f)
    {
        return x;
    }
    // |x| = m 2^e with e a multiple of 3 and m in [1/2, 4).
    int e = 0;
    float m = frexpf(fabsf(x), &e);
    int rest = e % 3;
    if (rest < 0)
    {
        rest += 3;
    }
    m = ldexpf(m, rest);
    e -= rest;
    // Two steps of Halley's iteration take the relative error from 13
    // percent at a line through the range to about 1e-8; one Newton step
    // from the residual m - y^3, computed exactly, squares it and rounds y
    // once.
    float y = 0.6f + 0.25f * m;
    for (int i = 0; i < 2; ++i)
    {
        float cube = y * y * y;
        y = y * (cube + 2.0f * m) / (2.0f * cube + m);
    }
    float squareLow = 0.0f;
    float square = TwoProductF(y, y, &squareLow);
    float cubeLow = 0.0f;
    float cube = TwoProductF(square, y, &cubeLow);
    cubeLow += squareLow * y;
    // m - cube is exact (Sterbenz): they agree to many digits.
    float residual = (m - cube) - cubeLow;
    y += residual / (3.0f * square);
    return copysignf(ldexpf(y, e / 3), x);
}

// pow where x or y is 0, 1 or infinite, or x is negative: true, with the
// result in *resultOut; false for every other pair.
static bool PowSpecialF(float x, float y, float* resultOut)
{
    if (y == 0.0f || x == 1.0f)
    {
        *resultOut = 1.0f;
    }
    else if (isnan(x) || isnan(y) || x < 0.0f)
    {
        *resultOut = NAN;
    }
    else if (x == 0.0f || isinf(x))
    {
        // 0 to a positive power and infinity to a negative one are 0.
        *resultOut = (x == 0.0f) == (y > 0.0f) ? 0.0f : HUGE_VALF;
    }
    else if (isinf(y))
    {
        *resultOut = (x > 1.0f) == (y > 0.0f) ? HUGE_VALF : 0.0f;
    }
    else
    {
        return false;
    }
    return true;
}

float mtwnPowF(float x, float y)
{
    float special = 0.0f;
    if (PowSpecialF(x, y, &special))
    {
        return special;
    }
    // e^(y ln x), with ln x and the product carried as pairs.
    float logLow = 0.0f;
    float logHigh = LogPairF(x, &logLow);
    float product = y * logHigh;
    if (product > MTWN_EXP_OVERFLOW_F)
    {
        return HUGE_VALF;
    }
    if (product < MTWN_EXP_UNDERFLOW_F)
    {
        return 0.0f;
    }
    float productLow = 0.0f;
    product = TwoProductF(y, logHigh, &productLow);
    productLow += y * logLow;
    return ExpPairF(product, productLow);
}

// Below this, sine is x and cosine is 1 to within half an ulp.
static const float s_tinyAngleF = 0x1.0p-12f;

// The sine and cosine of r + rLow for |r| <= pi / 4 and rLow far below
// an ulp of r.
static void SinCosKernelF(float r, float rLow, float* sineOut, float* cosineOut)
{
    float zLow = 0.0f;
    float z = TwoProductF(r, r, &zLow);
    float sineTail = r * z * PolynomialF(s_sinTaylorF, 4, z);
    *sineOut = r + (sineTail + rLow * (1.0f - 0.5f * z));
    float halfLow = 0.5f * zLow;
    float headLow = 0.0f;
    float head = FastTwoSumF(1.0f, -0.5f * z, &headLow);
    float cosineTail = z * z * PolynomialF(s_cosTaylorF, 4, z);
    *cosineOut = head + (headLow - halfLow + (cosineTail - r * rLow));
}

// From here k reaches 2^16 and the reduction by parts of pi / 2 stops
// being exact; past it ReduceFarF takes over.
static const float s_farAngleF = 0x1.8p+16f;

// A quiet NaN with its sign clear, the same bits on every target (an
// arithmetic NaN's sign differs between x86 and ARM).
static float QuietNanF(void)
{
    const uint32_t bits = 0x7FC00000u;
    float out = 0.0f;
    memcpy(&out, &bits, sizeof out);
    return out;
}

// 2^e as a double, for a normal result.
static double PowerOfTwoF(int e)
{
    uint64_t bits = (uint64_t)(e + 1023) << 52;
    double out = 0.0;
    memcpy(&out, &bits, sizeof out);
    return out;
}

// a b as a 128-bit product: its low word, the high in *highOut.
static uint64_t Multiply64F(uint64_t a, uint64_t b, uint64_t* highOut)
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
static uint64_t TwoOverPiBitsF(int from)
{
    int word = (from - 1) / 64;
    int offset = (from - 1) % 64;
    uint64_t out = s_twoOverPiF[word] << offset;
    return offset == 0 ? out : out | s_twoOverPiF[word + 1] >> (64 - offset);
}

// The 64 bits of p (four words, least significant first, the last 0)
// from bit `from` up, for -64 < from < 192; bits below
// 0 read as 0.
static uint64_t ProductBitsF(const uint64_t* p, int from)
{
    if (from < 0)
    {
        return p[0] << -from;
    }
    int word = from / 64;
    int offset = from % 64;
    return offset == 0 ? p[word] : p[word] >> offset | p[word + 1] << (64 - offset);
}

// Payne and Hanek's reduction for x >= s_farAngleF, exact for every
// finite float: x 2 / pi = n + f with |f| <= 1/2, from the window of
// 128 bits of 2 / pi that the bits of x reach (those before it make
// multiples of 4, those after it change f by under 2^-101). Returns n
// modulo 4; r + rLow is f pi / 2, from f to 53 bits in binary64.
static uint32_t ReduceFarF(float x, float* rOut, float* rLowOut)
{
    uint32_t bits = 0;
    memcpy(&bits, &x, sizeof bits);
    uint64_t mantissa = (bits & 0x7FFFFFu) | 0x800000u;
    int e = (int)(bits >> 23) - 150; // x = mantissa 2^e
    int start = e > 2 ? e - 1 : 1;
    // x (2 / pi) = mantissa C 2^-point, C the window from bit start.
    int point = start + 127 - e;
    uint64_t c1 = TwoOverPiBitsF(start);
    uint64_t c0 = TwoOverPiBitsF(start + 64);
    uint64_t h0 = 0;
    uint64_t h1 = 0;
    uint64_t p[4];
    p[0] = Multiply64F(mantissa, c0, &h0);
    uint64_t l1 = Multiply64F(mantissa, c1, &h1);
    p[1] = h0 + l1;
    p[2] = h1 + (p[1] < h0 ? 1u : 0u);
    p[3] = 0;
    uint32_t n = (uint32_t)(ProductBitsF(p, point) & 3u);
    uint64_t high = ProductBitsF(p, point - 64);
    uint64_t low = ProductBitsF(p, point - 128);
    // f at or past 1/2 rounds n up and is taken as f - 1.
    bool negative = (high >> 63) != 0;
    if (negative)
    {
        n = (n + 1) & 3u;
        low = ~low + 1;
        high = ~high + (low == 0 ? 1u : 0u);
    }
    // Normalized, the fraction's top 53 bits.
    int shift = 0;
    while (shift < 128 && (high >> 63) == 0)
    {
        high = high << 1 | low >> 63;
        low <<= 1;
        shift++;
    }
    double r = (double)(high >> 11) * PowerOfTwoF(-53 - shift) * MTWN_PIO2_BINARY64_F;
    r = negative ? -r : r;
    *rOut = (float)r;
    *rLowOut = (float)(r - (double)*rOut);
    return n;
}

void mtwnSinCosF(float x, float* sineOut, float* cosineOut)
{
    if (!isfinite(x))
    {
        *sineOut = QuietNanF();
        *cosineOut = QuietNanF();
        return;
    }
    if (fabsf(x) < s_tinyAngleF)
    {
        *sineOut = x;
        *cosineOut = 1.0f;
        return;
    }
    float r = 0.0f;
    float rLow = 0.0f;
    uint32_t quadrant = 0;
    if (fabsf(x) < s_farAngleF)
    {
        float k = floorf(x * MTWN_INV_PIO2_F + 0.5f);
        // x - k p1 is exact (Sterbenz) and each k p an exact product for
        // |k| < 2^16; the sums carry their errors in rLow.
        r = x - k * s_pio2PartsF[0];
        for (int i = 1; i < 6; ++i)
        {
            float error = 0.0f;
            r = TwoSumF(r, -k * s_pio2PartsF[i], &error);
            rLow += error;
        }
        // rLow is a few ulps of r at most, which the kernel takes to
        // first order.
        rLow -= k * s_pio2PartsF[6];
        // The quadrant, from k modulo 4 computed exactly for any k.
        float kModulo4 = fmodf(k, 4.0f);
        quadrant = (uint32_t)(kModulo4 < 0.0f ? kModulo4 + 4.0f : kModulo4);
    }
    else
    {
        // -x reduces to -n and -f.
        quadrant = ReduceFarF(fabsf(x), &r, &rLow);
        if (x < 0.0f)
        {
            quadrant = (4u - quadrant) & 3u;
            r = -r;
            rLow = -rLow;
        }
    }
    float sine = 0.0f;
    float cosine = 0.0f;
    SinCosKernelF(r, rLow, &sine, &cosine);
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
// returned float cannot.
static float ArctanPairF(float z, float zLow, float* lowOut)
{
    float index = floorf(z * 8.0f + 0.5f);
    float c = index * 0.125f;
    // z - c is exact (Sterbenz, or c = 0); 1 + z c is kept as a pair.
    float numerator = z - c;
    float productLow = 0.0f;
    float product = TwoProductF(z, c, &productLow);
    float denominatorLow = 0.0f;
    float denominator = FastTwoSumF(1.0f, product, &denominatorLow);
    denominatorLow += productLow;
    float u = numerator / denominator;
    float backLow = 0.0f;
    float back = TwoProductF(u, denominator, &backLow);
    float uLow = ((numerator - back) - backLow - u * denominatorLow) / denominator;
    // zLow moves atan by zLow / (1 + z^2) to first order.
    uLow += zLow / (1.0f + z * z);
    float v = u * u;
    float tail = u * v * PolynomialF(s_atanSeriesF, 3, v);
    int i = (int)index;
    float sumLow = 0.0f;
    float sum = TwoSumF(s_atanEighthsF[i], u, &sumLow);
    *lowOut = sumLow + (s_atanEighthsLowF[i] + uLow + tail);
    return sum;
}

// The C standard's atan2 where x or y is zero or infinite: true, with the
// angle in *angleOut; false for every other pair.
static bool Atan2SpecialF(float y, float x, float* angleOut)
{
    float ax = fabsf(x);
    float ay = fabsf(y);
    float angle = 0.0f;
    if (isinf(ax) || isinf(ay))
    {
        // Infinities of both, of y alone, or of x alone.
        float quarter = MTWN_PIO2_HI_F * 0.5f;
        angle = isinf(ay) ? (isinf(ax) ? quarter : MTWN_PIO2_HI_F) : 0.0f;
        if (signbit(x))
        {
            angle = isinf(ay) ? (isinf(ax) ? 3.0f * quarter : MTWN_PIO2_HI_F) : MTWN_PI_HI_F;
        }
    }
    else if (ay == 0.0f)
    {
        angle = signbit(x) ? MTWN_PI_HI_F : 0.0f;
    }
    else if (ax == 0.0f)
    {
        angle = MTWN_PIO2_HI_F;
    }
    else
    {
        return false;
    }
    *angleOut = copysignf(angle, y);
    return true;
}

float mtwnAtan2F(float y, float x)
{
    if (isnan(x) || isnan(y))
    {
        return NAN;
    }
    float special = 0.0f;
    if (Atan2SpecialF(y, x, &special))
    {
        return special;
    }
    float ax = fabsf(x);
    float ay = fabsf(y);
    // The ratio of the smaller to the larger, as a pair, then its angle.
    // Both are first scaled by one power of two, which leaves the angle as
    // it is and keeps Dekker's product in range for any finite pair.
    float small = ay <= ax ? ay : ax;
    float large = ay <= ax ? ax : ay;
    int exponent = 0;
    large = frexpf(large, &exponent);
    small = ldexpf(small, -exponent);
    float z = small / large;
    float backLow = 0.0f;
    float back = TwoProductF(z, large, &backLow);
    float zLow = ((small - back) - backLow) / large;
    float angleLow = 0.0f;
    float angle = ArctanPairF(z, zLow, &angleLow);
    if (ay > ax)
    {
        float low = 0.0f;
        angle = TwoSumF(MTWN_PIO2_HI_F, -angle, &low);
        angleLow = low + (MTWN_PIO2_LO_F - angleLow);
    }
    if (signbit(x))
    {
        float low = 0.0f;
        angle = TwoSumF(MTWN_PI_HI_F, -angle, &low);
        angleLow = low + (MTWN_PI_LO_F - angleLow);
    }
    return copysignf(angle + angleLow, y);
}
