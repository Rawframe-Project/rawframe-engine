// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Conversion between the two buffer layouts of the API: interleaved
// frames, which streams carry, and one array per channel, which the
// spatializer works on. Both hold 32-bit float samples.

#ifndef MAUL_AUDIO_BUFFER_H
#define MAUL_AUDIO_BUFFER_H

#include "maul-audio/base.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// Interleaves one array per channel into frames: sample `i` of channel
    /// `c` goes to `interleavedOut[i * channelCount + c]`. Samples are
    /// copied exactly. The arrays must not overlap.
    ///
    /// @param planar          One array of `frameCount` samples per channel.
    ///                        May be NULL when `frameCount` is 0.
    /// @param channelCount    The number of channels, at least 1.
    /// @param frameCount      The number of samples in each channel.
    /// @param interleavedOut  Room for `channelCount * frameCount` samples.
    ///                        May be NULL when `frameCount` is 0.
    /// @return `maud_success`, or `maud_errorInvalid` for a channel count of
    ///         0, a NULL array where samples are due, or a total that does
    ///         not fit in memory.
    /// @par Thread safety
    /// Real-time safe: no allocation, lock or wait.
    MAUD_NODISCARD MAUD_API maudResult maudInterleave(const float* const* planar,
                                                      uint32_t channelCount, uint32_t frameCount,
                                                      float* interleavedOut);

    /// Splits frames into one array per channel: `interleaved[i *
    /// channelCount + c]` goes to sample `i` of channel `c`. Samples are
    /// copied exactly. The arrays must not overlap.
    ///
    /// @param interleaved   `channelCount * frameCount` samples. May be NULL
    ///                      when `frameCount` is 0.
    /// @param channelCount  The number of channels, at least 1.
    /// @param frameCount    The number of frames.
    /// @param planarOut     One array with room for `frameCount` samples per
    ///                      channel. May be NULL when `frameCount` is 0.
    /// @return `maud_success`, or `maud_errorInvalid` for a channel count of
    ///         0, a NULL array where samples are due, or a total that does
    ///         not fit in memory.
    /// @par Thread safety
    /// Real-time safe: no allocation, lock or wait.
    MAUD_NODISCARD MAUD_API maudResult maudDeinterleave(const float* interleaved,
                                                        uint32_t channelCount, uint32_t frameCount,
                                                        float* const* planarOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_BUFFER_H
