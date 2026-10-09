// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Iterative radix-2 FFT: the bit-reversal permutation, then butterflies
// of doubling span, each twiddle computed directly (no recurrence, so
// rounding errors do not accumulate across the transform).

#include "fft.h"

#include <math.h>

static void Swap(double* values, uint32_t a, uint32_t b)
{
    double re = values[2 * a];
    double im = values[2 * a + 1];
    values[2 * a] = values[2 * b];
    values[2 * a + 1] = values[2 * b + 1];
    values[2 * b] = re;
    values[2 * b + 1] = im;
}

static void BitReverse(double* values, uint32_t count)
{
    uint32_t j = 0;
    for (uint32_t i = 1; i < count; ++i)
    {
        uint32_t bit = count >> 1;
        while ((j & bit) != 0)
        {
            j ^= bit;
            bit >>= 1;
        }
        j |= bit;
        if (i < j)
        {
            Swap(values, i, j);
        }
    }
}

void maudFft(double* values, uint32_t count, bool inverse)
{
    BitReverse(values, count);
    double sign = inverse ? 1.0 : -1.0;
    for (uint32_t span = 1; span < count; span *= 2)
    {
        double angle = sign * 3.14159265358979323846 / (double)span;
        for (uint32_t start = 0; start < count; start += 2 * span)
        {
            for (uint32_t k = 0; k < span; ++k)
            {
                double wr = cos(angle * (double)k);
                double wi = sin(angle * (double)k);
                double* a = values + 2 * (start + k);
                double* b = values + 2 * (start + k + span);
                double tr = wr * b[0] - wi * b[1];
                double ti = wr * b[1] + wi * b[0];
                b[0] = a[0] - tr;
                b[1] = a[1] - ti;
                a[0] += tr;
                a[1] += ti;
            }
        }
    }
    if (inverse)
    {
        for (uint32_t i = 0; i < 2 * count; ++i)
        {
            values[i] /= (double)count;
        }
    }
}
