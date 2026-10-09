// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The probe sets' slots (probe_sets.h): a free list taken from its end,
// a slot's generation moving on when its set is released.

#include "probe_sets.h"

#include "allocator.h"

typedef struct Slot
{
    uint32_t generation;
    bool live;
    maudProbeGraph graph;
    maudProbeBake bake;
} Slot;

struct maudProbeSets
{
    maudAllocator allocator;
    uint32_t capacity;
    uint32_t freeCount;
    Slot* slots;
    uint32_t* free;
};

static size_t Bytes(uint32_t capacity)
{
    return sizeof(maudProbeSets) + (size_t)capacity * (sizeof(Slot) + sizeof(uint32_t));
}

maudProbeSets* maudCreateProbeSets(const maudAllocator* allocator, uint32_t capacity)
{
    maudProbeSets* sets = maudAllocate(allocator, Bytes(capacity), alignof(maudProbeSets));
    if (sets == nullptr)
    {
        return nullptr;
    }
    unsigned char* after = (unsigned char*)(sets + 1);
    *sets = (maudProbeSets){.allocator = *allocator, .capacity = capacity, .freeCount = capacity};
    sets->slots = (Slot*)after;
    sets->free = (uint32_t*)(after + (size_t)capacity * sizeof(Slot));
    for (uint32_t i = 0; i < capacity; ++i)
    {
        sets->slots[i] = (Slot){.generation = 1};
        sets->free[i] = capacity - 1 - i;
    }
    return sets;
}

void maudDestroyProbeSets(maudProbeSets* sets)
{
    if (sets == nullptr)
    {
        return;
    }
    for (uint32_t i = 0; i < sets->capacity; ++i)
    {
        maudReleaseProbeGraph(&sets->allocator, &sets->slots[i].graph);
        maudReleaseProbeBake(&sets->allocator, &sets->slots[i].bake);
    }
    maudAllocator allocator = sets->allocator;
    maudRelease(&allocator, sets, Bytes(sets->capacity), alignof(maudProbeSets));
}

maudResult maudAddProbeSet(maudProbeSets* sets, const maudProbeQueries* queries,
                           const maudProbeSetDef* def, maudProbeSetId* setOut)
{
    if (sets->freeCount == 0)
    {
        return maud_errorCapacity;
    }
    uint32_t index = sets->free[sets->freeCount - 1];
    Slot* slot = &sets->slots[index];
    maudResult result = maudBuildProbeGraph(queries, def, &slot->graph);
    if (result != maud_success)
    {
        return result;
    }
    sets->freeCount -= 1;
    slot->live = true;
    *setOut = (maudProbeSetId){index + 1, slot->generation};
    return maud_success;
}

maudResult maudAdoptProbeSet(maudProbeSets* sets, maudProbeGraph* graph, maudProbeBake* bake,
                             maudProbeSetId* setOut)
{
    if (sets->freeCount == 0)
    {
        return maud_errorCapacity;
    }
    uint32_t index = sets->free[--sets->freeCount];
    Slot* slot = &sets->slots[index];
    slot->graph = *graph;
    slot->bake = *bake;
    slot->live = true;
    *graph = (maudProbeGraph){0};
    *bake = (maudProbeBake){0};
    *setOut = (maudProbeSetId){index + 1, slot->generation};
    return maud_success;
}

static maudResult Find(const maudProbeSets* sets, maudProbeSetId set, Slot** slotOut)
{
    if (sets == nullptr || set.index1 == 0 || set.index1 > sets->capacity || set.generation == 0)
    {
        return maud_errorInvalid;
    }
    Slot* slot = &sets->slots[set.index1 - 1];
    if (!slot->live || slot->generation != set.generation)
    {
        return maud_errorStale;
    }
    *slotOut = slot;
    return maud_success;
}

maudResult maudRemoveProbeSet(maudProbeSets* sets, maudProbeSetId set)
{
    Slot* slot = nullptr;
    maudResult result = Find(sets, set, &slot);
    if (result != maud_success)
    {
        return result;
    }
    maudReleaseProbeGraph(&sets->allocator, &slot->graph);
    maudReleaseProbeBake(&sets->allocator, &slot->bake);
    slot->live = false;
    slot->generation = slot->generation == UINT32_MAX ? 1 : slot->generation + 1;
    sets->free[sets->freeCount++] = set.index1 - 1;
    return maud_success;
}

maudResult maudFindProbeBake(maudProbeSets* sets, maudProbeSetId set,
                             const maudProbeGraph** graphOut, maudProbeBake** bakeOut)
{
    Slot* slot = nullptr;
    maudResult result = Find(sets, set, &slot);
    if (result == maud_success)
    {
        *graphOut = &slot->graph;
        *bakeOut = &slot->bake;
    }
    return result;
}

maudResult maudFindProbeSet(const maudProbeSets* sets, maudProbeSetId set,
                            const maudProbeGraph** graphOut)
{
    Slot* slot = nullptr;
    maudResult result = Find(sets, set, &slot);
    if (result == maud_success)
    {
        *graphOut = &slot->graph;
    }
    return result;
}
