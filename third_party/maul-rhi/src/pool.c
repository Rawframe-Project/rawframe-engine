// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The id pool. A slot's generation is odd while it is in use and even
// while it is free, so a live check needs no separate flag.

#include "pool.h"

void mrhiPoolInit(mrhiPool* pool, uint32_t capacity, uint32_t* generations, uint32_t* nextFree)
{
    *pool = (mrhiPool){
        .capacity = capacity,
        .freeHead = capacity > 0 ? 1 : 0,
        .generations = generations,
        .nextFree = nextFree,
    };
    for (uint32_t i = 0; i < capacity; ++i)
    {
        generations[i] = 0;
        nextFree[i] = i + 1 < capacity ? i + 2 : 0;
    }
}

bool mrhiPoolAcquire(mrhiPool* pool, uint32_t* index1Out, uint32_t* generationOut)
{
    if (pool->freeHead == 0)
    {
        return false;
    }
    uint32_t index1 = pool->freeHead;
    pool->freeHead = pool->nextFree[index1 - 1];
    // Odd while live, so a live id's generation is never 0, even after the
    // counter wraps.
    uint32_t generation = pool->generations[index1 - 1] + 1;
    pool->generations[index1 - 1] = generation;
    ++pool->used;
    *index1Out = index1;
    *generationOut = generation;
    return true;
}

bool mrhiPoolIsLive(const mrhiPool* pool, uint32_t index1, uint32_t generation)
{
    return index1 != 0 && index1 <= pool->capacity && (generation & 1u) == 1u &&
           pool->generations[index1 - 1] == generation;
}

void mrhiPoolRelease(mrhiPool* pool, uint32_t index1)
{
    ++pool->generations[index1 - 1];
    pool->nextFree[index1 - 1] = pool->freeHead;
    pool->freeHead = index1;
    --pool->used;
}
