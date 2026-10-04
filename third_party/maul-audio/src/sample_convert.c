// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Sample conversion. Integers scale by 2^15 or 2^31 both ways, so a
// round trip keeps every integer sample; the largest positive float
// value saturates one step below full scale. 32-bit integers are
// computed in double, whose mantissa holds them exactly.

#include "sample_convert.h"

#include <string.h>

size_t maudSampleBytes(maudSampleKind kind)
{
    return kind == maud_sampleInt16 ? sizeof(int16_t) : sizeof(int32_t);
}

// Rounds to the nearest integer in [low, high]; NaN is 0.
static int64_t Quantize(double value, double low, double high)
{
    if (!(value == value))
    {
        return 0;
    }
    value = value < low ? low : value;
    value = value > high ? high : value;
    return (int64_t)(value + (value >= 0.0 ? 0.5 : -0.5));
}

void maudFloatToSamples(void* out, const float* in, size_t count, maudSampleKind kind)
{
    if (kind == maud_sampleFloat32)
    {
        memcpy(out, in, count * sizeof(float));
        return;
    }
    if (kind == maud_sampleInt16)
    {
        int16_t* samples = out;
        for (size_t i = 0; i < count; ++i)
        {
            samples[i] = (int16_t)Quantize((double)in[i] * 32768.0, -32768.0, 32767.0);
        }
        return;
    }
    int32_t* samples = out;
    for (size_t i = 0; i < count; ++i)
    {
        samples[i] = (int32_t)Quantize((double)in[i] * 2147483648.0, -2147483648.0, 2147483647.0);
    }
}

void maudSamplesToFloat(float* out, const void* in, size_t count, maudSampleKind kind)
{
    if (kind == maud_sampleFloat32)
    {
        memcpy(out, in, count * sizeof(float));
        return;
    }
    if (kind == maud_sampleInt16)
    {
        const int16_t* samples = in;
        for (size_t i = 0; i < count; ++i)
        {
            out[i] = (float)samples[i] / 32768.0f;
        }
        return;
    }
    const int32_t* samples = in;
    for (size_t i = 0; i < count; ++i)
    {
        out[i] = (float)((double)samples[i] / 2147483648.0);
    }
}
