// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A path segment's arc length and its inverse, apart
// from the segments themselves (path.c).

#ifndef MAUL_TWEEN_SRC_PATH_LENGTH_H
#define MAUL_TWEEN_SRC_PATH_LENGTH_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

// A segment as a cubic in its parameter u: value a + u (b + u (c + u d)),
// or for a step the value alone.
typedef struct mtwnCubic
{
    double a[4];
    double b[4];
    double c[4];
    double d[4];
    bool step;
} mtwnCubic;

// The root of a sum of squares: fabs, the identity there, shows the
// compiler the root is never of a negative, so it calls no errno path.
static inline double mtwnRootOfSquares(double sum)
{
    return sqrt(fabs(sum));
}

// Where the speed is least inside (0, 1): v.a, half the speed squared's
// derivative, rising through 0. A turn back along a line puts a kink in
// the speed there, and a rule whose points all miss the span between two
// turns sees one polynomial on either side and agrees on it; a length is
// split at these, as QUADPACK's qagp splits at the points it is given.
typedef struct mtwnMinima
{
    double at[2];
    int count;
} mtwnMinima;

// The speed's minima inside (0, 1), none for one channel.
mtwnMinima mtwnSpeedMinima(const mtwnCubic* cubic, uint8_t channels);

// The arc length of the cubic from a to b, a < b, split at its minima.
double mtwnArcLength(const mtwnCubic* cubic, uint8_t channels, const mtwnMinima* minima, double a,
                     double b);

// The parameter at a length into a segment of that total.
double mtwnParameterAt(const mtwnCubic* cubic, uint8_t channels, double target, double total);

#endif // MAUL_TWEEN_SRC_PATH_LENGTH_H
