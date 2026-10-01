// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The exponential, sine and cosine that springs need, and the logarithm,
// power and cube root that colors need, written here so that they give
// the same bits on every platform (record mui-0001): only arithmetic,
// floor, sqrt and the exact scaling of a double by a power of two.

#ifndef MAUL_UI_SRC_MOTION_MATH_H
#define MAUL_UI_SRC_MOTION_MATH_H

// e to the x, within a few units in the last place. Underflows to 0
// below -745 and overflows to infinity above ln(DBL_MAX).
double muiExp(double x);

// The sine and cosine of x, within 1e-14 for |x| up to 2000 and exact
// reduction up to 2^20; larger arguments lose accuracy. A spring's argument
// stays far below.
void muiSinCos(double x, double* sineOut, double* cosineOut);

// The natural logarithm of x, within a few units in the last place: -inf
// for 0, NaN below 0, inf for inf.
double muiLog(double x);

// x to the y for x above 0, as e^(y ln x): within 2e-15 relative while
// |y ln x| stays below 10, as the sRGB transfer function's does.
double muiPow(double x, double y);

// The real cube root of x, of either sign, within a unit in the last
// place.
double muiCbrt(double x);

#endif // MAUL_UI_SRC_MOTION_MATH_H
