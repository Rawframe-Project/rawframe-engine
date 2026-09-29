// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Generation-checked ids over a fixed number of slots (family record
// 0016): a free list, and a generation per slot that a release bumps,
// so that an id kept past its object's end is refused. The arrays come
// from the owner's block; the pool never allocates.

#ifndef MAUL_RHI_SRC_POOL_H
#define MAUL_RHI_SRC_POOL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct mrhiPool
{
    uint32_t capacity;
    uint32_t used;
    // The first free slot's index plus one, or 0 when none is free.
    uint32_t freeHead;
    // Per slot: its generation, and the next free slot's index plus one.
    uint32_t* generations;
    uint32_t* nextFree;
} mrhiPool;

// Starts a pool over two arrays of capacity entries each.
void mrhiPoolInit(mrhiPool* pool, uint32_t capacity, uint32_t* generations, uint32_t* nextFree);

// Takes a free slot: true with its index1 and generation, false when
// every slot is used.
bool mrhiPoolAcquire(mrhiPool* pool, uint32_t* index1Out, uint32_t* generationOut);

// Whether an id names a slot in use.
bool mrhiPoolIsLive(const mrhiPool* pool, uint32_t index1, uint32_t generation);

// Frees a live slot and ends its ids.
void mrhiPoolRelease(mrhiPool* pool, uint32_t index1);

#endif // MAUL_RHI_SRC_POOL_H
