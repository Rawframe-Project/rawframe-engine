// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The pools of a context's objects:
// fixed slots of one stride, each opening with a header, handed out by
// generation-checked ids. Free slots are reused last in, first out; a
// slot at the last generation is retired.

#ifndef MAUL_TWEEN_SRC_POOL_H
#define MAUL_TWEEN_SRC_POOL_H

#include <stdbool.h>
#include <stdint.h>

// The first member of every slot.
typedef struct mtwnSlotHeader
{
    uint32_t generation;
    // A free slot's next free slot; a live one's partner (a target's
    // transition, a transition's target), as index1, 0 for none.
    uint32_t link;
    uint8_t flags;
} mtwnSlotHeader;

// The slot holds a live object.
#define MTWN_SLOT_ALIVE 1u

typedef struct mtwnPool
{
    unsigned char* slots;
    uint32_t stride;
    uint32_t capacity;
    // Slots used so far; past it every slot is fresh.
    uint32_t high;
    // The first free slot below high, as index1, 0 when none.
    uint32_t free;
} mtwnPool;

// The slot at index1, which is from 1 to the capacity.
static inline mtwnSlotHeader* mtwnPoolAt(const mtwnPool* pool, uint32_t index1)
{
    return (mtwnSlotHeader*)(pool->slots + (uint64_t)(index1 - 1) * pool->stride);
}

// The live slot an id names, or NULL.
mtwnSlotHeader* mtwnPoolFind(const mtwnPool* pool, uint32_t index1, uint32_t generation);

// Takes a slot, zeroed but for its advanced generation and marked alive;
// returns its index1, or 0 when the pool is full.
uint32_t mtwnPoolAcquire(mtwnPool* pool);

// Returns a live slot to the pool.
void mtwnPoolRelease(mtwnPool* pool, uint32_t index1);

#endif
