// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Magnitude least-squares binaural decoders (magls.h). Every direction of
// the set is weighted by the area of its ring's band; the weighted
// least-squares solution comes from the normal equations by Cholesky.
// The responses' spectra carry each ear's delay and the group delay as
// linear phase. Below the cutoff each bin is fitted as it is; above it,
// only its magnitude is kept, with the phase the previous bin's decoder
// reconstructs, advanced by the group delay. The filters are the inverse
// transforms, cut to length with a half-cosine fade. Everything is in
// double but the spectra, kept in float pairs to halve the memory.

#include "magls.h"

#include "allocator.h"
#include "fft.h"
#include "least_squares.h"
#include "spherical_harmonics.h"

#include <math.h>
#include <string.h>

#define CUTOFF_HZ 1500.0
#define PI_D      3.14159265358979323846
#define DEGREES   (PI_D / 180.0)

typedef struct Work
{
    uint32_t order;
    uint32_t directions;
    uint32_t channels;
    uint32_t bins;
    uint32_t size;
    double* harmonics; // directions x channels
    double* solve;     // channels x directions: the weighted pseudo-inverse
    double* weights;   // directions
    float* spectra;    // directions x 2 ears x bins x (re, im)
    double* decoder;   // 2 ears x channels x bins x (re, im)
    double* buffer;    // size x (re, im)
} Work;

typedef struct Parts
{
    size_t harmonics;
    size_t solve;
    size_t weights;
    size_t spectra;
    size_t decoder;
    size_t buffer;
    size_t size;
    bool overflow;
} Parts;

static Parts LayOut(uint32_t directions, uint32_t channels, uint32_t bins, uint32_t size)
{
    maudLayout layout = {0};
    Parts parts = {0};
    size_t d = sizeof(double);
    parts.harmonics = maudLayoutAdd(&layout, (size_t)directions * channels, d, alignof(double));
    parts.solve = maudLayoutAdd(&layout, (size_t)directions * channels, d, alignof(double));
    parts.weights = maudLayoutAdd(&layout, directions, d, alignof(double));
    parts.spectra =
        maudLayoutAdd(&layout, (size_t)directions * 4u * bins, sizeof(float), alignof(float));
    parts.decoder = maudLayoutAdd(&layout, (size_t)channels * 4u * bins, d, alignof(double));
    parts.buffer = maudLayoutAdd(&layout, 2u * (size_t)size, d, alignof(double));
    parts.size = layout.size;
    parts.overflow = layout.overflow;
    return parts;
}

void maudRingWeights(const maudHrtf* hrtf, double* weights)
{
    uint32_t d = 0;
    double total = 0.0;
    for (uint32_t ring = 0; ring < hrtf->ringCount; ++ring)
    {
        double elevation = (double)hrtf->elevations[ring];
        double low = ring == 0 ? -90.0 : 0.5 * (elevation + (double)hrtf->elevations[ring - 1]);
        double high = ring + 1 == hrtf->ringCount
                          ? 90.0
                          : 0.5 * (elevation + (double)hrtf->elevations[ring + 1]);
        uint32_t count = hrtf->azimuths[ring];
        double share = (sin(high * DEGREES) - sin(low * DEGREES)) / (double)count;
        for (uint32_t i = 0; i < count; ++i, ++d)
        {
            weights[d] = share;
            total += share;
        }
    }
    for (uint32_t i = 0; i < d; ++i)
    {
        weights[i] /= total;
    }
}

// The harmonics of every direction, and the weights.
static void Directions(const maudHrtf* hrtf, Work* work)
{
    uint32_t d = 0;
    for (uint32_t ring = 0; ring < hrtf->ringCount; ++ring)
    {
        double elevation = (double)hrtf->elevations[ring] * DEGREES;
        uint32_t count = hrtf->azimuths[ring];
        for (uint32_t i = 0; i < count; ++i, ++d)
        {
            double azimuth = 2.0 * PI_D * (double)i / (double)count;
            float g[16];
            maudSphericalHarmonics(work->order, (float)(cos(elevation) * cos(azimuth)),
                                   (float)(cos(elevation) * sin(azimuth)), (float)sin(elevation),
                                   g);
            for (uint32_t c = 0; c < work->channels; ++c)
            {
                work->harmonics[(size_t)d * work->channels + c] = (double)g[c];
            }
        }
    }
    maudRingWeights(hrtf, work->weights);
}

// The weighted pseudo-inverse of the harmonics; false if the set's
// directions cannot carry the order.
static bool Solve(Work* work)
{
    return maudWeightedPseudoInverse(work->harmonics, work->weights, work->directions,
                                     work->channels, work->solve);
}

// Each response's spectrum with its delay and the group delay as linear
// phase.
static void Spectra(const maudHrtf* hrtf, Work* work, double groupDelay)
{
    for (uint32_t d = 0; d < work->directions; ++d)
    {
        for (uint32_t ear = 0; ear < 2; ++ear)
        {
            size_t at = 2u * (size_t)d + ear;
            memset(work->buffer, 0, 2u * (size_t)work->size * sizeof(double));
            for (uint32_t t = 0; t < hrtf->taps; ++t)
            {
                work->buffer[2 * t] = (double)hrtf->responses[at * hrtf->taps + t];
            }
            maudFft(work->buffer, work->size, false);
            double delay = (double)hrtf->delays[at] + groupDelay;
            float* out = work->spectra + at * 2u * work->bins;
            for (uint32_t b = 0; b < work->bins; ++b)
            {
                double angle = -2.0 * PI_D * (double)b * delay / (double)work->size;
                double re = work->buffer[2 * b];
                double im = work->buffer[2 * b + 1];
                out[2 * b] = (float)(re * cos(angle) - im * sin(angle));
                out[2 * b + 1] = (float)(re * sin(angle) + im * cos(angle));
            }
        }
    }
}

