// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Ivanic and Ruedenberg's recursion, in double: order 1 is the rotation
// matrix itself, its rows and columns in ACN order (y, z, x); each later
// order follows from order 1 and the order before by the functions P, U,
// V and W of their paper as corrected.

#include "ambisonic_rotation.h"

#include <math.h>

// One order's matrix, indexed by m and n from -order to order.
typedef struct Band
{
    int order;
    double m[7][7];
} Band;

static double At(const Band* band, int m, int n)
{
    return band->m[m + band->order][n + band->order];
}

// P of the recursion: order 1's row i against the previous order's
// entries around column b.
static double P(const Band* one, const Band* previous, int i, int a, int b)
{
    int l = previous->order + 1;
    if (b == l)
    {
        return At(one, i, 1) * At(previous, a, l - 1) - At(one, i, -1) * At(previous, a, -l + 1);
    }
    if (b == -l)
    {
        return At(one, i, 1) * At(previous, a, -l + 1) + At(one, i, -1) * At(previous, a, l - 1);
    }
    return At(one, i, 0) * At(previous, a, b);
}

static double V(const Band* one, const Band* previous, int m, int n)
{
    if (m == 0)
    {
        return P(one, previous, 1, 1, n) + P(one, previous, -1, -1, n);
    }
    if (m > 0)
    {
        double d = m == 1 ? 1.0 : 0.0;
        return P(one, previous, 1, m - 1, n) * sqrt(1.0 + d) -
               P(one, previous, -1, -m + 1, n) * (1.0 - d);
    }
    double d = m == -1 ? 1.0 : 0.0;
    return P(one, previous, 1, m + 1, n) * (1.0 - d) +
           P(one, previous, -1, -m - 1, n) * sqrt(1.0 + d);
}

static double W(const Band* one, const Band* previous, int m, int n)
{
    if (m > 0)
    {
        return P(one, previous, 1, m + 1, n) + P(one, previous, -1, -m - 1, n);
    }
    return P(one, previous, 1, m - 1, n) - P(one, previous, -1, -m + 1, n);
}

// The entry (m, n) of order l from order 1 and order l - 1.
static double Entry(const Band* one, const Band* previous, int l, int m, int n)
{
    int am = m < 0 ? -m : m;
    double d = m == 0 ? 1.0 : 0.0;
    double denominator =
        (n < 0 ? -n : n) == l ? (double)(2 * l * (2 * l - 1)) : (double)((l + n) * (l - n));
    double u = sqrt((double)((l + m) * (l - m)) / denominator);
    double v =
        0.5 * sqrt((1.0 + d) * (double)((l + am - 1) * (l + am)) / denominator) * (1.0 - 2.0 * d);
    double w = -0.5 * sqrt((double)((l - am - 1) * (l - am)) / denominator) * (1.0 - d);
    double sum = 0.0;
    if (u != 0.0)
    {
        sum += u * P(one, previous, 0, m, n);
    }
    if (v != 0.0)
    {
        sum += v * V(one, previous, m, n);
    }
    if (w != 0.0)
    {
        sum += w * W(one, previous, m, n);
    }
    return sum;
}

void maudAmbisonicRotation(uint32_t order, const double rotation[3][3], float* blocks)
{
    // ACN order 1 is (y, z, x): field axis 1, 2, 0.
    static const int axis[3] = {1, 2, 0};
    Band one = {.order = 1};
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            one.m[i][j] = rotation[axis[i]][axis[j]];
            blocks[i * 3 + j] = (float)one.m[i][j];
        }
    }
    float* out = blocks + 9;
    Band previous = one;
    for (int l = 2; l <= (int)order; ++l)
    {
        Band band = {.order = l};
        for (int m = -l; m <= l; ++m)
        {
            for (int n = -l; n <= l; ++n)
            {
                band.m[m + l][n + l] = Entry(&one, &previous, l, m, n);
                *out++ = (float)band.m[m + l][n + l];
            }
        }
        previous = band;
    }
}
