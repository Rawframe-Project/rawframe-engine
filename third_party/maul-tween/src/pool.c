// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Slots handed out by generation-checked ids.

#include "pool.h"

#include "invariant.h"

#include <string.h>

mtwnSlotHeader* mtwnPoolFind(const mtwnPool* pool, uint32_t index1, uint32_t generation)
{
    if (index1 == 0 || index1 > pool->high)
    {
        return NULL;
    }
    mtwnSlotHeader* slot = mtwnPoolAt(pool, index1);
    bool alive = (slot->flags & MTWN_SLOT_ALIVE) != 0;
    return alive && slot->generation == generation ? slot : NULL;
}

uint32_t mtwnPoolAcquire(mtwnPool* pool)
{
    uint32_t index1 = pool->free;
    if (index1 != 0)
    {
        // The free list holds released slots only, never a retired one,
        // whose next generation would wrap to 0 and make its ids null.
        MTWN_ASSERT(index1 <= pool->high &&
                    (mtwnPoolAt(pool, index1)->flags & MTWN_SLOT_ALIVE) == 0 &&
                    mtwnPoolAt(pool, index1)->generation != UINT32_MAX);
        pool->free = mtwnPoolAt(pool, index1)->link;
    }
    else if (pool->high < pool->capacity)
    {
        index1 = ++pool->high;
    }
    else
    {
        return 0;
    }
    mtwnSlotHeader* slot = mtwnPoolAt(pool, index1);
    uint32_t generation = slot->generation + 1;
    memset(slot, 0, pool->stride);
    slot->generation = generation;
    slot->flags = MTWN_SLOT_ALIVE;
    return index1;
}

void mtwnPoolRelease(mtwnPool* pool, uint32_t index1)
{
    mtwnSlotHeader* slot = mtwnPoolAt(pool, index1);
    // Only a slot handed out is released, and only once.
    MTWN_ASSERT(index1 != 0 && index1 <= pool->high && (slot->flags & MTWN_SLOT_ALIVE) != 0);
    slot->flags = 0;
    // A slot whose generation is at its end is retired: its ids can never
    // come back.
    if (slot->generation != UINT32_MAX)
    {
        slot->link = pool->free;
        pool->free = index1;
    }
}
