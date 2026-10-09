// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Reflections (reflections.h). The convolver's blocks are 128 samples
// and the response is advanced by one block: the block's latency is
// taken back, and only what arrives in its first block (walls within a
// few decimetres) is lost. The bed is rendered and turned 128 frames at a
// time, the orientation interpolated across the call.

#include "reflections.h"

#include "allocator.h"
#include "partitioned.h"
#include "reflection_response.h"

#include "maul-audio/ambisonics.h"
#include "maul-audio/direct.h"

#include <math.h>
#include <stdatomic.h>
#include <string.h>

#define BLOCK       128u
#define CHUNK       128u
#define FRESH       4u
#define BIN_SECONDS 0.01

struct maudReflections
{
    maudAllocator allocator;
    uint32_t order;
    uint32_t channels;
    double rate;
    // The response's samples, the bins that cover them and the block
    // dropped before them, and the partitions they make.
    uint32_t frames;
    uint32_t bins;
    uint32_t partitions;
    uint32_t batches;
    size_t fieldFloats;
    size_t responseFloats;
    float* field;
    float* samples;
    double* scratch;
    float* cutScratch;
    float* responses;
    // The simulation side's buffer, the published one (with FRESH when
    // newer than the rendering side's), the rendering side's two.
    uint32_t back;
    _Atomic uint32_t shared;
    uint32_t front;
    uint32_t retiring;
    maudPartitioned convolver;
    void* convolverMemory;
    float* chunk;
    maudQuaternion last;
    bool oriented;
};

static size_t Bytes(uint32_t channels, size_t floats)
{
    return (size_t)channels * floats * sizeof(float);
}

static void Release(maudReflections* r)
{
    maudAllocator a = r->allocator;
    void* blocks[7] = {r->field,     r->samples,         r->scratch, r->cutScratch,
                       r->responses, r->convolverMemory, r->chunk};
    const size_t sizes[7] = {(size_t)r->batches * r->fieldFloats * sizeof(float),
                             Bytes(r->channels, (size_t)r->frames + BLOCK),
                             maudResponseScratch(r->rate) * sizeof(double),
                             (4 * (size_t)BLOCK + 2) * sizeof(float),
                             4 * r->responseFloats * sizeof(float),
                             maudPartitionedBytes(BLOCK, r->partitions, r->channels),
                             Bytes(r->channels, CHUNK)};
    for (int i = 0; i < 7; ++i)
    {
        if (blocks[i] != nullptr)
        {
            maudRelease(&a, blocks[i], sizes[i], alignof(double));
        }
    }
    maudRelease(&a, r, sizeof(maudReflections), alignof(maudReflections));
}

maudReflections* maudCreateReflections(const maudAllocator* allocator, uint32_t order,
                                       float duration, float rate, uint32_t batches)
{
    maudReflections* r = maudAllocate(allocator, sizeof(maudReflections), alignof(maudReflections));
    if (r == nullptr)
    {
        return nullptr;
    }
    uint32_t frames = (uint32_t)ceil((double)duration * (double)rate);
    uint32_t hop = maudResponseHop((double)rate);
    *r = (maudReflections){
        .allocator = *allocator,
        .order = order,
        .channels = (order + 1) * (order + 1),
        .rate = (double)rate,
        .frames = frames,
        .bins = (frames + BLOCK + hop - 1) / hop,
        .partitions = (frames + BLOCK - 1) / BLOCK,
        .batches = batches,
        .back = 0,
        .front = 2,
        .retiring = 3,
    };
    atomic_init(&r->shared, 1u);
    r->fieldFloats = (size_t)r->channels * MAUD_DIRECT_BANDS * r->bins;
    r->responseFloats = maudResponseFloats(BLOCK, r->partitions, r->channels);
    r->field =
        maudAllocate(allocator, (size_t)batches * r->fieldFloats * sizeof(float), alignof(double));
    r->samples =
        maudAllocate(allocator, Bytes(r->channels, (size_t)frames + BLOCK), alignof(double));
    r->scratch =
        maudAllocate(allocator, maudResponseScratch(r->rate) * sizeof(double), alignof(double));
    r->cutScratch =
        maudAllocate(allocator, (4 * (size_t)BLOCK + 2) * sizeof(float), alignof(double));
    r->responses = maudAllocate(allocator, 4 * r->responseFloats * sizeof(float), alignof(double));
    r->convolverMemory = maudAllocate(
        allocator, maudPartitionedBytes(BLOCK, r->partitions, r->channels), alignof(double));
    r->chunk = maudAllocate(allocator, Bytes(r->channels, CHUNK), alignof(double));
    if (r->field == nullptr || r->samples == nullptr || r->scratch == nullptr ||
        r->cutScratch == nullptr || r->responses == nullptr || r->convolverMemory == nullptr ||
        r->chunk == nullptr)
    {
        Release(r);
        return nullptr;
    }
    memset(r->responses, 0, 4 * r->responseFloats * sizeof(float));
    maudInitPartitioned(&r->convolver, BLOCK, r->partitions, r->channels, r->convolverMemory);
    return r;
}

