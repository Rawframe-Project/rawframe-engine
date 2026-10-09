// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Transcendental functions that give the same bits on every platform:
// built from correctly rounded operations and exact bit manipulation
// only (Cody-Waite range reduction, then fixed polynomials), so that
// estimates and bakes do not depend on a C library's exp or sin. Within
// 2.2e-16 of the true values in double; about twice glibc's time.

#ifndef MAUL_AUDIO_SRC_PORTABLE_MATH_H
#define MAUL_AUDIO_SRC_PORTABLE_MATH_H

// e^x; 0 below -708, infinity above 709, NaN for NaN.
double maudExp(double x);

// The natural logarithm; -infinity at 0, NaN below.
double maudLog(double x);

// log10(x).
double maudLog10(double x);

// 10^x.
double maudPow10(double x);

// base^x for base above 0, as e^(x log base).
double maudPow(double base, double x);

// The sine and cosine of x, for |x| up to 1e6; NaN beyond.
void maudSinCos(double x, double* sine, double* cosine);

#endif // MAUL_AUDIO_SRC_PORTABLE_MATH_H
