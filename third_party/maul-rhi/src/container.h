// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Shader containers (mrhi-0009, docs/contract/container.md): the parse
// that checks every byte as hostile input, and the decoded reflection.

#ifndef MAUL_RHI_SRC_CONTAINER_H
#define MAUL_RHI_SRC_CONTAINER_H

#include "container_interface.h"

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

typedef struct mrhiShaderConstant
{
    uint32_t id;
    mrhiConstantType type;
    uint32_t bits;
    // Whether it has no default, so every pipeline sets it.
    bool required;
} mrhiShaderConstant;

// An entry's Metal code: its MSL's range in the MSL section (0 and 0
// without one) and its buffer sizes' index, or MRHI_METAL_NONE.
typedef struct mrhiMetalEntry
{
    uint32_t mslOffset;
    uint32_t mslLength;
    uint8_t sizesIndex;
} mrhiMetalEntry;

// A Metal index the map leaves unused.
#define MRHI_METAL_NONE 255

// A D3D12 register and its space.
typedef struct mrhiD3d12Place
{
    uint32_t reg;
    uint32_t space;
} mrhiD3d12Place;

// The constant buffers a D3D12 map places besides the bindings.
typedef enum mrhiD3d12MapBuffer
{
    mrhiD3d12RootBlock,
    mrhiD3d12Constants,
    mrhiD3d12VertexInfo,
} mrhiD3d12MapBuffer;

// The classes of a D3D12 heap range: shader resource views, unordered
// access views, samplers.
typedef enum mrhiD3d12HeapClass
{
    mrhiD3d12HeapResource,
    mrhiD3d12HeapStorage,
    mrhiD3d12HeapSampler,
} mrhiD3d12HeapClass;

// A range of heap descriptors a container's DXIL reads: its class, and
// the register and space of its unbounded array.
typedef struct mrhiD3d12HeapRange
{
    mrhiD3d12HeapClass rangeClass;
    uint32_t reg;
    uint32_t space;
} mrhiD3d12HeapRange;

// The first space of heap ranges; bindings and constant buffers lie
// below it.
#define MRHI_D3D12_HEAP_SPACE 16

// An entry's D3D12 code: its DXIL's range in the DXIL section, and
// whether it reads the vertex information.
typedef struct mrhiD3d12Entry
{
    uint32_t dxilOffset;
    uint32_t dxilLength;
    bool vertexInfo;
} mrhiD3d12Entry;

// A checked container: its digest and root block, its sections in the
// caller's bytes, whether it uses 16-bit floats (in its interface or its
// SPIR-V's capabilities), subgroup operations and 64-bit integers, and the
// builtins and
// heap uses of its entries together; the WGSL is absent (NULL, 0 bytes)
// exactly when an entry uses a heap. The Metal map is NULL without Metal
// code, and the MSL and metallib each NULL and 0 bytes when absent; the
// D3D12 map and DXIL are both NULL and 0 bytes, or both present.
typedef struct mrhiContainer
{
    uint8_t digest[MRHI_DIGEST_BYTES];
    uint32_t rootBlockBytes;
    bool float16;
    bool subgroups;
    bool int64;
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
    const uint8_t* metalMap;
    size_t metalMapBytes;
    const uint8_t* msl;
    size_t mslBytes;
    const uint8_t* metallib;
    size_t metallibBytes;
    const uint8_t* d3d12Map;
    size_t d3d12MapBytes;
    const uint8_t* dxil;
    size_t dxilBytes;
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

// The Metal map of a checked container with Metal code: the root block's
// buffer index (MRHI_METAL_NONE when it is empty), an entry's code, and a
// binding's index in its class.
uint8_t mrhiContainerMetalRoot(const mrhiContainer* container);
mrhiMetalEntry mrhiContainerMetalEntry(const mrhiContainer* container, uint32_t index);
uint8_t mrhiContainerMetalIndex(const mrhiContainer* container, uint32_t binding);

// The D3D12 map of a checked container with DXIL: where a constant
// buffer lies ({0, 0} when the container has none of it), an entry's
// code, where a binding lies, and whether a constant is fixed.
mrhiD3d12Place mrhiContainerD3d12Buffer(const mrhiContainer* container, mrhiD3d12MapBuffer buffer);
mrhiD3d12Entry mrhiContainerD3d12Entry(const mrhiContainer* container, uint32_t index);
mrhiD3d12Place mrhiContainerD3d12Binding(const mrhiContainer* container, uint32_t binding);
uint32_t mrhiContainerD3d12HeapRangeCount(const mrhiContainer* container);
mrhiD3d12HeapRange mrhiContainerD3d12HeapRange(const mrhiContainer* container, uint32_t index);
bool mrhiContainerD3d12Fixed(const mrhiContainer* container, uint32_t constant);

#endif // MAUL_RHI_SRC_CONTAINER_H
