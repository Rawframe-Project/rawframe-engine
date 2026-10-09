// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Responses from fields (reflection_response.h). A frame's noise is a
// unit value at a random phase per FFT bin, so a flat magnitude m over
// an FFT of n gives samples of mean power m^2 / n: m = sqrt(n E / hop)
// gives power E / hop. Unit values rather than Gaussian ones fix each
// frame's energy, which a response whose energy lies in a few bins
// needs (Gaussian magnitudes left one 10 % off). The sine window squared
// and its neighbour's sum to 1 across the overlap, so the power holds
// between frames.

#include "reflection_response.h"

#include "fft.h"

#include "maul-audio/direct.h"

#include <math.h>
#include <string.h>

#define PI_D        3.14159265358979323846
#define BIN_SECONDS 0.01
// Energies below this are silence: their ratios mean nothing.
#define QUIET 1e-30

uint32_t maudResponseHop(double rate)
{
    return (uint32_t)lround(rate * BIN_SECONDS);
}

// The FFT size: a power of two holding two hops.
static uint32_t Size(uint32_t hop)
{
    uint32_t n = 2;
    while (n < 2 * hop)
    {
        n *= 2;
    }
    return n;
}

size_t maudResponseScratch(double rate)
{
    // Three complex frames.
    return (size_t)MAUD_DIRECT_BANDS * 2 * Size(maudResponseHop(rate));
}

// A number in (0, 1] from a bin, an FFT bin and a stream (lowbias32).
static double Uniform(uint32_t bin, uint32_t k, uint32_t stream)
{
    uint32_t x = bin * 0x9E3779B9u ^ k * 0x85EBCA6Bu ^ stream * 0xC2B2AE35u ^ 0x5BD1E995u;
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return ((double)(x >> 8) + 1.0) / 16777216.0;
}

// The band centres' logs: sqrt(20 * 800), sqrt(800 * 8000) and
// sqrt(8000 * 20000) Hz.
static void Centres(double* logs)
{
    logs[0] = log(sqrt(20.0 * 800.0));
    logs[1] = log(sqrt(800.0 * 8000.0));
    logs[2] = log(sqrt(8000.0 * 20000.0));
}

// The interpolation weights of the bands at a frequency (log hz): linear
// in log frequency between the centres, held beyond.
static void Weights(const double* centres, double logHz, double* w)
{
    w[0] = 0.0;
    w[1] = 0.0;
    w[2] = 0.0;
    if (logHz <= centres[0])
    {
        w[0] = 1.0;
        return;
    }
    if (logHz >= centres[2])
    {
        w[2] = 1.0;
        return;
    }
    int b = logHz < centres[1] ? 0 : 1;
    double u = (logHz - centres[b]) / (centres[b + 1] - centres[b]);
    w[b] = 1.0 - u;
    w[b + 1] = u;
}

// Fills the three basis spectra of a bin: shared noise times W's
// magnitude times each band's weight; false if the bin is silent.
static bool Spectra(const double* energy, uint32_t bin, uint32_t hop, uint32_t n, double rate,
                    double* frames)
{
    if (!(energy[0] > QUIET || energy[1] > QUIET || energy[2] > QUIET))
    {
        return false;
    }
    double centres[MAUD_DIRECT_BANDS];
    Centres(centres);
    double logs[MAUD_DIRECT_BANDS];
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        logs[b] = log(fmax(energy[b], QUIET));
    }
    memset(frames, 0, (size_t)MAUD_DIRECT_BANDS * 2 * n * sizeof(double));
    for (uint32_t k = 0; k <= n / 2; ++k)
    {
        double logHz = log(fmax((double)k * rate / (double)n, 1.0));
        double w[MAUD_DIRECT_BANDS];
        Weights(centres, logHz, w);
        double level = exp(w[0] * logs[0] + w[1] * logs[1] + w[2] * logs[2]);
        double magnitude = sqrt((double)n * level / (double)hop);
        // A unit value at a random phase.
        double angle = 2.0 * PI_D * Uniform(bin, k, 1);
        double re = magnitude * cos(angle);
        double im = k == 0 || k == n / 2 ? 0.0 : magnitude * sin(angle);
        for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
        {
            double* f = frames + (size_t)b * 2 * n;
            f[2 * k] = w[b] * re;
            f[2 * k + 1] = w[b] * im;
            if (k > 0 && k < n / 2)
            {
                f[2 * (n - k)] = w[b] * re;
                f[2 * (n - k) + 1] = -w[b] * im;
            }
        }
    }
    return true;
}

// Adds a channel's frame, the bases mixed by its ratios and windowed,
// into out from start (samples outside [0, frames) dropped).
static void Add(const double* bases, uint32_t n, const double* ratio, int64_t start, uint32_t span,
                float* out, uint32_t frames)
{
    for (uint32_t i = 0; i < span; ++i)
    {
        int64_t at = start + i;
        if (at < 0 || at >= frames)
        {
            continue;
        }
        double window = sin(PI_D * ((double)i + 0.5) / (double)span);
        double sum = 0.0;
        for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
        {
            sum += ratio[b] * bases[(size_t)b * 2 * n + 2 * i];
        }
        out[at] += (float)(window * sum);
    }
}

void maudReconstructResponse(const float* field, uint32_t order, uint32_t bins, double rate,
                             float* const* out, uint32_t frames, double* scratch)
{
    uint32_t channels = (order + 1) * (order + 1);
    uint32_t hop = maudResponseHop(rate);
    uint32_t n = Size(hop);
    uint32_t span = 2 * hop;
    for (uint32_t c = 0; c < channels; ++c)
    {
        memset(out[c], 0, (size_t)frames * sizeof(float));
    }
    for (uint32_t bin = 0; bin < bins && (uint64_t)bin * hop < frames; ++bin)
    {
        double energy[MAUD_DIRECT_BANDS];
        for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
        {
            energy[b] = (double)field[(uint32_t)b * bins + bin];
        }
        if (!Spectra(energy, bin, hop, n, rate, scratch))
        {
            continue;
        }
        for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
        {
            maudFft(scratch + (size_t)b * 2 * n, n, true);
        }
        // Centred on the bin: from half a hop before it.
        int64_t start = (int64_t)bin * hop - hop / 2;
        for (uint32_t c = 0; c < channels; ++c)
        {
            double ratio[MAUD_DIRECT_BANDS];
            for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
            {
                double e = energy[b];
                double v = (double)field[((size_t)c * MAUD_DIRECT_BANDS + b) * bins + bin];
                ratio[b] = e > QUIET ? v / e : 0.0;
            }
            Add(scratch, n, ratio, start, span, out[c], frames);
        }
    }
}