// The phase the decoder of bin b reconstructs at direction d, for ear.
static double Reconstructed(const Work* work, uint32_t ear, uint32_t b, uint32_t d)
{
    double re = 0.0;
    double im = 0.0;
    const double* y = work->harmonics + (size_t)d * work->channels;
    for (uint32_t c = 0; c < work->channels; ++c)
    {
        const double* coefficient =
            work->decoder + (((size_t)ear * work->channels + c) * work->bins + b) * 2u;
        re += y[c] * coefficient[0];
        im += y[c] * coefficient[1];
    }
    return atan2(im, re);
}

// One ear's decoder, bin by bin.
static void Fit(Work* work, uint32_t ear, double rate, double groupDelay)
{
    double advance = 2.0 * PI_D * groupDelay / (double)work->size;
    for (uint32_t b = 0; b < work->bins; ++b)
    {
        bool magnitude = b > 0 && (double)b * rate / (double)work->size > CUTOFF_HZ;
        double sums[16][2] = {{0.0}};
        for (uint32_t d = 0; d < work->directions; ++d)
        {
            const float* h = work->spectra + ((2u * (size_t)d + ear) * work->bins + b) * 2u;
            double re = (double)h[0];
            double im = (double)h[1];
            if (magnitude)
            {
                double size = sqrt(re * re + im * im);
                double phase = Reconstructed(work, ear, b - 1, d) - advance;
                re = size * cos(phase);
                im = size * sin(phase);
            }
            for (uint32_t c = 0; c < work->channels; ++c)
            {
                double a = work->solve[(size_t)c * work->directions + d];
                sums[c][0] += a * re;
                sums[c][1] += a * im;
            }
        }
        for (uint32_t c = 0; c < work->channels; ++c)
        {
            double* out =
                work->decoder + (((size_t)ear * work->channels + c) * work->bins + b) * 2u;
            out[0] = sums[c][0];
            out[1] = sums[c][1];
        }
    }
}

// The decoder's spectra as filters of taps, the last fadeTaps faded.
static void Filters(Work* work, uint32_t taps, uint32_t fadeTaps, float* filters)
{
    for (uint32_t i = 0; i < 2u * work->channels; ++i)
    {
        const double* spectrum = work->decoder + (size_t)i * work->bins * 2u;
        for (uint32_t b = 0; b < work->bins; ++b)
        {
            bool edge = b == 0 || b + 1 == work->bins;
            work->buffer[2 * b] = spectrum[2 * b];
            work->buffer[2 * b + 1] = edge ? 0.0 : spectrum[2 * b + 1];
            if (!edge)
            {
                work->buffer[2 * (work->size - b)] = spectrum[2 * b];
                work->buffer[2 * (work->size - b) + 1] = -spectrum[2 * b + 1];
            }
        }
        maudFft(work->buffer, work->size, true);
        float* out = filters + (size_t)i * taps;
        for (uint32_t t = 0; t < taps; ++t)
        {
            double fade = 1.0;
            if (t + fadeTaps >= taps)
            {
                double into = (double)(t + fadeTaps + 1 - taps) / (double)fadeTaps;
                fade = 0.5 * (1.0 + cos(PI_D * into));
            }
            out[t] = (float)(work->buffer[2 * t] * fade);
        }
    }
}

uint32_t maudMagLsSize(const maudHrtf* hrtf, uint32_t taps, double groupDelay)
{
    float largest = 0.0f;
    for (size_t i = 0; i < 2u * (size_t)hrtf->directionCount; ++i)
    {
        largest = hrtf->delays[i] > largest ? hrtf->delays[i] : largest;
    }
    double needed = (double)hrtf->taps + (double)largest + groupDelay + (double)taps;
    double least = 512.0 * (double)hrtf->sampleRate / 48000.0;
    uint32_t size = 2;
    while ((double)size < needed || (double)size < least)
    {
        size *= 2;
    }
    return size;
}

maudResult maudBuildMagLs(const maudHrtf* hrtf, uint32_t order, uint32_t taps, double groupDelay,
                          uint32_t fadeTaps, const maudAllocator* allocator, float* filters)
{
    uint32_t size = maudMagLsSize(hrtf, taps, groupDelay);
    uint32_t channels = (order + 1) * (order + 1);
    Parts parts = LayOut(hrtf->directionCount, channels, size / 2 + 1, size);
    unsigned char* block =
        parts.overflow ? nullptr : maudAllocate(allocator, parts.size, alignof(double));
    if (block == nullptr)
    {
        return maud_errorCapacity;
    }
    Work work = {
        .order = order,
        .directions = hrtf->directionCount,
        .channels = channels,
        .bins = size / 2 + 1,
        .size = size,
        .harmonics = (double*)(block + parts.harmonics),
        .solve = (double*)(block + parts.solve),
        .weights = (double*)(block + parts.weights),
        .spectra = (float*)(block + parts.spectra),
        .decoder = (double*)(block + parts.decoder),
        .buffer = (double*)(block + parts.buffer),
    };
    Directions(hrtf, &work);
    maudResult result = maud_errorInvalid;
    if (Solve(&work))
    {
        Spectra(hrtf, &work, groupDelay);
        Fit(&work, 0, (double)hrtf->sampleRate, groupDelay);
        Fit(&work, 1, (double)hrtf->sampleRate, groupDelay);
        Filters(&work, taps, fadeTaps, filters);
        result = maud_success;
    }
    maudRelease(allocator, block, parts.size, alignof(double));
    return result;
}
