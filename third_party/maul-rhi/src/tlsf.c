// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// TLSF over offsets (tlsf.h). A size's class is its power of two, the
// first level, and one of MRHI_TLSF_SECOND equal steps within it, the
// second; sizes below MRHI_TLSF_SECOND have classes of their own. A
// search rounds the size up to the next class boundary, so that every
// range in the class found fits, and takes the first non-empty class at
// or above it from the two bitmaps.

#include "tlsf.h"

#include "invariant.h"

#include <stdckdint.h>

#define NODE(store, id) (&(store)->nodes[(id) - 1])

static unsigned Log2(uint64_t value)
{
    return 63u - (unsigned)__builtin_clzll(value);
}

static void Classify(uint64_t size, uint32_t* firstOut, uint32_t* secondOut)
{
    if (size < MRHI_TLSF_SECOND)
    {
        *firstOut = 0;
        *secondOut = (uint32_t)size;
        return;
    }
    unsigned log = Log2(size);
    *firstOut = log - MRHI_TLSF_SECOND_LOG2 + 1;
    *secondOut = (uint32_t)(size >> (log - MRHI_TLSF_SECOND_LOG2)) ^ MRHI_TLSF_SECOND;
}

void mrhiTlsfNodesInit(mrhiTlsfNodes* store, mrhiTlsfNode* nodes, uint32_t capacity)
{
    *store = (mrhiTlsfNodes){.nodes = nodes, .capacity = capacity, .unusedCount = capacity};
    for (uint32_t i = 0; i < capacity; ++i)
    {
        nodes[i] = (mrhiTlsfNode){.nextFree = i + 1 < capacity ? i + 2 : 0};
    }
    store->unused = capacity > 0 ? 1 : 0;
}

void mrhiTlsfInit(mrhiTlsf* pool)
{
    *pool = (mrhiTlsf){0};
}

static uint32_t TakeNode(mrhiTlsfNodes* store)
{
    uint32_t id = store->unused;
    if (id != 0)
    {
        store->unused = NODE(store, id)->nextFree;
        --store->unusedCount;
    }
    return id;
}

static void ReturnNode(mrhiTlsfNodes* store, uint32_t id)
{
    *NODE(store, id) = (mrhiTlsfNode){.nextFree = store->unused};
    store->unused = id;
    ++store->unusedCount;
}

static void Insert(mrhiTlsf* pool, mrhiTlsfNodes* store, uint32_t id)
{
    mrhiTlsfNode* node = NODE(store, id);
    uint32_t first = 0;
    uint32_t second = 0;
    Classify(node->size, &first, &second);
    uint32_t head = pool->heads[first][second];
    node->free = true;
    node->previousFree = 0;
    node->nextFree = head;
    if (head != 0)
    {
        NODE(store, head)->previousFree = id;
    }
    pool->heads[first][second] = id;
    pool->secondMap[first] |= 1u << second;
    pool->firstMap |= UINT64_C(1) << first;
}

static void Remove(mrhiTlsf* pool, mrhiTlsfNodes* store, uint32_t id)
{
    mrhiTlsfNode* node = NODE(store, id);
    MRHI_ASSERT(node->free);
    uint32_t first = 0;
    uint32_t second = 0;
    Classify(node->size, &first, &second);
    if (node->previousFree != 0)
    {
        NODE(store, node->previousFree)->nextFree = node->nextFree;
    }
    else
    {
        pool->heads[first][second] = node->nextFree;
    }
    if (node->nextFree != 0)
    {
        NODE(store, node->nextFree)->previousFree = node->previousFree;
    }
    if (pool->heads[first][second] == 0)
    {
        pool->secondMap[first] &= ~(1u << second);
        if (pool->secondMap[first] == 0)
        {
            pool->firstMap &= ~(UINT64_C(1) << first);
        }
    }
    node->free = false;
    node->previousFree = 0;
    node->nextFree = 0;
}

uint32_t mrhiTlsfAddBlock(mrhiTlsf* pool, mrhiTlsfNodes* store, uint32_t block, uint64_t size)
{
    MRHI_ASSERT(size > 0);
    uint32_t id = TakeNode(store);
    if (id != 0)
    {
        *NODE(store, id) = (mrhiTlsfNode){.size = size, .block = block};
        Insert(pool, store, id);
    }
    return id;
}

// The first free range whose class is at or above the class of a size
// rounded up to its class's end: every range there holds the size.
static uint32_t FindFitting(const mrhiTlsf* pool, uint64_t size)
{
    if (size >= MRHI_TLSF_SECOND)
    {
        uint64_t step = (UINT64_C(1) << (Log2(size) - MRHI_TLSF_SECOND_LOG2)) - 1;
        if (ckd_add(&size, size, step))
        {
            return 0;
        }
    }
    uint32_t first = 0;
    uint32_t second = 0;
    Classify(size, &first, &second);
    uint32_t seconds = pool->secondMap[first] & (~0u << second);
    if (seconds == 0)
    {
        uint64_t firsts =
            first + 1 < MRHI_TLSF_FIRST ? pool->firstMap & (~UINT64_C(0) << (first + 1)) : 0;
        if (firsts == 0)
        {
            return 0;
        }
        first = (uint32_t)__builtin_ctzll(firsts);
        seconds = pool->secondMap[first];
    }
    return pool->heads[first][(uint32_t)__builtin_ctz(seconds)];
}