void maudDestroyReflections(maudReflections* reflections)
{
    if (reflections != nullptr)
    {
        Release(reflections);
    }
}

uint32_t maudReflectionsOrder(const maudReflections* reflections)
{
    return reflections->order;
}

void maudPrepareReflections(maudReflections* reflections, maudReverbTrace* trace,
                            maudReverbHistogram* histograms, uint32_t batches)
{
    trace->fieldOrder = reflections->order;
    trace->fieldBins = reflections->bins;
    for (uint32_t b = 0; b < batches; ++b)
    {
        histograms[b].field = reflections->field + (size_t)b * reflections->fieldFloats;
    }
}

void maudPublishReflections(maudReflections* reflections, const maudReverbTrace* trace,
                            maudReverbHistogram* histograms, uint32_t batches)
{
    maudReflections* r = reflections;
    float* response = r->responses + (size_t)r->back * r->responseFloats;
    maudSumReverbFields(trace, histograms, batches);
    float* channels[16];
    const float* advanced[16];
    for (uint32_t c = 0; c < r->channels; ++c)
    {
        channels[c] = r->samples + (size_t)c * ((size_t)r->frames + BLOCK);
        advanced[c] = channels[c] + BLOCK;
    }
    maudReconstructResponse(histograms[0].field, r->order, r->bins, r->rate, channels,
                            r->frames + BLOCK, r->scratch);
    maudPartitionResponse(&r->convolver, advanced, r->frames, response, r->cutScratch);
    uint32_t old = atomic_exchange_explicit(&r->shared, r->back | FRESH, memory_order_acq_rel);
    r->back = old & (FRESH - 1);
}

// The orientation a fraction t of the way from a to b (normalized
// linear interpolation along the shorter way), conjugated: it turns the
// world's axes into the listener's.
static maudQuaternion Toward(maudQuaternion a, maudQuaternion b, float t)
{
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    float s = dot < 0.0f ? -1.0f : 1.0f;
    maudQuaternion q = {a.x + t * (s * b.x - a.x), a.y + t * (s * b.y - a.y),
                        a.z + t * (s * b.z - a.z), a.w + t * (s * b.w - a.w)};
    float n = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return (maudQuaternion){-q.x / n, -q.y / n, -q.z / n, q.w / n};
}

// Takes the newest published response when no swap is pending: the one
// replaced goes back only at the next exchange, after its crossfade.
static void Take(maudReflections* r)
{
    if (r->convolver.next != r->convolver.response ||
        (atomic_load_explicit(&r->shared, memory_order_relaxed) & FRESH) == 0)
    {
        return;
    }
    uint32_t old = atomic_exchange_explicit(&r->shared, r->retiring, memory_order_acq_rel);
    r->retiring = r->front;
    r->front = old & (FRESH - 1);
    maudSetPartitionedResponse(&r->convolver, r->responses + (size_t)r->front * r->responseFloats);
}

void maudConvolveReflections(maudReflections* reflections, const maudQuaternion* orientation,
                             const float* send, float* const* bed, uint32_t frames)
{
    maudReflections* r = reflections;
    if (!r->oriented)
    {
        r->last = *orientation;
        r->oriented = true;
    }
    float* chunk[16];
    for (uint32_t c = 0; c < r->channels; ++c)
    {
        chunk[c] = r->chunk + (size_t)c * CHUNK;
    }
    for (uint32_t done = 0; done < frames;)
    {
        Take(r);
        uint32_t n = frames - done < CHUNK ? frames - done : CHUNK;
        memset(r->chunk, 0, Bytes(r->channels, CHUNK));
        maudRunPartitioned(&r->convolver, send + done, chunk, n);
        maudQuaternion from = Toward(r->last, *orientation, (float)done / (float)frames);
        maudQuaternion to = Toward(r->last, *orientation, (float)(done + n) / (float)frames);
        if (maudRotateAmbisonic(r->order, &from, &to, chunk, n) == maud_success)
        {
            for (uint32_t c = 0; c < r->channels; ++c)
            {
                for (uint32_t i = 0; i < n; ++i)
                {
                    bed[c][done + i] += chunk[c][i];
                }
            }
        }
        done += n;
    }
    r->last = *orientation;
}
