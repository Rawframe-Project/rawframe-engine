// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A listener's geometric reflections: the simulation side turns a
// traced energy field into a response (reflection_response.h), cuts it
// into a convolver's partitions and publishes it; the rendering side
// convolves the host's send with the newest published response
// (partitioned.h), crossfading over one block, and turns the result from
// the world's axes into the listener's. Responses pass through four
// buffers: the one being written, the one published, the one in use and
// the one it replaced, kept until the next exchange because the
// crossfade reads it.

#ifndef MAUL_AUDIO_SRC_REFLECTIONS_H
#define MAUL_AUDIO_SRC_REFLECTIONS_H

#include "reverb_estimate.h"

#include "maul-audio/base.h"

#include <stdint.h>

typedef struct maudReflections maudReflections;

// Makes reflections of order 1 to 3 lasting duration seconds (0.05 to
// 4) at rate for traces of batches batches; NULL if the allocator fails.
// Silent until a response is published.
maudReflections* maudCreateReflections(const maudAllocator* allocator, uint32_t order,
                                       float duration, float rate, uint32_t batches);

void maudDestroyReflections(maudReflections* reflections);

// Sets the trace's field and points each batch's histogram at its share
// of the field memory.
void maudPrepareReflections(maudReflections* reflections, maudReverbTrace* trace,
                            maudReverbHistogram* histograms, uint32_t batches);

// Builds the response from the traced histograms (their fields summed
// into the first) and publishes it. Responses start silent and only
// traced ones are published, so without geometry reflections stay
// silent.
void maudPublishReflections(maudReflections* reflections, const maudReverbTrace* trace,
                            maudReverbHistogram* histograms, uint32_t batches);

// Adds frames of the send's reflections into a bed of the order's
// channels, turned into the frame of a listener whose orientation moves
// to `orientation` across the call.
void maudConvolveReflections(maudReflections* reflections, const maudQuaternion* orientation,
                             const float* send, float* const* bed, uint32_t frames);

uint32_t maudReflectionsOrder(const maudReflections* reflections);

#endif // MAUL_AUDIO_SRC_REFLECTIONS_H
