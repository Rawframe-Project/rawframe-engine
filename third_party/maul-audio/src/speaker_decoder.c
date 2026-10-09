// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Speaker decoders for the bed, by all-round ambisonic decoding (AllRAD,
// Zotter and Frank 2012): the bed is decoded to 240 near-uniform virtual
// speakers (a Fibonacci lattice of 120 and its mirror image) with max-rE weights, and each virtual
// speaker is panned to the layout's by the speaker panner. Both steps are
// linear, so they fold into one matrix of speakers by channels, made at
// creation. The matrix is scaled so that a source encoded into the bed
// reaches the speakers with unit energy on average over the sphere, as a
// source panned straight to them does.

#include "allocator.h"
#include "spherical_harmonics.h"

#include "maul-audio/ambisonics.h"

#include <math.h>
#include <string.h>

#define SPEAKER_DECODER_DEF_COOKIE 0x6D617364u
#define MAX_CHANNELS               12
#define VIRTUAL_SPEAKERS           240
#define PI_D                       3.14159265358979323846

struct maudSpeakerDecoder
{
    maudAllocator allocator;
    uint32_t channels;
    uint32_t speakers;
    float matrix[MAX_CHANNELS][MAUD_MAX_AMBISONIC_CHANNELS];
};

maudSpeakerDecoderDef maudDefaultSpeakerDecoderDef(void)
{
    return (maudSpeakerDecoderDef){
        .cookie = SPEAKER_DECODER_DEF_COOKIE,
        .layout = maud_layoutStereo,
        .order = 3,
        .allocator = {nullptr, nullptr, nullptr},
    };
}

// The i-th of count near-uniform points, in the field's axes: a
// Fibonacci lattice of count / 2 points and their mirror images left to
// right, so that the decoder is exactly symmetric.
static void Lattice(uint32_t i, uint32_t count, double* p)
{
    uint32_t half = count / 2;
    uint32_t pair = i / 2;
    double j = (double)pair + 0.5;
    double z = 1.0 - 2.0 * j / (double)half;
    double azimuth = PI_D * (1.0 + sqrt(5.0)) * j;
    double r = sqrt(1.0 - z * z);
    p[0] = r * cos(azimuth);
    p[1] = (i % 2 == 0 ? 1.0 : -1.0) * r * sin(azimuth);
    p[2] = z;
}

// Each channel's weight: max-rE's Legendre value for its degree, times
// 2l + 1, which turns the SN3D bed into the sampling decoder's terms.
static void Weights(uint32_t order, double* weights)
{
    double x = cos(137.9 / ((double)order + 1.51) * PI_D / 180.0);
    double legendre[4] = {1.0, x, 0.5 * (3.0 * x * x - 1.0), 0.5 * (5.0 * x * x * x - 3.0 * x)};
    for (uint32_t l = 0; l <= order; ++l)
    {
        for (uint32_t m = 0; m < 2 * l + 1; ++m)
        {
            weights[l * l + m] = legendre[l] * (double)(2 * l + 1);
        }
    }
}

static void Harmonics(uint32_t order, const double* p, double* y)
{
    float g[MAUD_MAX_AMBISONIC_CHANNELS];
    maudSphericalHarmonics(order, (float)p[0], (float)p[1], (float)p[2], g);
    for (uint32_t c = 0; c < (order + 1) * (order + 1); ++c)
    {
        y[c] = (double)g[c];
    }
}

// Folds the virtual speakers into the matrix; false if the panner fails.
static bool Build(const maudSpeakerPanner* panner, uint32_t order, double m[][16],
                  uint32_t speakers)
{
    uint32_t k = (order + 1) * (order + 1);
    double weights[16];
    Weights(order, weights);
    for (uint32_t t = 0; t < VIRTUAL_SPEAKERS; ++t)
    {
        double p[3];
        double y[16];
        float gains[MAX_CHANNELS];
        Lattice(t, VIRTUAL_SPEAKERS, p);
        Harmonics(order, p, y);
        // The field's axes to the listener's: x ahead is -z, y left is -x.
        maudVector3 direction = {(float)-p[1], (float)p[2], (float)-p[0]};
        if (maudGetSpeakerGains(panner, direction, gains) != maud_success)
        {
            return false;
        }
        for (uint32_t s = 0; s < speakers; ++s)
        {
            for (uint32_t c = 0; c < k; ++c)
            {
                m[s][c] += (double)gains[s] * weights[c] * y[c] / VIRTUAL_SPEAKERS;
            }
        }
    }
    return true;
}

