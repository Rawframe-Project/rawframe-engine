// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Binaural decoders: filters fitted once by magnitude least squares
// (magls.h), then each ear the sum of every channel through its filter,
// as direct FIRs over each channel's history. One block from the def's
// allocator holds the filters and the histories; decoding only reads and
// writes it.

#include "allocator.h"
#include "binaural_dsp.h"
#include "hrtf_core.h"
#include "magls.h"

#include "maul-audio/ambisonics.h"

#include <math.h>
#include <string.h>

#define DECODER_DEF_COOKIE 0x6D616264u
#define MAX_FRAMES         16384u
#define DEFAULT_MAX_FRAMES 1024u
// At 48 kHz, in proportion at other rates: the filters' length, the
// group delay carried through the fit, and the fade at the filters' end.
#define FILTER_TAPS 96.0
#define GROUP_DELAY 32.0
#define FADE_TAPS   16.0

struct maudBinauralDecoder
{
    maudAllocator allocator;
    size_t bytes;
    uint32_t channels;
    uint32_t taps;
    uint32_t maxFrames;
    // 2 ears x channels x taps.
    float* filters;
    // Per channel: taps - 1 samples of history, then a call's frames.
    float* history;
};

maudBinauralDecoderDef maudDefaultBinauralDecoderDef(void)
{
    return (maudBinauralDecoderDef){
        .cookie = DECODER_DEF_COOKIE,
        .hrtf = nullptr,
        .order = MAUD_MAX_AMBISONIC_ORDER,
        .maxFrames = DEFAULT_MAX_FRAMES,
        .allocator = {nullptr, nullptr, nullptr},
    };
}

static bool DefValid(const maudBinauralDecoderDef* def)
{
    return def->cookie == DECODER_DEF_COOKIE && def->hrtf != nullptr && def->order >= 1 &&
           def->order <= MAUD_MAX_AMBISONIC_ORDER && def->maxFrames >= 1 &&
           def->maxFrames <= MAX_FRAMES && maudIsAllocatorValid(&def->allocator);
}

static uint32_t Scaled(double atFortyEight, uint32_t rate)
{
    double value = round(atFortyEight * (double)rate / 48000.0);
    return value < 1.0 ? 1u : (uint32_t)value;
}

maudResult maudCreateBinauralDecoder(const maudBinauralDecoderDef* def,
                                     maudBinauralDecoder** decoderOut)
{
    if (decoderOut != nullptr)
    {
        *decoderOut = nullptr;
    }
    if (def == nullptr || decoderOut == nullptr || !DefValid(def))
    {
        return maud_errorInvalid;
    }
    const maudHrtf* hrtf = def->hrtf;
    uint32_t channels = (def->order + 1) * (def->order + 1);
    uint32_t taps = Scaled(FILTER_TAPS, hrtf->sampleRate);
    maudLayout layout = {.size = sizeof(maudBinauralDecoder)};
    size_t filters =
        maudLayoutAdd(&layout, 2u * (size_t)channels * taps, sizeof(float), alignof(float));
    size_t history = maudLayoutAdd(&layout, (size_t)channels * (taps - 1 + def->maxFrames),
                                   sizeof(float), alignof(float));
    unsigned char* block =
        layout.overflow ? nullptr
                        : maudAllocate(&def->allocator, layout.size, alignof(maudBinauralDecoder));
    if (block == nullptr)
    {
        return maud_errorCapacity;
    }
    memset(block, 0, layout.size);
    maudBinauralDecoder* decoder = (maudBinauralDecoder*)block;
    *decoder = (maudBinauralDecoder){
        .allocator = def->allocator,
        .bytes = layout.size,
        .channels = channels,
        .taps = taps,
        .maxFrames = def->maxFrames,
        .filters = (float*)(block + filters),
        .history = (float*)(block + history),
    };
    maudResult result =
        maudBuildMagLs(hrtf, def->order, taps, GROUP_DELAY * (double)hrtf->sampleRate / 48000.0,
                       Scaled(FADE_TAPS, hrtf->sampleRate), &def->allocator, decoder->filters);
    if (result != maud_success)
    {
        maudRelease(&def->allocator, block, layout.size, alignof(maudBinauralDecoder));
        return result;
    }
    *decoderOut = decoder;
    return maud_success;
}

void maudDestroyBinauralDecoder(maudBinauralDecoder* decoder)
{
    if (decoder == nullptr)
    {
        return;
    }
    maudAllocator allocator = decoder->allocator;
    maudRelease(&allocator, decoder, decoder->bytes, alignof(maudBinauralDecoder));
}

maudResult maudResetBinauralDecoder(maudBinauralDecoder* decoder)
{
    if (decoder == nullptr)
    {
        return maud_errorInvalid;
    }
    memset(decoder->history, 0,
           (size_t)decoder->channels * (decoder->taps - 1 + decoder->maxFrames) * sizeof(float));
    return maud_success;
}

static bool ChannelsValid(const maudBinauralDecoder* decoder, const float* const* bed,
                          float* const out[2])
{
    if (bed == nullptr || out == nullptr || out[0] == nullptr || out[1] == nullptr)
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
    return true;
}

maudResult maudDecodeBinaural(maudBinauralDecoder* decoder, const float* const* bed,
                              float* const out[2], uint32_t frames)
{
    if (decoder == nullptr || frames > decoder->maxFrames ||
        (frames > 0 && !ChannelsValid(decoder, bed, out)))
    {
        return maud_errorInvalid;
    }
    if (frames == 0)
    {
        return maud_success;
    }
    uint32_t taps = decoder->taps;
    size_t stride = (size_t)taps - 1 + decoder->maxFrames;
    for (uint32_t c = 0; c < decoder->channels; ++c)
    {
        memcpy(decoder->history + c * stride + taps - 1, bed[c], (size_t)frames * sizeof(float));
    }
    for (uint32_t ear = 0; ear < 2; ++ear)
    {
        memset(out[ear], 0, (size_t)frames * sizeof(float));
        for (uint32_t c = 0; c < decoder->channels; ++c)
        {
            const float* filter = decoder->filters + ((size_t)ear * decoder->channels + c) * taps;
            maudFirAdd(decoder->history + c * stride + taps - 1, filter, taps, out[ear], frames);
        }
    }
    for (uint32_t c = 0; c < decoder->channels; ++c)
    {
        float* channel = decoder->history + c * stride;
        memmove(channel, channel + frames, ((size_t)taps - 1) * sizeof(float));
    }
    return maud_success;
}
