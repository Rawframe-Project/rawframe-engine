// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The binary32 entry points of the library's own elementary functions
// (a family file), for callers that compute in float: the names of
// src/elementary.h with an F suffix, computed in binary32 arithmetic
// alone so that vectorized callers keep their width, with the same
// bits on every platform. Each states its range and the accuracy its
// tests hold it to, against the C library's binary64 functions.

#ifndef MAUL_TWEEN_SRC_ELEMENTARY_FLOAT_H
#define MAUL_TWEEN_SRC_ELEMENTARY_FLOAT_H

// e to the x, within 1 ulp for results in the normal range. Overflows
// to infinity above ln(FLT_MAX), is 0 below ln(2^-150), and NaN for NaN;
// results in the subnormal range are rounded twice.
float mtwnExpF(float x);

// 2 to the x, within 1 ulp for results in the normal range, and exact
// for every integer x whose power is a float. Overflows to infinity from
// 128, is 0 below -150, and NaN for NaN.
float mtwnExp2F(float x);

// The natural logarithm of x, within 1 ulp: -infinity for 0, NaN below 0
// and for NaN, infinity for infinity, exactly 0 for 1.
float mtwnLogF(float x);

// The real cube root of x, of either sign, within 0.6 ulp, and exact
// where the root is a float; 0, infinity and NaN return themselves.
float mtwnCbrtF(float x);

// x to the y for x >= 0, within 1 ulp while |y ln x| <= 64 and 1.2 ulp
// over the whole range; exact where the power is a float the
// logarithm's pair resolves, as 2^k and 10^k for k up to 10. C's values
// where x or y is 0, 1 or infinite; NaN for a negative x.
float mtwnPowF(float x, float y);

// The sine and cosine of x together, each within 1 ulp for every finite
// x: below 1.5 2^16 the reduction by pi / 2 uses seven parts of it, past
// that Payne and Hanek's, from 2 / pi's bits, exact for any float. Both
// are a quiet NaN with its sign clear for an infinite or NaN x.
void mtwnSinCosF(float x, float* sineOut, float* cosineOut);

// The angle of the point (x, y) from the positive x axis, in radians
// from -pi to pi, within 1 ulp, with the C standard's values for zeros
// and infinities of either sign.
float mtwnAtan2F(float y, float x);

#endif // MAUL_TWEEN_SRC_ELEMENTARY_FLOAT_H
