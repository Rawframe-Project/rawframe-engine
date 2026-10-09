// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The bake (probe_bake.h): one block for the arrays; the neighbours come
// from the pathing's attachment (nearest first, in sight), and the log
// and exp of the time blend are the portable ones, so a blend gives the
// same bits everywhere.

#include "probe_bake.h"

#include "allocator.h"
#include "path_search.h"
#include "portable_math.h"

#include <string.h>

// Probes blended at most.
#define BLEND 4u

bool maudCreateProbeBake(const maudAllocator* allocator, uint32_t count, uint32_t fieldFloats,
                         maudProbeBake* bake)
{
    *bake = (maudProbeBake){.count = count, .fieldFloats = fieldFloats};
    maudLayout layout = {0};
    size_t times =
        maudLayoutAdd(&layout, (size_t)count * MAUD_DIRECT_BANDS, sizeof(float), alignof(float));
    size_t levels =
        maudLayoutAdd(&layout, (size_t)count * MAUD_DIRECT_BANDS, sizeof(float), alignof(float));
    size_t tailTimes =
        maudLayoutAdd(&layout, (size_t)count * MAUD_DIRECT_BANDS, sizeof(float), alignof(float));
    size_t tailLevels =
        maudLayoutAdd(&layout, (size_t)count * MAUD_DIRECT_BANDS, sizeof(float), alignof(float));
    size_t fields =
        maudLayoutAdd(&layout, (size_t)count * fieldFloats, sizeof(float), alignof(float));
    if (layout.overflow)
    {
        return false;
    }
    bake->bytes = layout.size > 0 ? layout.size : sizeof(float);
    bake->memory = maudAllocate(allocator, bake->bytes, alignof(float));
    if (bake->memory == nullptr)
    {
        *bake = (maudProbeBake){0};
        return false;
    }
    unsigned char* base = bake->memory;
    bake->times = (float*)(base + times);
    bake->levels = (float*)(base + levels);
    bake->tailTimes = (float*)(base + tailTimes);
    bake->tailLevels = (float*)(base + tailLevels);
    bake->fields = fieldFloats > 0 ? (float*)(base + fields) : nullptr;
    return true;
}

void maudReleaseProbeBake(const maudAllocator* allocator, maudProbeBake* bake)
{
    if (bake->memory != nullptr)
    {
        maudRelease(allocator, bake->memory, bake->bytes, alignof(float));
    }
    *bake = (maudProbeBake){0};
}

// The blend's weights for the attached probes, normalised; how many.
static uint32_t Weights(const maudPathEnd* end, double* weights)
{
    uint32_t n = end->count < BLEND ? end->count : BLEND;
    if (end->lengths[0] == 0.0f)
    {
        weights[0] = 1.0;
        return 1;
    }
    double cut = end->count > BLEND ? 1.0 / (double)end->lengths[BLEND] : 0.0;
    double sum = 0.0;
    for (uint32_t k = 0; k < n; ++k)
    {
        weights[k] = 1.0 / (double)end->lengths[k] - cut;
        sum += weights[k];
    }
    for (uint32_t k = 0; k < n; ++k)
    {
        weights[k] /= sum;
    }
    return n;
}

// The tail's blend in a band: its level in dB (-96 for a probe without
// one) and its time in log over the probes with one, their weights
// renormalised; 0 s and -96 dB where none has one.
static void BlendTail(const maudProbeBake* bake, const maudPathEnd* end, const double* weights,
                      uint32_t n, int b, maudReverbResult* result)
{
    double logTime = 0.0;
    double level = 0.0;
    double weight = 0.0;
    for (uint32_t k = 0; k < n; ++k)
    {
        size_t at = (size_t)end->probes[k] * MAUD_DIRECT_BANDS + (size_t)b;
        bool has = bake->tailTimes[at] > 0.0f;
        level += weights[k] * (has ? (double)bake->tailLevels[at] : -96.0);
        logTime += has ? weights[k] * maudLog((double)bake->tailTimes[at]) : 0.0;
        weight += has ? weights[k] : 0.0;
    }
    result->tailTime[b] = weight > 0.0 ? (float)maudExp(logTime / weight) : 0.0f;
    result->tailLevel[b] = weight > 0.0 ? (float)level : -96.0f;
}

bool maudInterpolateBake(const maudProbeGraph* graph, const maudProbeBake* bake,
                         maudAnyHitFn* anyHit, void* context, maudVector3 point,
                         maudReverbResult* result, float* field)
{
    maudPathEnd end;
    maudAttachPath(graph, graph->range, point, anyHit, context, &end);
    if (end.count == 0)
    {
        return false;
    }
    double weights[BLEND];
    uint32_t n = Weights(&end, weights);
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        double logTime = 0.0;
        double level = 0.0;
        for (uint32_t k = 0; k < n; ++k)
        {
            size_t at = (size_t)end.probes[k] * MAUD_DIRECT_BANDS + (size_t)b;
            logTime += weights[k] * maudLog((double)bake->times[at]);
            level += weights[k] * (double)bake->levels[at];
        }
        result->reverbTime[b] = (float)maudExp(logTime);
        result->level[b] = (float)level;
        BlendTail(bake, &end, weights, n, b, result);
    }
    if (field != nullptr && bake->fieldFloats > 0)
    {
        for (uint32_t i = 0; i < bake->fieldFloats; ++i)
        {
            double sum = 0.0;
            for (uint32_t k = 0; k < n; ++k)
            {
                sum += weights[k] *
                       (double)bake->fields[(size_t)end.probes[k] * bake->fieldFloats + i];
            }
            field[i] = (float)sum;
        }
    }
    return true;
}
