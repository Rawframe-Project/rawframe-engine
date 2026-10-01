// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "pool.h"

#include "invariant.h"

void muiPoolInit(muiPool* pool, muiPoolSlot* slots, uint32_t capacity)
{
    *pool = (muiPool){.slots = slots, .capacity = capacity};
}

uint32_t muiPoolTake(muiPool* pool)
{
    uint32_t slot = pool->freeHead;
    uint32_t generation = 1;
    if (slot != 0)
    {
        pool->freeHead = pool->slots[slot - 1].nextFree;
        generation = pool->slots[slot - 1].generation;
    }
    else if (pool->used < pool->capacity)
    {
        slot = ++pool->used;
    }
    else
    {
        return 0;
    }
    pool->slots[slot - 1] = (muiPoolSlot){.generation = generation, .live = true};
    return slot;
}

void muiPoolGive(muiPool* pool, uint32_t slot)
{
    MUI_ASSERT(slot != 0 && slot <= pool->used && pool->slots[slot - 1].live);
    muiPoolSlot* entry = &pool->slots[slot - 1];
    uint32_t generation = entry->generation + 1;
    *entry = (muiPoolSlot){
        // Generation 0 is never handed out, so a zeroed id never resolves.
        .generation = generation != 0 ? generation : 1,
        .nextFree = pool->freeHead,
    };
    pool->freeHead = slot;
}

uint32_t muiPoolResolve(const muiPool* pool, uint32_t index1, uint32_t generation)
{
    if (index1 == 0 || index1 > pool->used)
    {
        return 0;
    }
    const muiPoolSlot* entry = &pool->slots[index1 - 1];
    return entry->live && entry->generation == generation ? index1 : 0;
}

uint32_t muiPoolGeneration(const muiPool* pool, uint32_t slot)
{
    MUI_ASSERT(slot != 0 && slot <= pool->used);
    return pool->slots[slot - 1].generation;
}
