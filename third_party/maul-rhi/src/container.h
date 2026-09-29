// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Shader containers (mrhi-0009, docs/contract/container.md): the parse
// that checks every byte as hostile input, and the decoded reflection.

#ifndef MAUL_RHI_SRC_CONTAINER_H
#define MAUL_RHI_SRC_CONTAINER_H

#include "maul-rhi/shader.h"

// An entry point: its stage, its name in the strings, its workgroup
// size and storage, the builtins and heaps it uses, and its ranges of
// vertex inputs, color outputs and inter-stage variables.
typedef struct mrhiShaderEntry
{
    mrhiShaderStages stage;
    uint32_t nameOffset;
    uint32_t nameLength;
    uint32_t workgroup[3];
    uint16_t firstInput;
    uint16_t inputCount;
    uint16_t firstOutput;
    uint16_t outputCount;
    uint16_t firstVariable;
    uint16_t variableCount;
    mrhiShaderBuiltins builtins;
    uint32_t workgroupStorageBytes;
    mrhiShaderHeapUses heapUses;
} mrhiShaderEntry;

typedef struct mrhiShaderBinding
{
    uint8_t table;
    mrhiBindingKind kind;
    uint16_t slot;
    mrhiShaderStages stages;
    mrhiSamplerBinding sampler;
    mrhiSampleType sampleType;
    mrhiTextureKind viewDimension;
    mrhiStorageAccess access;
    mrhiFormat format;
    bool multisampled;
    uint64_t minSize;
} mrhiShaderBinding;

// A vertex input, a color output or an inter-stage variable.
typedef struct mrhiShaderVariable
{
    uint32_t location;
    mrhiScalarType type;
    uint8_t components;
    mrhiInterpolation interpolation;
    mrhiSampling sampling;
} mrhiShaderVariable;

typedef struct mrhiShaderConstant
{
    uint32_t id;
    mrhiConstantType type;
    uint32_t bits;
    // Whether it has no default, so every pipeline sets it.
    bool required;
} mrhiShaderConstant;

// A checked container: its digest and root block, its sections in the
// caller's bytes, whether it uses 16-bit floats, and the builtins and
// heap uses of its entries together; the WGSL is absent (NULL, 0 bytes)
// exactly when an entry uses a heap.
typedef struct mrhiContainer
{
    uint8_t digest[MRHI_DIGEST_BYTES];
    uint32_t rootBlockBytes;
    bool float16;
    mrhiShaderBuiltins builtins;
    mrhiShaderHeapUses heapUses;
    const uint8_t* strings;
    uint32_t stringBytes;
    const uint8_t* entries;
    uint32_t entryCount;
    const uint8_t* bindings;
    uint32_t bindingCount;
    const uint8_t* inputs;
    uint32_t inputCount;
    const uint8_t* outputs;
    uint32_t outputCount;
    const uint8_t* variables;
    uint32_t variableCount;
    const uint8_t* constants;
    uint32_t constantCount;
    const uint8_t* spirv;
    size_t spirvBytes;
    const uint8_t* wgsl;
    size_t wgslBytes;
} mrhiContainer;

// Checks a container: success with its sections, mrhi_errorVersion for
// another version, or mrhi_errorInvalid.
mrhiResult mrhiParseContainer(const void* bytes, size_t size, mrhiContainer* containerOut);

// The records of a checked container, by index.
mrhiShaderEntry mrhiContainerEntry(const mrhiContainer* container, uint32_t index);
mrhiShaderBinding mrhiContainerBinding(const mrhiContainer* container, uint32_t index);
mrhiShaderVariable mrhiContainerInput(const mrhiContainer* container, uint32_t index);
mrhiShaderVariable mrhiContainerOutput(const mrhiContainer* container, uint32_t index);
mrhiShaderVariable mrhiContainerVariable(const mrhiContainer* container, uint32_t index);
mrhiShaderConstant mrhiContainerConstant(const mrhiContainer* container, uint32_t index);

#endif // MAUL_RHI_SRC_CONTAINER_H
