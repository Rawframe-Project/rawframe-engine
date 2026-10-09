// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A spatializer's probe sets and pathing (spatializer_state.h): sets
// are made, read and destroyed here, one is chosen for pathing, and the
// direct step hands its occluded sources to the pathing.

#include "bake_file.h"
#include "spatializer_state.h"

#include <string.h>

#define PROBE_SET_DEF_COOKIE 0x6D617062u

maudProbeQueries maudSpatializerQueries(const maudSpatializer* s)
{
    return (maudProbeQueries){
        .anyHit = s->anyHit,
        .closestHit = s->closestHit,
        .rayContext = s->rayContext,
        .enqueueTask = s->enqueueTask,
        .finishTask = s->finishTask,
        .userTaskContext = s->userTaskContext,
        .allocator = &s->allocator,
        .maxProbes = s->maxProbes,
        .maxPairs = s->maxProbePairs,
    };
}

void maudPathSources(maudSpatializer* s, const maudPose* listener, Entry* entries)
{
    const maudProbeGraph* graph = nullptr;
    if (s->pathing == nullptr || s->pathSet.index1 == 0 ||
        maudFindProbeSet(s->probeSets, s->pathSet, &graph) != maud_success)
    {
        return;
    }
    maudPathJob* jobs = maudPathJobs(s->pathing);
    uint32_t count = 0;
    for (uint32_t i = 0; i < s->capacity && count < s->maxPaths; ++i)
    {
        const Slot* slot = &s->slots[i];
        if (slot->live && slot->pathing && entries[i].result.occlusion > 0.0f)
        {
            jobs[count++] = (maudPathJob){&slot->pose, &slot->directivity, &entries[i].result};
        }
    }
    if (count > 0)
    {
        maudProbeQueries queries = maudSpatializerQueries(s);
        maudRunPathing(s->pathing, graph, &queries, listener, count);
    }
}

maudProbeSetDef maudDefaultProbeSetDef(void)
{
    return (maudProbeSetDef){
        .cookie = PROBE_SET_DEF_COOKIE,
        .points = nullptr,
        .pointCount = 0,
        .boxMin = {0.0f, 0.0f, 0.0f},
        .boxMax = {0.0f, 0.0f, 0.0f},
        .spacing = 2.0f,
        .height = 1.5f,
        .range = 5.0f,
    };
}

maudResult maudCreateProbeSet(maudSpatializer* spatializer, const maudProbeSetDef* def,
                              maudProbeSetId* setOut)
{
    if (setOut != nullptr)
    {
        *setOut = (maudProbeSetId){0, 0};
    }
    if (spatializer == nullptr || def == nullptr || setOut == nullptr ||
        def->cookie != PROBE_SET_DEF_COOKIE || !maudProbeSetDefValid(def))
    {
        return maud_errorInvalid;
    }
    maudSpatializer* s = spatializer;
    if (s->probeSets == nullptr)
    {
        return maud_errorCapacity;
    }
    maudProbeQueries queries = maudSpatializerQueries(s);
    return maudAddProbeSet(s->probeSets, &queries, def, setOut);
}

maudResult maudDestroyProbeSet(maudSpatializer* spatializer, maudProbeSetId set)
{
    if (spatializer == nullptr)
    {
        return maud_errorInvalid;
    }
    maudResult result = maudRemoveProbeSet(spatializer->probeSets, set);
    if (result == maud_success && set.index1 == spatializer->pathSet.index1 &&
        set.generation == spatializer->pathSet.generation)
    {
        spatializer->pathSet = (maudProbeSetId){0, 0};
    }
    if (result == maud_success && set.index1 == spatializer->bakedSet.index1 &&
        set.generation == spatializer->bakedSet.generation)
    {
        spatializer->bakedSet = (maudProbeSetId){0, 0};
    }
    return result;
}

maudResult maudSetPathing(maudSpatializer* spatializer, maudProbeSetId set)
{
    if (spatializer == nullptr)
    {
        return maud_errorInvalid;
    }
    if (set.index1 != 0 || set.generation != 0)
    {
        const maudProbeGraph* graph = nullptr;
        maudResult result = maudFindProbeSet(spatializer->probeSets, set, &graph);
        if (result != maud_success)
        {
            return result;
        }
    }
    spatializer->pathSet = set;
    return maud_success;
}

maudResult maudGetProbeSet(const maudSpatializer* spatializer, maudProbeSetId set,
                           maudProbeSetInfo* infoOut, uint32_t first, uint32_t count,
                           maudVector3* points)
{
    if (spatializer == nullptr || infoOut == nullptr)
    {
        return maud_errorInvalid;
    }
    const maudProbeGraph* graph = nullptr;
    maudProbeBake* bake = nullptr;
    maudResult result = maudFindProbeBake(spatializer->probeSets, set, &graph, &bake);
    if (result != maud_success)
    {
        return result;
    }
    if (points != nullptr && (first > graph->count || count > graph->count - first))
    {
        return maud_errorInvalid;
    }
    *infoOut = (maudProbeSetInfo){graph->count, graph->links, bake->memory != nullptr};
    if (points != nullptr && count > 0)
    {
        memcpy(points, graph->points + first, (size_t)count * sizeof(maudVector3));
    }
    return maud_success;
}

// The floats of a probe's field: the bed's channels, 3 bands, its bins.
static uint32_t FieldFloats(const maudSpatializer* s)
{
    if (s->reflections == nullptr)
    {
        return 0;
    }
    uint32_t channels = (s->trace.fieldOrder + 1) * (s->trace.fieldOrder + 1);
    return channels * MAUD_DIRECT_BANDS * s->trace.fieldBins;
}

