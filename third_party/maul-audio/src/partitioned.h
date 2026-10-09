// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Uniformly partitioned overlap-save convolution of one input with a
// response of several channels. The response is cut into partitions of
// b samples, each zero-padded to 2b and transformed (maudPartitionResponse,
// off the audio thread); the input is gathered b samples at a time, each
// block's spectrum (with the block before) kept in a ring as long as the
// response, and every channel's next b samples are the ring times its
// partitions, summed and transformed back. Output lags input by b. A new
// response takes over across one block, crossfaded from the old.

#ifndef MAUL_AUDIO_SRC_PARTITIONED_H
#define MAUL_AUDIO_SRC_PARTITIONED_H

#include "real_fft.h"

#include <stddef.h>
#include <stdint.h>

typedef struct maudPartitioned
{
    maudRealFft fft;
    uint32_t block;
    uint32_t partitions;
    uint32_t channels;
    // The last 2b input samples, the newest b filling from the middle.
    float* input;
    uint32_t filled;
    // The ring of input spectra, partitions of b + 1 complex bins, and
    // where the newest is.
    float* ring;
    uint32_t newest;
    // Each channel's b output samples, read from the start as input
    // fills.
    float* output;
    // Scratch: a spectrum and 2b samples.
    float* spectrum;
    float* samples;
    // The response in use and the one taking over at the next block
    // (NULL for none).
    const float* response;
    const float* next;
} maudPartitioned;

// The bytes a convolver of these sizes needs (block a power of two from
// 2 to 32,768), not counting responses.
size_t maudPartitionedBytes(uint32_t block, uint32_t partitions, uint32_t channels);

// The floats a response takes: channels of partitions of b + 1 complex
// bins.
size_t maudResponseFloats(uint32_t block, uint32_t partitions, uint32_t channels);

// Makes a silent convolver in memory of maudPartitionedBytes bytes,
// aligned for float.
void maudInitPartitioned(maudPartitioned* p, uint32_t block, uint32_t partitions, uint32_t channels,
                         void* memory);

// Cuts a response (channels of frames samples, frames at most block
// times partitions) into a convolver's partitions: response holds
// maudResponseFloats floats. scratch holds 2b floats plus 2b + 2.
void maudPartitionResponse(const maudPartitioned* p, const float* const* samples, uint32_t frames,
                           float* response, float* scratch);

// Uses response (or nothing, if NULL) from the next block on, crossfaded
// over it. The response must stay unchanged while in use.
void maudSetPartitionedResponse(maudPartitioned* p, const float* response);

// Convolves frames of input, adding each channel's output into out.
void maudRunPartitioned(maudPartitioned* p, const float* in, float* const* out, uint32_t frames);

// Silences the convolver's history (the response stays).
void maudResetPartitioned(maudPartitioned* p);

#endif // MAUL_AUDIO_SRC_PARTITIONED_H
