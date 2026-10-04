// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Interleaving and deinterleaving. Stereo, by far the most common case,
// has its own loop; other counts walk frame by frame so that the
// interleaved side is read or written in order.

#include "maul-audio/buffer.h"

#include <stdckdint.h>

// Checks the arguments shared by both directions: a channel count, and
// arrays wherever samples are due, with a total that fits in memory.
static bool ArgumentsValid(bool havePlanar, uint32_t channelCount, uint32_t frameCount,
                           bool haveInterleaved)
{
    if (channelCount == 0)
    {
        return false;
    }
    if (frameCount == 0)
    {
        return true;
    }
    size_t bytes;
    if (ckd_mul(&bytes, (size_t)channelCount, (size_t)frameCount) ||
        ckd_mul(&bytes, bytes, sizeof(float)))
    {
        return false;
    }
    return havePlanar && haveInterleaved;
}

static bool ChannelsPresent(const float* const* channels, uint32_t channelCount)
{
    for (uint32_t c = 0; c < channelCount; ++c)
    {
        if (channels[c] == nullptr)
        {
            return false;
        }
    }
    return true;
}

maudResult maudInterleave(const float* const* planar, uint32_t channelCount, uint32_t frameCount,
                          float* interleavedOut)
{
    if (!ArgumentsValid(planar != nullptr, channelCount, frameCount, interleavedOut != nullptr))
    {
        return maud_errorInvalid;
    }
    if (frameCount == 0)
    {
        return maud_success;
    }
    if (!ChannelsPresent(planar, channelCount))
    {
        return maud_errorInvalid;
    }
    if (channelCount == 2)
    {
        const float* left = planar[0];
        const float* right = planar[1];
        for (size_t i = 0; i < frameCount; ++i)
        {
            interleavedOut[2 * i] = left[i];
            interleavedOut[2 * i + 1] = right[i];
        }
        return maud_success;
    }
    float* out = interleavedOut;
    for (size_t i = 0; i < frameCount; ++i)
    {
        for (uint32_t c = 0; c < channelCount; ++c)
        {
            *out++ = planar[c][i];
        }
    }
    return maud_success;
}

maudResult maudDeinterleave(const float* interleaved, uint32_t channelCount, uint32_t frameCount,
                            float* const* planarOut)
{
    if (!ArgumentsValid(planarOut != nullptr, channelCount, frameCount, interleaved != nullptr))
    {
        return maud_errorInvalid;
    }
    if (frameCount == 0)
    {
        return maud_success;
    }
    if (!ChannelsPresent((const float* const*)planarOut, channelCount))
    {
        return maud_errorInvalid;
    }
    if (channelCount == 2)
    {
        float* left = planarOut[0];
        float* right = planarOut[1];
        for (size_t i = 0; i < frameCount; ++i)
        {
            left[i] = interleaved[2 * i];
            right[i] = interleaved[2 * i + 1];
        }
        return maud_success;
    }
    const float* in = interleaved;
    for (size_t i = 0; i < frameCount; ++i)
    {
        for (uint32_t c = 0; c < channelCount; ++c)
        {
            planarOut[c][i] = *in++;
        }
    }
    return maud_success;
}
