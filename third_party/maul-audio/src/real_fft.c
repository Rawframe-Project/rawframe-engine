// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The real FFT (real_fft.h). Forward: the samples as n / 2 complex
// values z[j] = x[2j] + i x[2j + 1], loaded in bit-reversed order,
// transformed in place by decimation in time, then split:
// X[k] = (Z[k] + conj Z[m - k]) / 2 - i e^(-2 pi i k / n) (Z[k] -
// conj Z[m - k]) / 2, m = n / 2. Inverse: the split undone, the complex
// inverse by decimation in frequency (natural in, bit-reversed out), the
// samples stored from the bit-reversed positions.

#include "real_fft.h"

#include <math.h>

#define PI_D 3.14159265358979323846

size_t maudRealFftBytes(uint32_t size)
{
    uint32_t m = size / 2;
    return (size_t)(m / 2) * 2 * sizeof(float) + (size_t)m * 2 * sizeof(float) +
           (size_t)m * sizeof(uint32_t);
}

void maudInitRealFft(maudRealFft* fft, uint32_t size, void* memory)
{
    uint32_t m = size / 2;
    fft->size = size;
    fft->twiddles = memory;
    fft->split = fft->twiddles + (size_t)(m / 2) * 2;
    // The indices after the floats (both four bytes, alike aligned).
    size_t floats = ((size_t)(m / 2) * 2 + (size_t)m * 2) * sizeof(float);
    fft->reverse = (uint32_t*)((unsigned char*)memory + floats);
    for (uint32_t k = 0; k < m / 2; ++k)
    {
        double a = -2.0 * PI_D * (double)k / (double)m;
        fft->twiddles[2 * k] = (float)cos(a);
        fft->twiddles[2 * k + 1] = (float)sin(a);
    }
    for (uint32_t k = 0; k < m; ++k)
    {
        double a = -2.0 * PI_D * (double)k / (double)size;
        fft->split[2 * k] = (float)cos(a);
        fft->split[2 * k + 1] = (float)sin(a);
    }
    uint32_t bits = 0;
    while ((1u << bits) < m)
    {
        ++bits;
    }
    for (uint32_t k = 0; k < m; ++k)
    {
        uint32_t r = 0;
        for (uint32_t b = 0; b < bits; ++b)
        {
            r |= ((k >> b) & 1u) << (bits - 1 - b);
        }
        fft->reverse[k] = r;
    }
}

// Decimation in time over m complex values in bit-reversed order:
// natural order out. Twiddles conjugated when inverse.
static void Time(const maudRealFft* fft, float* z, uint32_t m)
{
    for (uint32_t half = 1; half < m; half *= 2)
    {
        uint32_t stride = m / (2 * half);
        for (uint32_t start = 0; start < m; start += 2 * half)
        {
            for (uint32_t j = 0; j < half; ++j)
            {
                float wr = fft->twiddles[2 * j * stride];
                float wi = fft->twiddles[2 * j * stride + 1];
                float* a = z + 2 * (start + j);
                float* b = a + 2 * half;
                float tr = wr * b[0] - wi * b[1];
                float ti = wr * b[1] + wi * b[0];
                b[0] = a[0] - tr;
                b[1] = a[1] - ti;
                a[0] += tr;
                a[1] += ti;
            }
        }
    }
}

// Decimation in frequency, inverse (conjugate twiddles), over m complex
// values in natural order: bit-reversed order out.
static void Frequency(const maudRealFft* fft, float* z, uint32_t m)
{
    for (uint32_t half = m / 2; half >= 1; half /= 2)
    {
        uint32_t stride = m / (2 * half);
        for (uint32_t start = 0; start < m; start += 2 * half)
        {
            for (uint32_t j = 0; j < half; ++j)
            {
                float wr = fft->twiddles[2 * j * stride];
                float wi = -fft->twiddles[2 * j * stride + 1];
                float* a = z + 2 * (start + j);
                float* b = a + 2 * half;
                float dr = a[0] - b[0];
                float di = a[1] - b[1];
                a[0] += b[0];
                a[1] += b[1];
                b[0] = wr * dr - wi * di;
                b[1] = wr * di + wi * dr;
            }
        }
    }
}

void maudForwardRealFft(const maudRealFft* fft, const float* in, float* bins)
{
    uint32_t m = fft->size / 2;
    for (uint32_t j = 0; j < m; ++j)
    {
        uint32_t r = fft->reverse[j];
        bins[2 * r] = in[2 * j];
        bins[2 * r + 1] = in[2 * j + 1];
    }
    Time(fft, bins, m);
    // The split, k and m - k together; Z[m] is Z[0].
    float z0r = bins[0];
    float z0i = bins[1];
    bins[0] = z0r + z0i;
    bins[1] = 0.0f;
    bins[2 * m] = z0r - z0i;
    bins[2 * m + 1] = 0.0f;
    for (uint32_t k = 1; k <= m / 2; ++k)
    {
        float* p = bins + 2 * k;
        float* q = bins + 2 * (m - k);
        float er = 0.5f * (p[0] + q[0]);
        float ei = 0.5f * (p[1] - q[1]);
        float or = 0.5f * (p[1] + q[1]);
        float oi = -0.5f * (p[0] - q[0]);
        float wr = fft->split[2 * k];
        float wi = fft->split[2 * k + 1];
        float tr = wr * or - wi * oi;
        float ti = wr * oi + wi * or;
        // X[k] = E + W O; X[m - k] = conj(E - W O) with W for m - k.
        p[0] = er + tr;
        p[1] = ei + ti;
        q[0] = er - tr;
        q[1] = -(ei - ti);
    }
}

void maudInverseRealFft(const maudRealFft* fft, float* bins, float* out)
{
    uint32_t m = fft->size / 2;
    // Undo the split: Z[k] = E + i O, E = (X[k] + conj X[m - k]) / 2,
    // O = (X[k] - conj X[m - k]) e^(2 pi i k / n) / 2.
    float x0 = bins[0];
    float xm = bins[2 * m];
    bins[0] = 0.5f * (x0 + xm);
    bins[1] = 0.5f * (x0 - xm);
    for (uint32_t k = 1; k <= m / 2; ++k)
    {
        float* p = bins + 2 * k;
        float* q = bins + 2 * (m - k);
        float er = 0.5f * (p[0] + q[0]);
        float ei = 0.5f * (p[1] - q[1]);
        float dr = 0.5f * (p[0] - q[0]);
        float di = 0.5f * (p[1] + q[1]);
        float wr = fft->split[2 * k];
        float wi = -fft->split[2 * k + 1];
        float or = dr * wr - di * wi;
        float oi = dr * wi + di * wr;
        // Z[k] = E + i O; Z[m - k] = conj(E) + i conj(O).
        p[0] = er - oi;
        p[1] = ei + or;
        q[0] = er + oi;
        q[1] = -ei + or;
    }
    Frequency(fft, bins, m);
    float scale = 1.0f / (float)m;
    for (uint32_t j = 0; j < m; ++j)
    {
        uint32_t r = fft->reverse[j];
        out[2 * j] = bins[2 * r] * scale;
        out[2 * j + 1] = bins[2 * r + 1] * scale;
    }
}