// Links a new node after a node of the same block.
static void LinkAfter(mrhiTlsfNodes* store, uint32_t id, uint32_t added)
{
    mrhiTlsfNode* node = NODE(store, id);
    mrhiTlsfNode* next = NODE(store, added);
    next->block = node->block;
    next->previous = id;
    next->next = node->next;
    if (node->next != 0)
    {
        NODE(store, node->next)->previous = added;
    }
    node->next = added;
}

// Moves a free range's first bytes, which alignment skips, into a free
// node of their own; or, with no node left, onto the range before,
// which a block's start never lacks since offset 0 is always aligned.
static uint32_t SplitFront(mrhiTlsf* pool, mrhiTlsfNodes* store, uint32_t id, uint64_t skip)
{
    mrhiTlsfNode* node = NODE(store, id);
    uint32_t used = TakeNode(store);
    if (used != 0)
    {
        LinkAfter(store, id, used);
        mrhiTlsfNode* rest = NODE(store, used);
        rest->offset = node->offset + skip;
        rest->size = node->size - skip;
        node->size = skip;
        Insert(pool, store, id);
        return used;
    }
    MRHI_ASSERT(node->previous != 0);
    mrhiTlsfNode* before = NODE(store, node->previous);
    bool wasFree = before->free;
    if (wasFree)
    {
        Remove(pool, store, node->previous);
    }
    before->size += skip;
    if (wasFree)
    {
        Insert(pool, store, node->previous);
    }
    node->offset += skip;
    node->size -= skip;
    return id;
}

// The bytes alignment skips at the start of a range.
static uint64_t SkipOf(const mrhiTlsfNode* node, uint64_t alignment)
{
    return ((node->offset + alignment - 1) & ~(alignment - 1)) - node->offset;
}

static bool Fits(const mrhiTlsfNode* node, uint64_t size, uint64_t alignment)
{
    uint64_t skip = SkipOf(node, alignment);
    return skip <= node->size && size <= node->size - skip;
}

// A free range for an allocation: the first class that holds the size,
// if its first range is aligned or has room to be; else the first class
// that holds the size with its worst padding; else, for an exact fit,
// any range of the size's own class that holds it.
static uint32_t FindFree(const mrhiTlsf* pool, const mrhiTlsfNodes* store, uint64_t size,
                         uint64_t alignment)
{
    uint32_t id = FindFitting(pool, size);
    if (id != 0 && Fits(NODE(store, id), size, alignment))
    {
        return id;
    }
    uint64_t padded = 0;
    id = ckd_add(&padded, size, alignment - 1) ? 0 : FindFitting(pool, padded);
    if (id != 0)
    {
        return id;
    }
    uint32_t first = 0;
    uint32_t second = 0;
    Classify(size, &first, &second);
    for (id = pool->heads[first][second]; id != 0; id = NODE(store, id)->nextFree)
    {
        if (Fits(NODE(store, id), size, alignment))
        {
            return id;
        }
    }
    return 0;
}

uint32_t mrhiTlsfAllocate(mrhiTlsf* pool, mrhiTlsfNodes* store, uint64_t size, uint64_t alignment,
                          uint32_t* blockOut, uint64_t* offsetOut)
{
    MRHI_ASSERT(size > 0 && alignment > 0 && (alignment & (alignment - 1)) == 0);
    uint32_t id = FindFree(pool, store, size, alignment);
    if (id == 0)
    {
        return 0;
    }
    Remove(pool, store, id);
    mrhiTlsfNode* node = NODE(store, id);
    uint64_t skip = SkipOf(node, alignment);
    if (skip > 0)
    {
        id = SplitFront(pool, store, id, skip);
        node = NODE(store, id);
    }
    uint64_t rest = node->size - size;
    uint32_t tail = rest > 0 ? TakeNode(store) : 0;
    if (tail != 0)
    {
        LinkAfter(store, id, tail);
        NODE(store, tail)->offset = node->offset + size;
        NODE(store, tail)->size = rest;
        node->size = size;
        Insert(pool, store, tail);
    }
    node->free = false;
    *blockOut = node->block;
    *offsetOut = node->offset;
    return id;
}

// Folds a free node's next neighbour into it.
static void Absorb(mrhiTlsfNodes* store, uint32_t id)
{
    mrhiTlsfNode* node = NODE(store, id);
    uint32_t gone = node->next;
    mrhiTlsfNode* next = NODE(store, gone);
    node->size += next->size;
    node->next = next->next;
    if (next->next != 0)
    {
        NODE(store, next->next)->previous = id;
    }
    ReturnNode(store, gone);
}

uint32_t mrhiTlsfFree(mrhiTlsf* pool, mrhiTlsfNodes* store, uint32_t allocation)
{
    uint32_t id = allocation;
    MRHI_ASSERT(id != 0 && id <= store->capacity && !NODE(store, id)->free);
    uint32_t before = NODE(store, id)->previous;
    if (before != 0 && NODE(store, before)->free)
    {
        Remove(pool, store, before);
        Absorb(store, before);
        id = before;
    }
    uint32_t after = NODE(store, id)->next;
    if (after != 0 && NODE(store, after)->free)
    {
        Remove(pool, store, after);
        Absorb(store, id);
    }
    Insert(pool, store, id);
    return id;
}

bool mrhiTlsfIsBlockEmpty(const mrhiTlsfNodes* store, uint32_t node)
{
    const mrhiTlsfNode* range = NODE(store, node);
    return range->free && range->previous == 0 && range->next == 0;
}

void mrhiTlsfRemoveBlock(mrhiTlsf* pool, mrhiTlsfNodes* store, uint32_t node)
{
    MRHI_ASSERT(mrhiTlsfIsBlockEmpty(store, node));
    Remove(pool, store, node);
    ReturnNode(store, node);
}
