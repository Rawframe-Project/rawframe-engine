// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The binaural effect's sample loops (binaural_dsp.h).

#include "binaural_dsp.h"

void maudFir(const float* restrict x, const float* restrict h, uint32_t taps, float* restrict out,
             uint32_t frames)
{
    for (uint32_t n = 0; n < frames; ++n)
    {
        out[n] = 0.0f;
    }
    for (uint32_t k = 0; k < taps; ++k)
    {
        float coefficient = h[k];
        const float* in = x - k;
        for (uint32_t n = 0; n < frames; ++n)
        {
            out[n] += coefficient * in[n];
        }
    }
}

// The cubic Lagrange weights of the points at offsets -1, 0, 1 and 2
// samples from the read's whole delay, for its fraction f.
static void Weights(float f, float* c)
{
    c[0] = -f * (f - 1.0f) * (f - 2.0f) / 6.0f;
    c[1] = (f + 1.0f) * (f - 1.0f) * (f - 2.0f) / 2.0f;
    c[2] = -(f + 1.0f) * f * (f - 2.0f) / 2.0f;
    c[3] = (f + 1.0f) * f * (f - 1.0f) / 6.0f;
}

void maudFirAdd(const float* restrict x, const float* restrict h, uint32_t taps,
                float* restrict out, uint32_t frames)
{
    for (uint32_t k = 0; k < taps; ++k)
    {
        float coefficient = h[k];
        const float* in = x - k;
        for (uint32_t n = 0; n < frames; ++n)
        {
            out[n] += coefficient * in[n];
        }
    }
}

void maudReadDelayed(const float* restrict x, float start, float step, float* restrict out,
                     uint32_t frames)
{
    float c[4];
    if (step == 0.0f)
    {
        // A still delay: one set of weights, a four-tap filter.
        uint32_t whole = (uint32_t)start;
        Weights(start - (float)whole, c);
        const float* later = x - whole + 1;
        const float* at = x - whole;
        const float* earlier = x - whole - 1;
        const float* earliest = x - whole - 2;
        for (uint32_t n = 0; n < frames; ++n)
        {
            out[n] = c[0] * later[n] + c[1] * at[n] + c[2] * earlier[n] + c[3] * earliest[n];
        }
        return;
    }
    for (uint32_t n = 0; n < frames; ++n)
    {
        float delay = start + step * (float)n;
        uint32_t whole = (uint32_t)delay;
        Weights(delay - (float)whole, c);
        // x[n - whole + 1] back to x[n - whole - 2]: the four points around
        // the read, the first at offset -1 from it.
        const float* p = x + n - whole;
        out[n] = c[0] * p[1] + c[1] * p[0] + c[2] * p[-1] + c[3] * p[-2];
    }
}

void maudShelve(float* samples, uint32_t frames, const float start[3], const float step[3],
                float state[2])
{
    float x1 = state[0];
    float y1 = state[1];
    if (step[0] == 0.0f && step[1] == 0.0f && step[2] == 0.0f)
    {
        // A still filter: its coefficients once.
        float b0 = start[0];
        float b1 = start[1];
        float a1 = start[2];
        for (uint32_t n = 0; n < frames; ++n)
        {
            float x = samples[n];
            float y = b0 * x + b1 * x1 - a1 * y1;
            samples[n] = y;
            x1 = x;
            y1 = y;
        }
        state[0] = x1;
        state[1] = y1;
        return;
    }
    for (uint32_t n = 0; n < frames; ++n)
    {
        float b0 = start[0] + step[0] * (float)n;
        float b1 = start[1] + step[1] * (float)n;
        float a1 = start[2] + step[2] * (float)n;
        float x = samples[n];
        float y = b0 * x + b1 * x1 - a1 * y1;
        samples[n] = y;
        x1 = x;
        y1 = y;
    }
    state[0] = x1;
    state[1] = y1;
}
