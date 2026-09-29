// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A shader container's decoded reflection (mrhi-0009): one block from
// the device's allocator, held by the shader and by every pipeline made
// from it, freed when the last lets go.

#ifndef MAUL_RHI_SRC_REFLECTION_H
#define MAUL_RHI_SRC_REFLECTION_H

#include "container.h"

// The reflection and its arrays, which follow it in the same block. The
// entries' names are packed after the records, and each entry's
// nameOffset points into them.
typedef struct mrhiReflection
{
    uint32_t references;
    size_t bytes;
    uint8_t digest[MRHI_DIGEST_BYTES];
    uint32_t rootBlockBytes;
    // What its entries read through heaps, together.
    mrhiShaderHeapUses heapUses;
    uint32_t entryCount;
    uint32_t bindingCount;
    uint32_t inputCount;
    uint32_t outputCount;
    uint32_t variableCount;
    uint32_t constantCount;
    const mrhiShaderEntry* entries;
    const mrhiShaderBinding* bindings;
    const mrhiShaderVariable* inputs;
    const mrhiShaderVariable* outputs;
    const mrhiShaderVariable* variables;
    const mrhiShaderConstant* constants;
    const char* names;
} mrhiReflection;

// Decodes a checked container's reflection into a new block with one
// reference: the block, or NULL when the allocator fails.
mrhiReflection* mrhiKeepReflection(const mrhiAllocator* allocator, const mrhiContainer* container);

// Drops a reference, freeing the block with the last.
void mrhiReleaseReflection(const mrhiAllocator* allocator, mrhiReflection* reflection);

// The index of the entry named by length bytes with a stage, or
// entryCount when there is none.
uint32_t mrhiFindEntry(const mrhiReflection* reflection, const char* name, size_t length,
                       mrhiShaderStages stage);

#endif // MAUL_RHI_SRC_REFLECTION_H
