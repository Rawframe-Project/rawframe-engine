// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Convolution in two stages (Gardner, "Efficient convolution without
// input-output delay", 1995): a head of small blocks b over the
// response's first 2T - b samples, and a tail of large blocks T over the
// rest, so that a long response costs about 2T / b + N / T complex
// multiply-adds a sample a channel instead of N / b. Output lags input
// by b, as the head's own overlap-save does: the tail convolves the
// response from 2T - b as if it started at 2T, which takes that block
// back. Tail input block j ends at (j + 1)T and is first heard at
// (j + 2)T, so its work is done in the period between, its channels
// shared out over the period's T / b head blocks, and its output played
// the period after: no call carries a tail block alone. A response the
// head covers alone keeps one stage.
//
// A new response takes over at a tail boundary, crossfaded over the
// tail block computed then, and the head switches when the output
// reaches the same point, crossfaded over its block.

#ifndef MAUL_AUDIO_SRC_STAGED_H
#define MAUL_AUDIO_SRC_STAGED_H

#include "partitioned.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The most channels a staged convolver runs.
#define MAUD_STAGED_CHANNELS 16u

// The sizes for a response of frames samples convolved in blocks of
// block: the tail's block (0 for one stage), each stage's partitions and
// the samples the head covers.
typedef struct maudStagedPlan
{
    uint32_t block;
    uint32_t tailBlock;
    uint32_t headPartitions;
    uint32_t tailPartitions;
    uint32_t headFrames;
} maudStagedPlan;

typedef struct maudStaged
{
    maudStagedPlan plan;
    uint32_t channels;
    maudPartitioned head;
    maudPartitioned tail;
    size_t headFloats;
    // The tail's input gathered this period, its output in two regions a
    // channel (one played while the other is computed), and where the
    // period stands.
    float* tailInput;
    float* tailOutput;
    uint32_t position;
    uint32_t period;
    // The response in use, the one being faded out of by the tail block
    // in progress (with fading set), and the one waiting for the next
    // tail boundary.
    const float* response;
    const float* previous;
    bool fading;
    const float* pending;
    bool waiting;
} maudStaged;

// The plan for a response of frames samples in blocks of block (a power
// of two from 2 to 16,384): T the power of two nearest sqrt(frames
// block / 2), at least 2 block, and one stage when the head covers the
// response alone.
maudStagedPlan maudPlanStaged(uint32_t block, uint32_t frames);

// The bytes a staged convolver needs, not counting responses; the floats
// a response takes; the floats maudCutStagedResponse's scratch holds.
size_t maudStagedBytes(const maudStagedPlan* plan, uint32_t channels);
size_t maudStagedResponseFloats(const maudStagedPlan* plan, uint32_t channels);
size_t maudStagedScratchFloats(const maudStagedPlan* plan);

// Makes a silent convolver of channels (at most MAUD_STAGED_CHANNELS) in
// memory of maudStagedBytes bytes, aligned for double.
void maudInitStaged(maudStaged* s, const maudStagedPlan* plan, uint32_t channels, void* memory);

// Cuts a response (channels of frames samples, frames at most the
// plan's) into response's maudStagedResponseFloats floats.
void maudCutStagedResponse(const maudStaged* s, const float* const* samples, uint32_t frames,
                           float* response, float* scratch);

// Uses response (or nothing, if NULL) from the next tail boundary on; it
// must stay unchanged while in use. Whether a change is still under way,
// before which the response in use and the one before it are both read.
void maudSetStagedResponse(maudStaged* s, const float* response);
bool maudStagedSwitching(const maudStaged* s);

// Convolves frames of input, adding each channel's output into out.
void maudRunStaged(maudStaged* s, const float* in, float* const* out, uint32_t frames);

// Silences the convolver's history (the response stays).
void maudResetStaged(maudStaged* s);

#endif // MAUL_AUDIO_SRC_STAGED_H
