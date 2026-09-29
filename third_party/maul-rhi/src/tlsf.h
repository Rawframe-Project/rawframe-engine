// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A two-level segregated fit allocator over offsets (TLSF: Masmano,
// Ripoll, Crespo and Real, ECRTS 2004; docs/references.md), for GPU
// memory a driver suballocates: a pool's free ranges, across any number
// of the pool's blocks, in lists by size class found with two bit
// scans, so allocating and freeing take constant time and freed
// neighbours merge. It touches no memory but its bookkeeping, whose
// ranges come from a node store the caller sizes once.

#ifndef MAUL_RHI_SRC_TLSF_H
#define MAUL_RHI_SRC_TLSF_H

#include <stdbool.h>
#include <stdint.h>

// Each power of two splits into 2^MRHI_TLSF_SECOND_LOG2 classes.
#define MRHI_TLSF_SECOND_LOG2 4
#define MRHI_TLSF_SECOND      (1u << MRHI_TLSF_SECOND_LOG2)
// First-level classes, enough for any 64-bit size.
#define MRHI_TLSF_FIRST (64 - MRHI_TLSF_SECOND_LOG2 + 1)

// A range of a block, free or used; links are node indices plus one.
typedef struct mrhiTlsfNode
{
    uint64_t offset;
    uint64_t size;
    uint32_t block;
    uint32_t previous;
    uint32_t next;
    uint32_t previousFree;
    uint32_t nextFree;
    bool free;
} mrhiTlsfNode;

// The ranges every pool of a device takes its nodes from.
typedef struct mrhiTlsfNodes
{
    mrhiTlsfNode* nodes;
    uint32_t capacity;
    // Unused nodes, linked through nextFree.
    uint32_t unused;
    uint32_t unusedCount;
} mrhiTlsfNodes;

// One pool's free lists.
typedef struct mrhiTlsf
{
    uint64_t firstMap;
    uint32_t secondMap[MRHI_TLSF_FIRST];
    uint32_t heads[MRHI_TLSF_FIRST][MRHI_TLSF_SECOND];
} mrhiTlsf;

// Makes a store of capacity nodes over the caller's array.
void mrhiTlsfNodesInit(mrhiTlsfNodes* store, mrhiTlsfNode* nodes, uint32_t capacity);

// Makes an empty pool.
void mrhiTlsfInit(mrhiTlsf* pool);

// Adds a block of size bytes, all free, under the caller's block id:
// its free node, or 0 when the store has no node left.
uint32_t mrhiTlsfAddBlock(mrhiTlsf* pool, mrhiTlsfNodes* store, uint32_t block, uint64_t size);

// Takes size bytes at a multiple of alignment, a power of two: the
// allocation's node, with its block and offset, or 0 when no free range
// fits. A store out of nodes costs bytes, never the allocation: the
// rest of the range stays with it.
uint32_t mrhiTlsfAllocate(mrhiTlsf* pool, mrhiTlsfNodes* store, uint64_t size, uint64_t alignment,
                          uint32_t* blockOut, uint64_t* offsetOut);

// Frees an allocation, merging it with free neighbours: the free node
// that holds it now, which spans its whole block when the block is
// empty.
uint32_t mrhiTlsfFree(mrhiTlsf* pool, mrhiTlsfNodes* store, uint32_t allocation);

// Whether a free node spans its whole block.
bool mrhiTlsfIsBlockEmpty(const mrhiTlsfNodes* store, uint32_t node);

// Removes an empty block by its free node, returning the node.
void mrhiTlsfRemoveBlock(mrhiTlsf* pool, mrhiTlsfNodes* store, uint32_t node);

#endif // MAUL_RHI_SRC_TLSF_H
