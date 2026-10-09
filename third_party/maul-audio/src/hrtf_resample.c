// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Band-limited interpolation (Smith, "Digital Audio Resampling Home
// Page"): an output tap at time t (in input samples) is the sum of the
// input taps weighted by a sinc at the lower of the two Nyquist
// frequencies, under a Blackman window. A filter's taps are samples of
// its impulse response times the sample period, so going from rate r1
// to r2 also scales them by r1 / r2. Computed in double.

#include "hrtf_resample.h"

#include <math.h>

// The sinc's reach on each side, in input samples, and how early the
// output starts. Measured on the KU100 set: a reach of 32 keeps the
// response flat to 20 kHz and takes 23.5 kHz down by 43 dB going to
// 44.1 kHz (16 left 18 dB); a lead of 24 keeps every response within
// 0.02 dB below 19 kHz, where no lead loses up to 11 dB at 96 kHz.
#define HALF_WIDTH 32.0
#define LEAD       24.0
#define PI         3.14159265358979323846

// The output samples the output starts early by: LEAD input samples.
static uint32_t Lead(uint32_t from, uint32_t to)
{
    return (uint32_t)ceil(LEAD * (double)to / (double)from);
}

uint32_t maudResampledTaps(uint32_t taps, uint32_t from, uint32_t to)
{
    if (from == to)
    {
        return taps;
    }
    return (uint32_t)(((uint64_t)taps * to + from - 1) / from) + Lead(from, to);
}

// The window and sinc at x input samples from a tap, for a cutoff of
// cutoff times the input's Nyquist frequency.
static double Kernel(double x, double cutoff)
{
    double half = HALF_WIDTH / cutoff;
    if (fabs(x) >= half)
    {
        return 0.0;
    }
    double window = 0.42 + 0.5 * cos(PI * x / half) + 0.08 * cos(2.0 * PI * x / half);
    double phase = PI * cutoff * x;
    return window * cutoff * (phase == 0.0 ? 1.0 : sin(phase) / phase);
}

void maudResampleResponse(const float* in, uint32_t inTaps, uint32_t from, float* out,
                          uint32_t outTaps, uint32_t to)
{
    double step = (double)from / (double)to;
    double cutoff = to < from ? (double)to / (double)from : 1.0;
    double span = HALF_WIDTH / cutoff;
    double lead = (double)Lead(from, to);
    for (uint32_t n = 0; n < outTaps; ++n)
    {
        double t = ((double)n - lead) * step;
        double first = ceil(t - span);
        double last = floor(t + span);
        double sum = 0.0;
        for (double k = first < 0.0 ? 0.0 : first; k <= last && k < (double)inTaps; k += 1.0)
        {
            sum += (double)in[(uint32_t)k] * Kernel(t - k, cutoff);
        }
        out[n] = (float)(sum * step);
    }
}
