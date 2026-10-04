// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Exclusive mode. The device's formats are asked for in order, without
// a closest match; the buffer is one period long, and when the endpoint
// wants another length (AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) the client is
// made again with the length it gives.

#include "wasapi_exclusive.h"

#include "wasapi_format.h"

#include <mmreg.h>

static const GUID s_iidAudioClient = {
    0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
static const GUID s_subtypeFloat = {
    0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
static const GUID s_subtypePcm = {
    0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

typedef struct Candidate
{
    WORD bits;
    WORD valid;
    maudSampleKind kind;
} Candidate;

static const Candidate s_candidates[] = {
    {32, 32, maud_sampleFloat32},
    {32, 32, maud_sampleInt32},
    {32, 24, maud_sampleInt32},
    {16, 16, maud_sampleInt16},
};

static WAVEFORMATEXTENSIBLE FormatOf(const maudStreamCore* core, const Candidate* candidate)
{
    WORD channels = (WORD)core->period.channelCount;
    DWORD rate = core->format.sampleRate;
    WORD block = (WORD)(channels * candidate->bits / 8);
    return (WAVEFORMATEXTENSIBLE){
        .Format =
            {
                .wFormatTag = WAVE_FORMAT_EXTENSIBLE,
                .nChannels = channels,
                .nSamplesPerSec = rate,
                .nAvgBytesPerSec = rate * block,
                .nBlockAlign = block,
                .wBitsPerSample = candidate->bits,
                .cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX),
            },
        .Samples = {.wValidBitsPerSample = candidate->valid},
        .dwChannelMask = maudWasapiMaskOfLayout(core->format.layout),
        .SubFormat = candidate->kind == maud_sampleFloat32 ? s_subtypeFloat : s_subtypePcm,
    };
}

static maudResult ResultOf(HRESULT result)
{
    if (SUCCEEDED(result))
    {
        return maud_success;
    }
    return result == AUDCLNT_E_UNSUPPORTED_FORMAT || result == AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED
               ? maud_errorUnsupported
               : maud_errorPlatform;
}

// The first candidate the device takes as is, or NULL with the reason
// in *resultOut.
static const Candidate* Choose(maudWasapiStream* entry, HRESULT* resultOut)
{
    *resultOut = AUDCLNT_E_UNSUPPORTED_FORMAT;
    for (size_t i = 0; i < sizeof(s_candidates) / sizeof(s_candidates[0]); ++i)
    {
        WAVEFORMATEXTENSIBLE format = FormatOf(entry->core, &s_candidates[i]);
        *resultOut = IAudioClient_IsFormatSupported(entry->client, AUDCLNT_SHAREMODE_EXCLUSIVE,
                                                    (WAVEFORMATEX*)&format, nullptr);
        if (*resultOut == S_OK)
        {
            return &s_candidates[i];
        }
        if (*resultOut != AUDCLNT_E_UNSUPPORTED_FORMAT)
        {
            return nullptr;
        }
    }
    return nullptr;
}

maudResult maudWasapiInitializeExclusive(maudWasapiStream* entry, IMMDevice* device)
{
    HRESULT result = S_OK;
    const Candidate* chosen = Choose(entry, &result);
    if (chosen == nullptr)
    {
        return result == S_OK ? maud_errorUnsupported : ResultOf(result);
    }
    const maudStreamCore* core = entry->core;
    uint32_t rate = core->format.sampleRate;
    REFERENCE_TIME standard = 0;
    REFERENCE_TIME minimum = 0;
    if (FAILED(IAudioClient_GetDevicePeriod(entry->client, &standard, &minimum)))
    {
        return maud_errorPlatform;
    }
    REFERENCE_TIME period =
        ((REFERENCE_TIME)core->format.periodFrames * 10000000 + rate - 1) / rate;
    period = period < minimum ? minimum : period;
    WAVEFORMATEXTENSIBLE format = FormatOf(core, chosen);
    DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    result = IAudioClient_Initialize(entry->client, AUDCLNT_SHAREMODE_EXCLUSIVE, flags, period,
                                     period, (WAVEFORMATEX*)&format, nullptr);
    if (result == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED)
    {
        UINT32 frames = 0;
        if (FAILED(IAudioClient_GetBufferSize(entry->client, &frames)))
        {
            return maud_errorPlatform;
        }
        IAudioClient_Release(entry->client);
        entry->client = nullptr;
        if (FAILED(IMMDevice_Activate(device, &s_iidAudioClient, CLSCTX_ALL, nullptr,
                                      (void**)&entry->client)))
        {
            entry->client = nullptr;
            return maud_errorPlatform;
        }
        period = ((REFERENCE_TIME)frames * 10000000 + rate / 2) / rate;
        result = IAudioClient_Initialize(entry->client, AUDCLNT_SHAREMODE_EXCLUSIVE, flags, period,
                                         period, (WAVEFORMATEX*)&format, nullptr);
    }
    entry->sampleKind = chosen->kind;
    return ResultOf(result);
}
