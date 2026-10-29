// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Small pieces of the voice processors: biquad filters and levels.

#ifndef MAUL_AUDIO_SRC_VOICE_DSP_H
#define MAUL_AUDIO_SRC_VOICE_DSP_H

#include "flush.h"

#include <math.h>
#include <stdint.h>

// A second-order section in transposed direct form II.
typedef struct maudBiquad
{
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
    float z1;
    float z2;
} maudBiquad;

// A Butterworth high-pass at frequency hertz (RBJ's cookbook).
void maudDesignHighPass(maudBiquad* filter, double frequency, double sampleRate);

// A band-pass from low to high hertz, 0 dB at its centre, the geometric
// mean of the edges (RBJ's cookbook, constant peak gain).
void maudDesignBandPass(maudBiquad* filter, double low, double high, double sampleRate);

static inline float maudFilter(maudBiquad* filter, float in)
{
    float out = filter->b0 * in + filter->z1;
    filter->z1 = filter->b1 * in - filter->a1 * out + filter->z2;
    filter->z2 = filter->b2 * in - filter->a2 * out;
    return out;
}

// The smallest biquad state kept, checked once a 10 ms frame rather than
// on each sample's dependency chain: within a frame the detector's
// filters' states fall by 2e-14 at most (their slowest poles' radius
// to the frame's length, at 8 to 96 kHz), so from 1e-20 they stay
// normal until the next check.
#define MAUD_BIQUAD_TINY 1e-20f

static inline void maudFlushBiquad(maudBiquad* filter)
{
    if (fabsf(filter->z1) < MAUD_BIQUAD_TINY && fabsf(filter->z2) < MAUD_BIQUAD_TINY)
    {
        filter->z1 = 0.0f;
        filter->z2 = 0.0f;
    }
}

// The level of a mean square, in dBFS, no lower than MAUD_FLOOR_DBFS.
float maudLevelDbfs(double meanSquare);

// The exponential integral E1(x) for x > 0, as the log-spectral
// amplitude gain takes it: its series below 3 at a fixed degree, its
// continued fraction above; within 2e-13 of its value.
double maudExpIntegral(double x);

// The level below which a frame counts as silence.
#define MAUD_FLOOR_DBFS (-100.0f)

#endif // MAUL_AUDIO_SRC_VOICE_DSP_H
