// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A fixed array of numbered slots handed out and taken back, with the
// generation that tells an id's object from later occupants of its slot
// (family record 0016). Slots are 1-based; 0 means none. Reuse is last
// in, first out, as the node store's.

#ifndef MAUL_UI_SRC_POOL_H
#define MAUL_UI_SRC_POOL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct muiPoolSlot
{
    uint32_t generation;
    // For a free slot, the next free slot.
    uint32_t nextFree;
    bool live;
} muiPoolSlot;

typedef struct muiPool
{
    // Slot i is slots[i - 1].
    muiPoolSlot* slots;
    uint32_t capacity;
    // Slots ever handed out; those above are untouched.
    uint32_t used;
    uint32_t freeHead;
} muiPool;

// Sets up a pool over slots, an array of capacity zeroed entries.
void muiPoolInit(muiPool* pool, muiPoolSlot* slots, uint32_t capacity);

// Hands out a slot, or 0 when every slot is in use.
uint32_t muiPoolTake(muiPool* pool);

// Takes back a live slot; ids of its occupant go stale.
void muiPoolGive(muiPool* pool, uint32_t slot);

// The slot an id's index and generation name while it lives, or 0.
uint32_t muiPoolResolve(const muiPool* pool, uint32_t index1, uint32_t generation);

// The generation of a live slot.
uint32_t muiPoolGeneration(const muiPool* pool, uint32_t slot);

#endif // MAUL_UI_SRC_POOL_H
