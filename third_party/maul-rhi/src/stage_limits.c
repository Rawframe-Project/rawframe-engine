// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Per-stage resource limits under a total (stage_limits.h).

#include "stage_limits.h"

enum
{
    KINDS = 5
};

static uint32_t Capped(uint32_t limit, uint32_t floor, uint32_t cap)
{
    uint32_t most = floor > cap ? floor : cap;
    return limit < most ? limit : most;
}

void mrhiFitStageLimits(mrhiLimits* limits, uint32_t total)
{
    const mrhiLimits floor = mrhiDefaultLimits();
    uint32_t* const kinds[KINDS] = {&limits->sampledTexturesPerStage, &limits->samplersPerStage,
                                    &limits->storageBuffersPerStage,
                                    &limits->storageTexturesPerStage,
                                    &limits->uniformBuffersPerStage};
    const uint32_t floors[KINDS] = {floor.sampledTexturesPerStage, floor.samplersPerStage,
                                    floor.storageBuffersPerStage, floor.storageTexturesPerStage,
                                    floor.uniformBuffersPerStage};
    uint64_t sum = 0;
    uint32_t highest = 0;
    for (size_t i = 0; i < KINDS; ++i)
    {
        sum += *kinds[i];
        highest = *kinds[i] > highest ? *kinds[i] : highest;
    }
    if (sum <= total)
    {
        return;
    }
    // The highest cap whose capped limits fit, by halving: the sum grows
    // with the cap.
    uint32_t low = 0;
    uint32_t high = highest;
    while (low < high)
    {
        uint32_t cap = low + (high - low + 1) / 2;
        uint64_t capped = 0;
        for (size_t i = 0; i < KINDS; ++i)
        {
            capped += Capped(*kinds[i], floors[i], cap);
        }
        if (capped <= total)
        {
            low = cap;
        }
        else
        {
            high = cap - 1;
        }
    }
    for (size_t i = 0; i < KINDS; ++i)
    {
        *kinds[i] = Capped(*kinds[i], floors[i], low);
    }
}