maudResult maudBakeProbeSet(maudSpatializer* spatializer, maudProbeSetId set)
{
    if (spatializer == nullptr)
    {
        return maud_errorInvalid;
    }
    maudSpatializer* s = spatializer;
    const maudProbeGraph* graph = nullptr;
    maudProbeBake* bake = nullptr;
    maudResult result = maudFindProbeBake(s->probeSets, set, &graph, &bake);
    if (result != maud_success)
    {
        return result;
    }
    if (s->trace.rays == 0 || s->closestHit == nullptr)
    {
        return maud_errorState;
    }
    maudReleaseProbeBake(&s->allocator, bake);
    uint32_t fieldFloats = FieldFloats(s);
    if (!maudCreateProbeBake(&s->allocator, graph->count, fieldFloats, bake))
    {
        return maud_errorCapacity;
    }
    for (uint32_t p = 0; p < graph->count; ++p)
    {
        maudReverbResult estimate = {0};
        uint32_t batches = maudTraceFrom(s, graph->points[p], &estimate);
        memcpy(bake->times + (size_t)p * MAUD_DIRECT_BANDS, estimate.reverbTime,
               sizeof(estimate.reverbTime));
        memcpy(bake->levels + (size_t)p * MAUD_DIRECT_BANDS, estimate.level,
               sizeof(estimate.level));
        memcpy(bake->tailTimes + (size_t)p * MAUD_DIRECT_BANDS, estimate.tailTime,
               sizeof(estimate.tailTime));
        memcpy(bake->tailLevels + (size_t)p * MAUD_DIRECT_BANDS, estimate.tailLevel,
               sizeof(estimate.tailLevel));
        if (fieldFloats > 0)
        {
            maudSumReverbFields(&s->trace, s->histograms, batches);
            memcpy(bake->fields + (size_t)p * fieldFloats, s->histograms[0].field,
                   (size_t)fieldFloats * sizeof(float));
        }
    }
    return maud_success;
}

maudResult maudUseBakedReverb(maudSpatializer* spatializer, maudProbeSetId set)
{
    if (spatializer == nullptr)
    {
        return maud_errorInvalid;
    }
    if (set.index1 != 0 || set.generation != 0)
    {
        const maudProbeGraph* graph = nullptr;
        maudProbeBake* bake = nullptr;
        maudResult result = maudFindProbeBake(spatializer->probeSets, set, &graph, &bake);
        if (result != maud_success)
        {
            return result;
        }
        if (bake->memory == nullptr)
        {
            return maud_errorState;
        }
    }
    spatializer->bakedSet = set;
    return maud_success;
}

bool maudBakedReverb(maudSpatializer* s, maudVector3 position)
{
    const maudProbeGraph* graph = nullptr;
    maudProbeBake* bake = nullptr;
    if (s->bakedSet.index1 == 0 ||
        maudFindProbeBake(s->probeSets, s->bakedSet, &graph, &bake) != maud_success)
    {
        return false;
    }
    float* field = s->reflections != nullptr ? s->histograms[0].field : nullptr;
    if (!maudInterpolateBake(graph, bake, s->anyHit, s->rayContext, position, &s->reverb, field))
    {
        return false;
    }
    bool hybrid = s->reflections != nullptr;
    s->reverb.delay = hybrid ? s->reflectionDuration - REVERB_ONSET : 0.0f;
    if (hybrid)
    {
        maudPublishReflections(s->reflections, &s->trace, s->histograms, 1);
    }
    return true;
}

maudResult maudSaveProbeSet(const maudSpatializer* spatializer, maudProbeSetId set, void* bytes,
                            size_t capacity, size_t* sizeOut)
{
    if (spatializer == nullptr || sizeOut == nullptr)
    {
        return maud_errorInvalid;
    }
    *sizeOut = 0;
    const maudSpatializer* s = spatializer;
    const maudProbeGraph* graph = nullptr;
    maudProbeBake* bake = nullptr;
    maudResult result = maudFindProbeBake(s->probeSets, set, &graph, &bake);
    if (result != maud_success)
    {
        return result;
    }
    size_t size = maudBakeFileBytes(graph, bake, s->trace.fieldOrder, s->trace.fieldBins);
    if (size == 0)
    {
        return maud_errorCapacity;
    }
    *sizeOut = size;
    if (bytes == nullptr)
    {
        return maud_success;
    }
    if (capacity < size)
    {
        return maud_errorCapacity;
    }
    maudWriteBakeFile(graph, bake, s->trace.fieldOrder, s->trace.fieldBins, bytes);
    return maud_success;
}

maudResult maudLoadProbeSet(maudSpatializer* spatializer, const void* bytes, size_t size,
                            maudProbeSetId* setOut)
{
    if (setOut != nullptr)
    {
        *setOut = (maudProbeSetId){0, 0};
    }
    if (spatializer == nullptr || bytes == nullptr || setOut == nullptr)
    {
        return maud_errorInvalid;
    }
    maudSpatializer* s = spatializer;
    if (s->probeSets == nullptr)
    {
        return maud_errorCapacity;
    }
    maudBakeLimits limits = {s->maxProbes, s->maxProbePairs,
                             s->reflections != nullptr ? s->trace.fieldOrder : 0,
                             s->reflections != nullptr ? s->trace.fieldBins : 0};
    maudProbeGraph graph;
    maudProbeBake bake;
    maudResult result = maudReadBakeFile(bytes, size, &limits, &s->allocator, &graph, &bake);
    if (result != maud_success)
    {
        return result;
    }
    result = maudAdoptProbeSet(s->probeSets, &graph, &bake, setOut);
    if (result != maud_success)
    {
        maudReleaseProbeGraph(&s->allocator, &graph);
        maudReleaseProbeBake(&s->allocator, &bake);
    }
    return result;
}
