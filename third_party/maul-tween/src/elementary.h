// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The library's own exponential, logarithm and trigonometry (a family
// file), so that they give the same bits on every platform: only
// arithmetic and the functions IEEE 754 rounds exactly. Each states its
// range and the accuracy its tests hold it to. These take and return
// binary64; binary32 entry points, for callers that compute in float,
// take the same names with an F suffix.

#ifndef MAUL_TWEEN_SRC_ELEMENTARY_H
#define MAUL_TWEEN_SRC_ELEMENTARY_H

// e to the x, within 1 ulp for results in the normal range. Overflows
// to infinity above ln(DBL_MAX), is 0 below ln(2^-1075), and NaN for
// NaN; results in the subnormal range are rounded twice.
double mtwnExp(double x);

// 2 to the x, within 1 ulp for results in the normal range, and exact
// for every integer x whose power is a double. Overflows to infinity
// from 1024, is 0 below -1075, and NaN for NaN.
double mtwnExp2(double x);

// The natural logarithm of x, within 1 ulp: -infinity for 0, NaN below 0
// and for NaN, infinity for infinity, exactly 0 for 1.
double mtwnLog(double x);

// The real cube root of x, of either sign, within 0.6 ulp, and exact
// where the root is a double; 0, infinity and NaN return themselves.
double mtwnCbrt(double x);

// x to the y for x >= 0, within 1 ulp while |y ln x| <= 32, past which
// the error grows with |y ln x| by about 5e-18 of it relative; exact
// where the power is a double the logarithm's pair resolves, as 2^k and
// 10^k for k up to 22. C's values where x or y is 0, 1 or infinite; NaN
// for a negative x, which the library never raises.
double mtwnPow(double x, double y);

// The sine and cosine of x together, each within 1 ulp for every finite
// x: below 1.5 2^20 the reduction by pi / 2 uses three parts of it,
// past that Payne and Hanek's, from 2 / pi's bits, exact for any double.
// Both are a quiet NaN with its sign clear for an infinite or NaN x.
void mtwnSinCos(double x, double* sineOut, double* cosineOut);

// The angle of the point (x, y) from the positive x axis, in radians
// from -pi to pi, within 1 ulp, with the C standard's values for zeros
// and infinities of either sign.
double mtwnAtan2(double y, double x);

#endif // MAUL_TWEEN_SRC_ELEMENTARY_H
