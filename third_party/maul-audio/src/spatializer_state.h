// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A spatializer's state, shared by its files: spatializer.c (sources,
// steps, results) and spatializer_paths.c (probe sets and pathing).

#ifndef MAUL_AUDIO_SRC_SPATIALIZER_STATE_H
#define MAUL_AUDIO_SRC_SPATIALIZER_STATE_H

#include "pathing.h"
#include "probe_sets.h"
#include "reflections.h"
#include "reverb_estimate.h"

#include "maul-audio/spatializer.h"

#include <stdatomic.h>
#include <stdint.h>

// The flag on `shared` marking the waiting buffer newer.
#define FRESH 4u
// When the reverb's level is matched without reflections, and how long
// its output stays silent after its input (its shortest delay line).
#define LEVEL_AT     0.1f
#define REVERB_ONSET 0.023f

typedef struct Entry
{
    // The source's generation when the step ran; 0 for no source.
    uint32_t generation;
    maudDirectResult result;
} Entry;

typedef struct Buffer
{
    uint64_t step;
    Entry* entries;
    maudReverbResult reverb;
} Buffer;

typedef struct Slot
{
    uint32_t generation;
    bool live;
    maudPose pose;
    maudDirectivityPattern directivity;
    maudOcclusionMethod occlusion;
    float radius;
    uint32_t samples;
    bool transmission;
    bool pathing;
} Slot;

struct maudSpatializer
{
    maudAllocator allocator;
    uint32_t capacity;
    Slot* slots;
    // Free slots' indices, the next one taken from the end.
    uint32_t* free;
    uint32_t freeCount;
    uint32_t maxSamples;
    maudAnyHitFn* anyHit;
    maudClosestHitFn* closestHit;
    void* rayContext;
    uint32_t maxSurfaces;
    uint32_t materialCapacity;
    uint32_t materialCount;
    maudAcousticMaterial* materials;
    // Whether the round's rays go to the closest-hit query.
    bool closest;
    maudRayHit* hits;
    // The sources walking a transmission path, and how far each got.
    uint32_t* walkers;
    float* walked;
    maudEnqueueTaskFn* enqueueTask;
    maudFinishTaskFn* finishTask;
    void* userTaskContext;
    // The unit ball's points, a round's rays and answers, and where each
    // source's rays start in the round.
    maudVector3* points;
    maudRay* rays;
    uint8_t* occluded;
    uint32_t* offsets;
    uint32_t roundRays;
    // The reverberation estimate's trace, its batches' histograms and
    // the newest estimate.
    maudReverbTrace trace;
    maudReverbHistogram* histograms;
    // Geometric reflections, if the def asked for them, and their
    // response's length.
    maudReflections* reflections;
    float reflectionDuration;
    // Probe sets, if the def asked for any, and their limits.
    maudProbeSets* probeSets;
    uint32_t maxProbes;
    uint32_t maxProbePairs;
    // Pathing, if the def asked for it, the set in use and the most
    // sources a step paths.
    maudPathing* pathing;
    maudProbeSetId pathSet;
    // The baked set reverberation comes from.
    maudProbeSetId bakedSet;
    uint32_t maxPaths;
    maudReverbResult reverb;
    Buffer buffers[3];
    // The simulation side's buffer and step count.
    uint32_t back;
    uint64_t steps;
    // The rendering side's latched buffer.
    uint32_t front;
    _Atomic uint32_t shared;
};

// The host's queries and tasks, and the probe limits, for probe work.
maudProbeQueries maudSpatializerQueries(const maudSpatializer* s);

// Traces a reverberation estimate from a position into the histograms
// (fields not summed) and result's times, levels and delay; returns the
// batches traced. Needs the closest-hit query and reverberation rays.
uint32_t maudTraceFrom(maudSpatializer* s, maudVector3 position, maudReverbResult* result);

// An estimate from the baked set in use at a position, into the
// spatializer's reverberation result and, with reflections, published;
// false without a set in use or a probe in sight.
bool maudBakedReverb(maudSpatializer* s, maudVector3 position);

// A direct step's pathing: paths for the occluded sources that ask, up
// to maxPaths in slot order, over the set in use.
void maudPathSources(maudSpatializer* s, const maudPose* listener, Entry* entries);

#endif // MAUL_AUDIO_SRC_SPATIALIZER_STATE_H
