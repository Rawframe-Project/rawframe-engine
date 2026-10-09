// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Weighted least squares through the normal equations (least_squares.h),
// factored by Cholesky, in double.

#include "least_squares.h"

#include <math.h>
#include <stddef.h>

// The normal matrix Y^T W Y.
static void Normal(const double* rows, const double* weights, uint32_t count, uint32_t k,
                   double g[16][16])
{
    for (uint32_t d = 0; d < count; ++d)
    {
        const double* y = rows + (size_t)d * k;
        for (uint32_t i = 0; i < k; ++i)
        {
            for (uint32_t j = 0; j < k; ++j)
            {
                g[i][j] += weights[d] * y[i] * y[j];
            }
        }
    }
}

// g = L L^T in place, L in the lower triangle; false if g is not
// positive definite.
static bool Cholesky(double g[16][16], uint32_t k)
{
    for (uint32_t j = 0; j < k; ++j)
    {
        for (uint32_t i = j; i < k; ++i)
        {
            double sum = g[i][j];
            for (uint32_t p = 0; p < j; ++p)
            {
                sum -= g[i][p] * g[j][p];
            }
            if (i == j && sum <= 0.0)
            {
                return false;
            }
            g[i][j] = i == j ? sqrt(sum) : sum / g[j][j];
        }
    }
    return true;
}

// x = (L L^T)^-1 b, by forward and back substitution.
static void Substitute(const double l[16][16], uint32_t k, double* x)
{
    for (uint32_t i = 0; i < k; ++i)
    {
        for (uint32_t p = 0; p < i; ++p)
        {
            x[i] -= l[i][p] * x[p];
        }
        x[i] /= l[i][i];
    }
    for (uint32_t i = k; i-- > 0;)
    {
        for (uint32_t p = i + 1; p < k; ++p)
        {
            x[i] -= l[p][i] * x[p];
        }
        x[i] /= l[i][i];
    }
}

bool maudWeightedPseudoInverse(const double* rows, const double* weights, uint32_t count,
                               uint32_t k, double* solve)
{
    double g[16][16] = {{0.0}};
    Normal(rows, weights, count, k, g);
    if (!Cholesky(g, k))
    {
        return false;
    }
    for (uint32_t d = 0; d < count; ++d)
    {
        double x[16];
        for (uint32_t i = 0; i < k; ++i)
        {
            x[i] = weights[d] * rows[(size_t)d * k + i];
        }
        Substitute(g, k, x);
        for (uint32_t i = 0; i < k; ++i)
        {
            solve[(size_t)i * count + d] = x[i];
        }
    }
    return true;
}