// The mean energy at the speakers of a plane wave from each virtual
// speaker's direction.
static double MeanEnergy(double m[][16], uint32_t speakers, uint32_t order)
{
    uint32_t k = (order + 1) * (order + 1);
    double total = 0.0;
    for (uint32_t t = 0; t < VIRTUAL_SPEAKERS; ++t)
    {
        double p[3];
        double y[16];
        Lattice(t, VIRTUAL_SPEAKERS, p);
        Harmonics(order, p, y);
        for (uint32_t s = 0; s < speakers; ++s)
        {
            double out = 0.0;
            for (uint32_t c = 0; c < k; ++c)
            {
                out += m[s][c] * y[c];
            }
            total += out * out;
        }
    }
    return total / VIRTUAL_SPEAKERS;
}

static bool DefValid(const maudSpeakerDecoderDef* def)
{
    return def->cookie == SPEAKER_DECODER_DEF_COOKIE && def->order >= 1 &&
           def->order <= MAUD_MAX_AMBISONIC_ORDER && maudGetLayoutChannelCount(def->layout) != 0 &&
           maudIsAllocatorValid(&def->allocator);
}

maudResult maudCreateSpeakerDecoder(const maudSpeakerDecoderDef* def,
                                    maudSpeakerDecoder** decoderOut)
{
    if (decoderOut != nullptr)
    {
        *decoderOut = nullptr;
    }
    if (def == nullptr || decoderOut == nullptr || !DefValid(def))
    {
        return maud_errorInvalid;
    }
    maudSpeakerPannerDef pannerDef = maudDefaultSpeakerPannerDef();
    pannerDef.layout = def->layout;
    pannerDef.allocator = def->allocator;
    maudSpeakerPanner* panner = nullptr;
    maudResult result = maudCreateSpeakerPanner(&pannerDef, &panner);
    if (result != maud_success)
    {
        return result;
    }
    uint32_t speakers = maudGetLayoutChannelCount(def->layout);
    double m[MAX_CHANNELS][16] = {{0.0}};
    bool built = Build(panner, def->order, m, speakers);
    maudDestroySpeakerPanner(panner);
    if (!built)
    {
        return maud_errorInvalid;
    }
    maudSpeakerDecoder* decoder =
        maudAllocate(&def->allocator, sizeof(maudSpeakerDecoder), alignof(maudSpeakerDecoder));
    if (decoder == nullptr)
    {
        return maud_errorCapacity;
    }
    *decoder = (maudSpeakerDecoder){
        .allocator = def->allocator,
        .channels = maudGetAmbisonicChannelCount(def->order),
        .speakers = speakers,
    };
    double scale = 1.0 / sqrt(MeanEnergy(m, speakers, def->order));
    for (uint32_t s = 0; s < speakers; ++s)
    {
        for (uint32_t c = 0; c < decoder->channels; ++c)
        {
            decoder->matrix[s][c] = (float)(m[s][c] * scale);
        }
    }
    *decoderOut = decoder;
    return maud_success;
}

void maudDestroySpeakerDecoder(maudSpeakerDecoder* decoder)
{
    if (decoder == nullptr)
    {
        return;
    }
    maudAllocator allocator = decoder->allocator;
    maudRelease(&allocator, decoder, sizeof(maudSpeakerDecoder), alignof(maudSpeakerDecoder));
}

maudResult maudGetSpeakerDecoderMatrix(const maudSpeakerDecoder* decoder, float* matrixOut)
{
    if (decoder == nullptr || matrixOut == nullptr)
    {
        return maud_errorInvalid;
    }
    for (uint32_t s = 0; s < decoder->speakers; ++s)
    {
        memcpy(matrixOut + (size_t)s * decoder->channels, decoder->matrix[s],
               decoder->channels * sizeof(float));
    }
    return maud_success;
}

static bool PointersValid(const maudSpeakerDecoder* decoder, const float* const* bed,
                          float* const* out)
{
    if (bed == nullptr || out == nullptr)
    {
        return false;
    }
    for (uint32_t c = 0; c < decoder->channels; ++c)
    {
        if (bed[c] == nullptr)
        {
            return false;
        }
    }
    for (uint32_t s = 0; s < decoder->speakers; ++s)
    {
        if (out[s] == nullptr)
        {
            return false;
        }
    }
    return true;
}

maudResult maudDecodeToSpeakers(const maudSpeakerDecoder* decoder, const float* const* bed,
                                float* const* out, uint32_t frames)
{
    if (decoder == nullptr || (frames > 0 && !PointersValid(decoder, bed, out)))
    {
        return maud_errorInvalid;
    }
    for (uint32_t s = 0; s < decoder->speakers && frames > 0; ++s)
    {
        memset(out[s], 0, (size_t)frames * sizeof(float));
        for (uint32_t c = 0; c < decoder->channels; ++c)
        {
            float weight = decoder->matrix[s][c];
            if (weight == 0.0f)
            {
                continue;
            }
            const float* in = bed[c];
            for (uint32_t n = 0; n < frames; ++n)
            {
                out[s][n] += weight * in[n];
            }
        }
    }
    return maud_success;
}
