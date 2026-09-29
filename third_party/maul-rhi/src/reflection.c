// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Reflections: a checked container decoded once into one block, the
// struct first and its arrays after it, shared by reference count.

#include "reflection.h"

#include "allocator.h"

#include <stdalign.h>
#include <string.h>

// Where each part of a reflection block starts.
typedef struct Parts
{
    size_t entries;
    size_t bindings;
    size_t inputs;
    size_t outputs;
    size_t variables;
    size_t constants;
    size_t names;
} Parts;

// The bytes of a container's entries' names.
static size_t NameBytes(const mrhiContainer* container)
{
    size_t bytes = 0;
    for (uint32_t i = 0; i < container->entryCount; ++i)
    {
        bytes += mrhiContainerEntry(container, i).nameLength;
    }
    return bytes;
}

// Copies the container's records into the block's arrays.
static void Decode(const mrhiContainer* container, unsigned char* block, Parts parts,
                   mrhiReflection* reflection)
{
    mrhiShaderEntry* entries = (mrhiShaderEntry*)(block + parts.entries);
    mrhiShaderBinding* bindings = (mrhiShaderBinding*)(block + parts.bindings);
    mrhiShaderVariable* inputs = (mrhiShaderVariable*)(block + parts.inputs);
    mrhiShaderVariable* outputs = (mrhiShaderVariable*)(block + parts.outputs);
    mrhiShaderVariable* variables = (mrhiShaderVariable*)(block + parts.variables);
    mrhiShaderConstant* constants = (mrhiShaderConstant*)(block + parts.constants);
    char* names = (char*)(block + parts.names);
    uint32_t packed = 0;
    for (uint32_t i = 0; i < container->entryCount; ++i)
    {
        mrhiShaderEntry entry = mrhiContainerEntry(container, i);
        memcpy(names + packed, container->strings + entry.nameOffset, entry.nameLength);
        entry.nameOffset = packed;
        packed += entry.nameLength;
        entries[i] = entry;
    }
    for (uint32_t i = 0; i < container->bindingCount; ++i)
    {
        bindings[i] = mrhiContainerBinding(container, i);
    }
    for (uint32_t i = 0; i < container->inputCount; ++i)
    {
        inputs[i] = mrhiContainerInput(container, i);
    }
    for (uint32_t i = 0; i < container->outputCount; ++i)
    {
        outputs[i] = mrhiContainerOutput(container, i);
    }
    for (uint32_t i = 0; i < container->variableCount; ++i)
    {
        variables[i] = mrhiContainerVariable(container, i);
    }
    for (uint32_t i = 0; i < container->constantCount; ++i)
    {
        constants[i] = mrhiContainerConstant(container, i);
    }
    reflection->entries = entries;
    reflection->bindings = bindings;
    reflection->inputs = inputs;
    reflection->outputs = outputs;
    reflection->variables = variables;
    reflection->constants = constants;
    reflection->names = names;
}

mrhiReflection* mrhiKeepReflection(const mrhiAllocator* allocator, const mrhiContainer* container)
{
    mrhiLayout layout = {.size = sizeof(mrhiReflection)};
    Parts parts = {
        .entries = mrhiLayoutAdd(&layout, container->entryCount, sizeof(mrhiShaderEntry),
                                 alignof(mrhiShaderEntry)),
        .bindings = mrhiLayoutAdd(&layout, container->bindingCount, sizeof(mrhiShaderBinding),
                                  alignof(mrhiShaderBinding)),
        .inputs = mrhiLayoutAdd(&layout, container->inputCount, sizeof(mrhiShaderVariable),
                                alignof(mrhiShaderVariable)),
        .outputs = mrhiLayoutAdd(&layout, container->outputCount, sizeof(mrhiShaderVariable),
                                 alignof(mrhiShaderVariable)),
        .variables = mrhiLayoutAdd(&layout, container->variableCount, sizeof(mrhiShaderVariable),
                                   alignof(mrhiShaderVariable)),
        .constants = mrhiLayoutAdd(&layout, container->constantCount, sizeof(mrhiShaderConstant),
                                   alignof(mrhiShaderConstant)),
        .names = mrhiLayoutAdd(&layout, NameBytes(container), 1, 1),
    };
    unsigned char* block =
        layout.overflow ? nullptr : mrhiAllocate(allocator, layout.size, alignof(mrhiReflection));
    if (block == nullptr)
    {
        return nullptr;
    }
    mrhiReflection* reflection = (mrhiReflection*)block;
    *reflection = (mrhiReflection){
        .references = 1,
        .bytes = layout.size,
        .rootBlockBytes = container->rootBlockBytes,
        .heapUses = container->heapUses,
        .entryCount = container->entryCount,
        .bindingCount = container->bindingCount,
        .inputCount = container->inputCount,
        .outputCount = container->outputCount,
        .variableCount = container->variableCount,
        .constantCount = container->constantCount,
    };
    memcpy(reflection->digest, container->digest, MRHI_DIGEST_BYTES);
    Decode(container, block, parts, reflection);
    return reflection;
}

void mrhiReleaseReflection(const mrhiAllocator* allocator, mrhiReflection* reflection)
{
    if (--reflection->references == 0)
    {
        mrhiRelease(allocator, reflection, reflection->bytes, alignof(mrhiReflection));
    }
}

uint32_t mrhiFindEntry(const mrhiReflection* reflection, const char* name, size_t length,
                       mrhiShaderStages stage)
{
    for (uint32_t i = 0; i < reflection->entryCount; ++i)
    {
        const mrhiShaderEntry* entry = &reflection->entries[i];
        if (entry->stage == stage && entry->nameLength == length &&
            memcmp(reflection->names + entry->nameOffset, name, length) == 0)
        {
            return i;
        }
    }
    return reflection->entryCount;
}
