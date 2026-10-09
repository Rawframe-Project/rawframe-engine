// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A probe set's bake: per probe the reverberation times and levels and,
// when the spatializer renders reflections, the energy field. A point
// takes the 4 nearest probes it sees within the set's range, weighted
// by 1/d less 1/d of the fifth nearest (0 without a fifth), normalised,
// so a probe's weight reaches 0 before it leaves the four; times blend
// in log, levels in dB, fields linearly. A point on a probe takes that
// probe's values exactly: its weight is 1, and a time's round trip
// through log and exp in double lands back on the same float.

#ifndef MAUL_AUDIO_SRC_PROBE_BAKE_H
#define MAUL_AUDIO_SRC_PROBE_BAKE_H

#include "probe_graph.h"

#include <stdint.h>

typedef struct maudProbeBake
{
    uint32_t count;
    // Floats of a probe's field; 0 for none.
    uint32_t fieldFloats;
    float* times;
    float* levels;
    // The slower slope's, as maudReverbResult's tail: 0 s and -96 dB for
    // none.
    float* tailTimes;
    float* tailLevels;
    float* fields;
    void* memory;
    size_t bytes;
} maudProbeBake;

// Allocates a bake for count probes; false when memory runs out or the
// sizes overflow (nothing to release then).
bool maudCreateProbeBake(const maudAllocator* allocator, uint32_t count, uint32_t fieldFloats,
                         maudProbeBake* bake);
void maudReleaseProbeBake(const maudAllocator* allocator, maudProbeBake* bake);

// The bake's values at a point, as a reverberation estimate's: times,
// levels and the tail's (3 each) and, if the bake has fields and field is
// not NULL, the field. Each slope is blended apart, times in log and
// levels in dB; a probe without a tail weighs in at -96 dB, and the
// tail's time is blended over the probes with one. False when no probe
// is in sight (nothing written).
bool maudInterpolateBake(const maudProbeGraph* graph, const maudProbeBake* bake,
                         maudAnyHitFn* anyHit, void* context, maudVector3 point,
                         maudReverbResult* result, float* field);

#endif // MAUL_AUDIO_SRC_PROBE_BAKE_H
