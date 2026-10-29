// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The container's parse: the header and section table
// (container_table.c), then every record against the rules of
// docs/user/shader-container-format.md. Every read is bounded by the sizes checked
// before it, and bytes are read one at a time, so nothing depends on the
// caller's alignment.

#include "container.h"

#include "bytes.h"
#include "capabilities_core.h"
#include "container_interface.h"
#include "container_table.h"
#include "invariant.h"
#include "label.h"
#include "spirv_caps.h"

#include <stdckdint.h>
#include <string.h>

#define ENTRY_BYTES       48
#define BINDING_BYTES     24
#define CONSTANT_BYTES    16
#define METAL_ENTRY_BYTES 16
#define D3D12_HEAD_BYTES  32
#define D3D12_ENTRY_BYTES 16
#define D3D12_RANGE_BYTES 16
#define SPIRV_MAGIC       0x07230203u

enum
{
    SECTION_META = 1,
    SECTION_STRINGS,
    SECTION_ENTRIES,
    SECTION_BINDINGS,
    SECTION_INPUTS,
    SECTION_OUTPUTS,
    SECTION_CONSTANTS,
    SECTION_SPIRV,
    SECTION_WGSL,
    SECTION_VARIABLES,
    SECTION_METAL_MAP,
    SECTION_MSL,
    SECTION_METALLIB,
    SECTION_D3D12_MAP,
    SECTION_DXIL,
};
static_assert(SECTION_DXIL == MRHI_CONTAINER_TYPES, "every section type known to the table");

// Metal's argument indices per class: buffers, textures and samplers.
#define METAL_BUFFERS  31
#define METAL_TEXTURES 128
#define METAL_SAMPLERS 16

// D3D12's register spaces end below this; a DXIL container's header
// holds its size at byte 24.
#define D3D12_SPACES      0xFFFFFFF0u
#define DXBC_HEADER_BYTES 32

// Finds a section by type: its bytes and size, or NULL when absent.
static const uint8_t* FindSection(const uint8_t* bytes, const mrhiContainerSection* sections,
                                  uint32_t count, uint32_t type, uint64_t* sizeOut)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        if (sections[i].type == type)
        {
            *sizeOut = sections[i].size;
            return bytes + sections[i].offset;
        }
    }
    *sizeOut = 0;
    return nullptr;
}

// Takes an array section: false when its size is not a whole number of
// records, or holds more than MRHI_MAX_CONTAINER_RECORDS.
static bool TakeArray(const uint8_t* bytes, const mrhiContainerSection* sections, uint32_t count,
                      uint32_t type, uint32_t recordBytes, const uint8_t** arrayOut,
                      uint32_t* countOut)
{
    uint64_t size = 0;
    *arrayOut = FindSection(bytes, sections, count, type, &size);
    *countOut = (uint32_t)(size / recordBytes);
    return size % recordBytes == 0 && size / recordBytes <= MRHI_MAX_CONTAINER_RECORDS;
}

// Whether a record lies inside the section given. The accessors read
// only after the parse checked the sizes they rely on; only assertions
// call it.
[[maybe_unused]] static bool Inside(const uint8_t* section, size_t sectionBytes, const uint8_t* at,
                                    size_t bytes)
{
    return section != nullptr && at >= section && bytes <= sectionBytes &&
           (size_t)(at - section) <= sectionBytes - bytes;
}

mrhiShaderEntry mrhiContainerEntry(const mrhiContainer* container, uint32_t index)
{
    MRHI_ASSERT(index < container->entryCount);
    const uint8_t* at = container->entries + (size_t)index * ENTRY_BYTES;
    return (mrhiShaderEntry){
        .stage = mrhiRead32(at),
        .nameOffset = mrhiRead32(at + 4),
        .nameLength = mrhiRead32(at + 8),
        .workgroup = {mrhiRead32(at + 12), mrhiRead32(at + 16), mrhiRead32(at + 20)},
        .firstInput = mrhiRead16(at + 24),
        .inputCount = mrhiRead16(at + 26),
        .firstOutput = mrhiRead16(at + 28),
        .outputCount = mrhiRead16(at + 30),
        .firstVariable = mrhiRead16(at + 32),
        .variableCount = mrhiRead16(at + 34),
        .builtins = mrhiRead32(at + 36),
        .workgroupStorageBytes = mrhiRead32(at + 40),
        .heapUses = mrhiRead32(at + 44),
    };
}

mrhiShaderBinding mrhiContainerBinding(const mrhiContainer* container, uint32_t index)
{
    MRHI_ASSERT(index < container->bindingCount);
    const uint8_t* at = container->bindings + (size_t)index * BINDING_BYTES;
    return (mrhiShaderBinding){
        .table = at[0],
        .kind = at[1],
        .slot = mrhiRead16(at + 2),
        .stages = mrhiRead32(at + 4),
        .sampler = at[8],
        .sampleType = at[9],
        .viewDimension = at[10],
        .access = at[11],
        .format = mrhiRead16(at + 12),
        .multisampled = at[14] != 0,
        .minSize = mrhiRead64(at + 16),
    };
}

mrhiShaderVariable mrhiContainerInput(const mrhiContainer* container, uint32_t index)
{
    MRHI_ASSERT(index < container->inputCount);
    return mrhiReadInterfaceRecord(container->inputs, index);
}

// A color output's byte 6 is its blend source, not an interpolation.
mrhiShaderVariable mrhiContainerOutput(const mrhiContainer* container, uint32_t index)
{
    MRHI_ASSERT(index < container->outputCount);
    mrhiShaderVariable output = mrhiReadInterfaceRecord(container->outputs, index);
    output.blendSource = (uint8_t)output.interpolation;
    output.interpolation = 0;
    return output;
}

mrhiShaderVariable mrhiContainerVariable(const mrhiContainer* container, uint32_t index)
{
    MRHI_ASSERT(index < container->variableCount);
    return mrhiReadInterfaceRecord(container->variables, index);
}

mrhiShaderConstant mrhiContainerConstant(const mrhiContainer* container, uint32_t index)
{
    MRHI_ASSERT(index < container->constantCount);
    const uint8_t* at = container->constants + (size_t)index * CONSTANT_BYTES;
    return (mrhiShaderConstant){
        .id = mrhiRead32(at),
        .type = at[4],
        .bits = mrhiRead32(at + 8),
        .required = at[12] != 0,
    };
}

uint8_t mrhiContainerMetalRoot(const mrhiContainer* container)
{
    MRHI_ASSERT(Inside(container->metalMap, container->metalMapBytes, container->metalMap, 8));
    return container->metalMap[0];
}

mrhiMetalEntry mrhiContainerMetalEntry(const mrhiContainer* container, uint32_t index)
{
    const uint8_t* at = container->metalMap + 8 + (size_t)index * METAL_ENTRY_BYTES;
    MRHI_ASSERT(index < container->entryCount &&
                Inside(container->metalMap, container->metalMapBytes, at, METAL_ENTRY_BYTES));
    return (mrhiMetalEntry){
        .mslOffset = mrhiRead32(at),
        .mslLength = mrhiRead32(at + 4),
        .sizesIndex = at[8],
    };
}

uint8_t mrhiContainerMetalIndex(const mrhiContainer* container, uint32_t binding)
{
    const uint8_t* at =
        container->metalMap + 8 + (size_t)container->entryCount * METAL_ENTRY_BYTES + binding;
    MRHI_ASSERT(binding < container->bindingCount &&
                Inside(container->metalMap, container->metalMapBytes, at, 1));
    return *at;
}

static mrhiD3d12Place ReadPlace(const uint8_t* at)
{
    return (mrhiD3d12Place){.reg = mrhiRead32(at), .space = mrhiRead32(at + 4)};
}

// Whether a record lies inside the D3D12 map. Only assertions call it.
[[maybe_unused]] static bool InD3d12Map(const mrhiContainer* container, const uint8_t* at,
                                        size_t bytes)
{
    return Inside(container->d3d12Map, container->d3d12MapBytes, at, bytes);
}

mrhiD3d12Place mrhiContainerD3d12Buffer(const mrhiContainer* container, mrhiD3d12MapBuffer buffer)
{
    const uint8_t* at = container->d3d12Map + (size_t)buffer * 8;
    MRHI_ASSERT(buffer <= mrhiD3d12VertexInfo && InD3d12Map(container, at, 8));
    return ReadPlace(at);
}

mrhiD3d12Entry mrhiContainerD3d12Entry(const mrhiContainer* container, uint32_t index)
{
    const uint8_t* at = container->d3d12Map + D3D12_HEAD_BYTES + (size_t)index * D3D12_ENTRY_BYTES;
    MRHI_ASSERT(index < container->entryCount && InD3d12Map(container, at, D3D12_ENTRY_BYTES));
    return (mrhiD3d12Entry){
        .dxilOffset = mrhiRead32(at),
        .dxilLength = mrhiRead32(at + 4),
        .vertexInfo = mrhiRead32(at + 8) != 0,
    };
}

// Where the D3D12 map's bindings start; its constants follow them.
static const uint8_t* D3d12Bindings(const mrhiContainer* container)
{
    return container->d3d12Map + D3D12_HEAD_BYTES +
           (size_t)container->entryCount * D3D12_ENTRY_BYTES;
}

mrhiD3d12Place mrhiContainerD3d12Binding(const mrhiContainer* container, uint32_t binding)
{
    const uint8_t* at = D3d12Bindings(container) + (size_t)binding * 8;
    MRHI_ASSERT(binding < container->bindingCount && InD3d12Map(container, at, 8));
    return ReadPlace(at);
}

uint32_t mrhiContainerD3d12HeapRangeCount(const mrhiContainer* container)
{
    MRHI_ASSERT(InD3d12Map(container, container->d3d12Map + 24, 4));
    return mrhiRead32(container->d3d12Map + 24);
}

static const uint8_t* D3d12HeapRanges(const mrhiContainer* container)
{
    return D3d12Bindings(container) + (size_t)container->bindingCount * 8;
}

mrhiD3d12HeapRange mrhiContainerD3d12HeapRange(const mrhiContainer* container, uint32_t index)
{
    const uint8_t* at = D3d12HeapRanges(container) + (size_t)index * D3D12_RANGE_BYTES;
    MRHI_ASSERT(index < mrhiContainerD3d12HeapRangeCount(container) &&
                InD3d12Map(container, at, D3D12_RANGE_BYTES));
    return (mrhiD3d12HeapRange){
        .rangeClass = (mrhiD3d12HeapClass)mrhiRead32(at),
        .reg = mrhiRead32(at + 4),
        .space = mrhiRead32(at + 8),
    };
}

bool mrhiContainerD3d12Fixed(const mrhiContainer* container, uint32_t constant)
{
    const uint8_t* at = D3d12HeapRanges(container) +
                        (size_t)mrhiContainerD3d12HeapRangeCount(container) * D3D12_RANGE_BYTES +
                        constant;
    MRHI_ASSERT(constant < container->constantCount && InD3d12Map(container, at, 1));
    return *at != 0;
}

// Whether a range of records lies in an array of count.
static bool InArray(uint32_t first, uint32_t length, uint32_t count)
{
    return first + length <= count;
}

// Whether an entry is well formed, its name unique among those before.
static bool IsEntryValid(const mrhiContainer* container, uint32_t index)
{
    mrhiShaderEntry entry = mrhiContainerEntry(container, index);
    bool vertex = entry.stage == mrhi_stageVertex;
    bool fragment = entry.stage == mrhi_stageFragment;
    bool compute = entry.stage == mrhi_stageCompute;
    bool workgroup =
        compute ? entry.workgroup[0] > 0 && entry.workgroup[1] > 0 && entry.workgroup[2] > 0
                : entry.workgroup[0] == 0 && entry.workgroup[1] == 0 && entry.workgroup[2] == 0 &&
                      entry.workgroupStorageBytes == 0;
    bool name =
        entry.nameLength > 0 && entry.nameLength <= MRHI_MAX_CONTAINER_NAME_BYTES &&
        (uint64_t)entry.nameOffset + entry.nameLength <= container->stringBytes &&
        mrhiIsTextValid((const char*)container->strings + entry.nameOffset, entry.nameLength);
    bool inputs = InArray(entry.firstInput, entry.inputCount, container->inputCount) &&
                  (entry.inputCount == 0 || vertex);
    bool outputs = InArray(entry.firstOutput, entry.outputCount, container->outputCount) &&
                   (entry.outputCount == 0 || fragment);
    bool variables = InArray(entry.firstVariable, entry.variableCount, container->variableCount) &&
                     (entry.variableCount == 0 || !compute);
    // The view index is a vertex or fragment builtin; the others are a
    // fragment entry's.
    uint32_t others = entry.builtins & ~(uint32_t)mrhi_builtinViewIndex;
    bool builtins = (entry.builtins & ~mrhiShaderBuiltinsKnown) == 0 && (others == 0 || fragment) &&
                    ((entry.builtins & mrhi_builtinViewIndex) == 0 || !compute);
    // Writes go with the storage kinds, never in a vertex entry, as
    // WebGPU's rule for bound storage.
    uint32_t storage = mrhi_heapUseStorageTextures | mrhi_heapUseStorageBuffers;
    bool writes =
        (entry.heapUses & mrhi_heapUseWrites) == 0 || ((entry.heapUses & storage) != 0 && !vertex);
    bool heap = (entry.heapUses & ~mrhiShaderHeapUsesKnown) == 0 && writes;
    if (!(vertex || fragment || compute) || !workgroup || !name || !inputs || !outputs ||
        !variables || !builtins || !heap)
    {
        return false;
    }
    for (uint32_t i = 0; i < index; ++i)
    {
        mrhiShaderEntry other = mrhiContainerEntry(container, i);
        if (other.nameLength == entry.nameLength &&
            memcmp(container->strings + other.nameOffset, container->strings + entry.nameOffset,
                   entry.nameLength) == 0)
        {
            return false;
        }
    }
    return true;
}

// Whether the locations of a range of interface records are unique.
// Whether an entry's inputs, outputs and variables each have unique
// locations.
static bool AreEntryLocationsUnique(const mrhiContainer* container, mrhiShaderEntry entry)
{
    return mrhiAreInterfaceLocationsUnique(container->inputs, entry.firstInput, entry.inputCount,
                                           false) &&
           mrhiAreInterfaceLocationsUnique(container->outputs, entry.firstOutput, entry.outputCount,
                                           true) &&
           mrhiAreInterfaceLocationsUnique(container->variables, entry.firstVariable,
                                           entry.variableCount, false);
}

// Whether a binding's details suit its kind.
static bool AreDetailsValid(const mrhiShaderBinding* binding)
{
    bool buffer = binding->kind == mrhi_bindingUniformBuffer ||
                  binding->kind == mrhi_bindingStorageBuffer ||
                  binding->kind == mrhi_bindingReadOnlyStorageBuffer;
    bool sampler = binding->kind == mrhi_bindingSampler;
    bool sampled = binding->kind == mrhi_bindingSampledTexture;
    bool storage = binding->kind == mrhi_bindingStorageTexture;
    bool samplerOk = sampler ? binding->sampler >= mrhi_samplerFiltering &&
                                   binding->sampler <= mrhi_samplerComparison
                             : binding->sampler == mrhi_samplerNone;
    bool sampleOk =
        sampled ? binding->sampleType >= mrhi_sampleFloat && binding->sampleType <= mrhi_sampleUint
                : binding->sampleType == mrhi_sampleNone;
    bool accessOk = storage ? binding->access >= mrhi_storageReadOnly &&
                                  binding->access <= mrhi_storageReadWrite &&
                                  mrhiIsFormatKnown(binding->format)
                            : binding->access == mrhi_storageNone && binding->format == 0;
    bool texture = sampled || storage;
    bool dimension = !texture  ? binding->viewDimension == 0
                     : storage ? binding->viewDimension == mrhi_texture2d ||
                                     binding->viewDimension == mrhi_texture2dArray ||
                                     binding->viewDimension == mrhi_texture3d
                               : binding->viewDimension <= mrhi_texture3d;
    bool multisampled =
        !binding->multisampled || (sampled && binding->viewDimension == mrhi_texture2d &&
                                   binding->sampleType != mrhi_sampleFloat);
    return samplerOk && sampleOk && accessOk && dimension && multisampled &&
           (buffer || binding->minSize == 0);
}

// Whether a binding is well formed, its table and slot unique among those
// before.
static bool IsBindingValid(const mrhiContainer* container, uint32_t index)
{
    const uint8_t* raw = container->bindings + (size_t)index * BINDING_BYTES;
    mrhiShaderBinding binding = mrhiContainerBinding(container, index);
    bool writable =
        binding.kind == mrhi_bindingStorageBuffer ||
        (binding.kind == mrhi_bindingStorageTexture && binding.access != mrhi_storageReadOnly);
    bool stages = binding.stages != 0 && (binding.stages & ~mrhiShaderStagesKnown) == 0 &&
                  !(writable && (binding.stages & mrhi_stageVertex) != 0);
    if (raw[14] > 1 || raw[15] != 0 || binding.table >= 4 || binding.kind == mrhi_bindingNone ||
        binding.kind > mrhi_bindingStorageTexture || !stages || !AreDetailsValid(&binding))
    {
        return false;
    }
    for (uint32_t i = 0; i < index; ++i)
    {
        mrhiShaderBinding other = mrhiContainerBinding(container, i);
        if (other.table == binding.table && other.slot == binding.slot)
        {
            return false;
        }
    }
    return true;
}

// Whether a constant is well formed, its id unique among those before.
static bool IsConstantValid(const mrhiContainer* container, uint32_t index)
{
    const uint8_t* raw = container->constants + (size_t)index * CONSTANT_BYTES;
    mrhiShaderConstant constant = mrhiContainerConstant(container, index);
    if (!mrhiIsZero(raw + 5, 3) || raw[12] > 1 || !mrhiIsZero(raw + 13, 3) ||
        constant.type == mrhi_constantNone || constant.type > mrhi_constantFloat32 ||
        (constant.type == mrhi_constantBool && constant.bits > 1) ||
        (constant.required && constant.bits != 0))
    {
        return false;
    }
    for (uint32_t i = 0; i < index; ++i)
    {
        if (mrhiContainerConstant(container, i).id == constant.id)
        {
            return false;
        }
    }
    return true;
}

// A binding's Metal class and its count of indices.
static uint32_t MetalClass(mrhiBindingKind kind, uint32_t* limitOut)
{
    switch (kind)
    {
    case mrhi_bindingSampler:
        *limitOut = METAL_SAMPLERS;
        return 2;
    case mrhi_bindingSampledTexture:
    case mrhi_bindingStorageTexture:
        *limitOut = METAL_TEXTURES;
        return 1;
    default:
        *limitOut = METAL_BUFFERS;
        return 0;
    }
}

// Whether a buffer index is a binding's, or the root block's.
static bool IsMetalBufferTaken(const mrhiContainer* container, uint8_t index)
{
    if (index == mrhiContainerMetalRoot(container))
    {
        return true;
    }
    for (uint32_t i = 0; i < container->bindingCount; ++i)
    {
        uint32_t limit = 0;
        if (MetalClass(mrhiContainerBinding(container, i).kind, &limit) == 0 &&
            mrhiContainerMetalIndex(container, i) == index)
        {
            return true;
        }
    }
    return false;
}

// Whether each binding's Metal index is in its class's range, apart from
// the bindings of its class before it and from the root block.
static bool AreMetalBindingsValid(const mrhiContainer* container)
{
    uint8_t root = mrhiContainerMetalRoot(container);
    for (uint32_t i = 0; i < container->bindingCount; ++i)
    {
        uint32_t limit = 0;
        uint32_t kind = MetalClass(mrhiContainerBinding(container, i).kind, &limit);
        uint8_t index = mrhiContainerMetalIndex(container, i);
        if (index >= limit || (kind == 0 && index == root))
        {
            return false;
        }
        for (uint32_t j = 0; j < i; ++j)
        {
            uint32_t otherLimit = 0;
            if (MetalClass(mrhiContainerBinding(container, j).kind, &otherLimit) == kind &&
                mrhiContainerMetalIndex(container, j) == index)
            {
                return false;
            }
        }
    }
    return true;
}

// Whether an entry's Metal record is well formed: its MSL inside the MSL
// section, or none without one, and its buffer sizes at a free index.
static bool IsMetalEntryValid(const mrhiContainer* container, uint32_t index)
{
    const uint8_t* raw = container->metalMap + 8 + (size_t)index * METAL_ENTRY_BYTES;
    mrhiMetalEntry entry = mrhiContainerMetalEntry(container, index);
    uint64_t end = (uint64_t)entry.mslOffset + entry.mslLength;
    bool code =
        container->msl == nullptr
            ? entry.mslOffset == 0 && entry.mslLength == 0
            : entry.mslLength > 0 && end <= container->mslBytes &&
                  mrhiIsTextValid((const char*)container->msl + entry.mslOffset, entry.mslLength);
    bool sizes =
        entry.sizesIndex == MRHI_METAL_NONE ||
        (entry.sizesIndex < METAL_BUFFERS && !IsMetalBufferTaken(container, entry.sizesIndex));
    return mrhiIsZero(raw + 9, 7) && code && sizes;
}

// Whether the Metal code agrees with the rules: the map exactly when
// there is MSL or a metallib and no entry uses a heap, of its size, with
// its root block, bindings and entries well formed.
static bool IsMetalValid(const mrhiContainer* container)
{
    bool code = container->msl != nullptr || container->metallib != nullptr;
    if (container->metalMap == nullptr)
    {
        return !code;
    }
    uint64_t size =
        8 + (uint64_t)container->entryCount * METAL_ENTRY_BYTES + container->bindingCount;
    if (!code || container->heapUses != 0 || container->metalMapBytes != size)
    {
        return false;
    }
    uint8_t root = mrhiContainerMetalRoot(container);
    bool rootValid =
        container->rootBlockBytes == 0 ? root == MRHI_METAL_NONE : root < METAL_BUFFERS;
    if (!mrhiIsZero(container->metalMap + 1, 7) || !rootValid || !AreMetalBindingsValid(container))
    {
        return false;
    }
    for (uint32_t i = 0; i < container->entryCount; ++i)
    {
        if (!IsMetalEntryValid(container, i))
        {
            return false;
        }
    }
    return true;
}

// A binding's D3D12 class: constant buffers, shader resource views,
// unordered access views or samplers.
enum
{
    D3D12_CBV,
    D3D12_SRV,
    D3D12_UAV,
    D3D12_SAMPLER,
};

static int D3d12Class(mrhiBindingKind kind)
{
    switch (kind)
    {
    case mrhi_bindingUniformBuffer:
        return D3D12_CBV;
    case mrhi_bindingSampledTexture:
    case mrhi_bindingReadOnlyStorageBuffer:
        return D3D12_SRV;
    case mrhi_bindingSampler:
        return D3D12_SAMPLER;
    default:
        return D3D12_UAV;
    }
}

static bool IsSamePlace(mrhiD3d12Place a, mrhiD3d12Place b)
{
    return a.reg == b.reg && a.space == b.space;
}

// Whether the map's own constant buffers are placed exactly when the
// container has them, in range and apart; notes which it has.
static bool AreD3d12BuffersValid(const mrhiContainer* container, bool vertexInfo, bool* present)
{
    present[mrhiD3d12RootBlock] = container->rootBlockBytes > 0;
    present[mrhiD3d12Constants] = container->constantCount > 0;
    present[mrhiD3d12VertexInfo] = vertexInfo;
    for (int i = mrhiD3d12RootBlock; i <= mrhiD3d12VertexInfo; ++i)
    {
        mrhiD3d12Place place = mrhiContainerD3d12Buffer(container, (mrhiD3d12MapBuffer)i);
        if (!present[i])
        {
            if (place.reg != 0 || place.space != 0)
            {
                return false;
            }
            continue;
        }
        if (place.space >= MRHI_D3D12_HEAP_SPACE)
        {
            return false;
        }
        for (int j = mrhiD3d12RootBlock; j < i; ++j)
        {
            if (present[j] &&
                IsSamePlace(place, mrhiContainerD3d12Buffer(container, (mrhiD3d12MapBuffer)j)))
            {
                return false;
            }
        }
    }
    return true;
}

// Whether each binding's place is in range and apart from the bindings
// of its class before it and, for a constant buffer, from the map's own.
static bool AreD3d12BindingsValid(const mrhiContainer* container, const bool* present)
{
    for (uint32_t i = 0; i < container->bindingCount; ++i)
    {
        int kind = D3d12Class(mrhiContainerBinding(container, i).kind);
        mrhiD3d12Place place = mrhiContainerD3d12Binding(container, i);
        if (place.space >= MRHI_D3D12_HEAP_SPACE)
        {
            return false;
        }
        for (int b = mrhiD3d12RootBlock; kind == D3D12_CBV && b <= mrhiD3d12VertexInfo; ++b)
        {
            if (present[b] &&
                IsSamePlace(place, mrhiContainerD3d12Buffer(container, (mrhiD3d12MapBuffer)b)))
            {
                return false;
            }
        }
        for (uint32_t j = 0; j < i; ++j)
        {
            if (D3d12Class(mrhiContainerBinding(container, j).kind) == kind &&
                IsSamePlace(place, mrhiContainerD3d12Binding(container, j)))
            {
                return false;
            }
        }
    }
    return true;
}

// Whether an entry's D3D12 record is well formed: a DXIL container
// inside the DXIL section, and the vertex information read only by a
// vertex entry.
static bool IsD3d12EntryValid(const mrhiContainer* container, uint32_t index)
{
    const uint8_t* raw = container->d3d12Map + D3D12_HEAD_BYTES + (size_t)index * D3D12_ENTRY_BYTES;
    mrhiD3d12Entry entry = mrhiContainerD3d12Entry(container, index);
    uint32_t reads = mrhiRead32(raw + 8);
    uint64_t end = (uint64_t)entry.dxilOffset + entry.dxilLength;
    if (!mrhiIsZero(raw + 12, 4) || reads > 1 || entry.dxilOffset % 4 != 0 ||
        entry.dxilLength < DXBC_HEADER_BYTES || end > container->dxilBytes ||
        (reads == 1 && mrhiContainerEntry(container, index).stage != mrhi_stageVertex))
    {
        return false;
    }
    const uint8_t* dxil = container->dxil + entry.dxilOffset;
    return memcmp(dxil, "DXBC", 4) == 0 && mrhiRead32(dxil + 24) == entry.dxilLength;
}

// Whether the heap ranges are well formed: in the heap's spaces, apart
// within their class, resource ranges exactly when an entry reads
// resources from the heap and sampler ranges exactly when one reads
// samplers.
static bool AreD3d12HeapRangesValid(const mrhiContainer* container)
{
    uint32_t count = mrhiContainerD3d12HeapRangeCount(container);
    bool resources = false;
    bool samplers = false;
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint8_t* raw = D3d12HeapRanges(container) + (size_t)i * D3D12_RANGE_BYTES;
        mrhiD3d12HeapRange range = mrhiContainerD3d12HeapRange(container, i);
        if (mrhiRead32(raw) > mrhiD3d12HeapSampler || !mrhiIsZero(raw + 12, 4) ||
            range.space < MRHI_D3D12_HEAP_SPACE || range.space >= D3D12_SPACES)
        {
            return false;
        }
        for (uint32_t j = 0; j < i; ++j)
        {
            mrhiD3d12HeapRange other = mrhiContainerD3d12HeapRange(container, j);
            if (other.rangeClass == range.rangeClass && other.reg == range.reg &&
                other.space == range.space)
            {
                return false;
            }
        }
        samplers = samplers || range.rangeClass == mrhiD3d12HeapSampler;
        resources = resources || range.rangeClass != mrhiD3d12HeapSampler;
    }
    const mrhiShaderHeapUses reads =
        mrhi_heapUseSampledTextures | mrhi_heapUseStorageTextures | mrhi_heapUseStorageBuffers;
    return resources == ((container->heapUses & reads) != 0) &&
           samplers == ((container->heapUses & mrhi_heapUseSamplers) != 0);
}

// Whether the D3D12 code agrees with the rules: the map exactly when
// there is DXIL, of its size, with its entries, constant buffers,
// bindings, heap ranges and constants well formed.
static bool IsD3d12Valid(const mrhiContainer* container)
{
    if (container->d3d12Map == nullptr || container->dxil == nullptr)
    {
        return container->d3d12Map == nullptr && container->dxil == nullptr;
    }
    if (container->d3d12MapBytes < D3D12_HEAD_BYTES)
    {
        return false;
    }
    uint32_t ranges = mrhiContainerD3d12HeapRangeCount(container);
    uint64_t size = D3D12_HEAD_BYTES + (uint64_t)container->entryCount * D3D12_ENTRY_BYTES +
                    (uint64_t)container->bindingCount * 8 + (uint64_t)ranges * D3D12_RANGE_BYTES +
                    container->constantCount;
    if (ranges > MRHI_MAX_CONTAINER_RECORDS || container->d3d12MapBytes != size ||
        !mrhiIsZero(container->d3d12Map + 28, 4) || !AreD3d12HeapRangesValid(container))
    {
        return false;
    }
    bool vertexInfo = false;
    for (uint32_t i = 0; i < container->entryCount; ++i)
    {
        if (!IsD3d12EntryValid(container, i))
        {
            return false;
        }
        vertexInfo = vertexInfo || mrhiContainerD3d12Entry(container, i).vertexInfo;
    }
    bool present[3];
    if (!AreD3d12BuffersValid(container, vertexInfo, present) ||
        !AreD3d12BindingsValid(container, present))
    {
        return false;
    }
    const uint8_t* fixed = D3d12HeapRanges(container) + (size_t)ranges * D3D12_RANGE_BYTES;
    for (uint32_t i = 0; i < container->constantCount; ++i)
    {
        if (fixed[i] > 1 || (fixed[i] == 1 && mrhiContainerConstant(container, i).required))
        {
            return false;
        }
    }
    return true;
}

// Whether the records agree with the rules and with each other; notes
// 16-bit floats and the builtins and heap uses of the entries.
static bool AreRecordsValid(mrhiContainer* container)
{
    if (container->entryCount == 0 ||
        !mrhiAreInterfaceRecordsValid(container->inputs, container->inputCount, mrhi_interfaceInput,
                                      &container->float16) ||
        !mrhiAreInterfaceRecordsValid(container->outputs, container->outputCount,
                                      mrhi_interfaceOutput, &container->float16) ||
        !mrhiAreInterfaceRecordsValid(container->variables, container->variableCount,
                                      mrhi_interfaceVariable, &container->float16))
    {
        return false;
    }
    for (uint32_t i = 0; i < container->entryCount; ++i)
    {
        mrhiShaderEntry entry = mrhiContainerEntry(container, i);
        if (!IsEntryValid(container, i) || !AreEntryLocationsUnique(container, entry))
        {
            return false;
        }
        container->builtins |= entry.builtins;
        container->heapUses |= entry.heapUses;
    }
    // WGSL reads no heaps yet and has no view index or 64-bit integers: a
    // container using any has no WGSL, and one using none needs it.
    bool native = container->heapUses != 0 || (container->builtins & mrhi_builtinViewIndex) != 0 ||
                  container->int64;
    if (native == (container->wgslBytes > 0))
    {
        return false;
    }
    for (uint32_t i = 0; i < container->bindingCount; ++i)
    {
        if (!IsBindingValid(container, i))
        {
            return false;
        }
    }
    for (uint32_t i = 0; i < container->constantCount; ++i)
    {
        if (!IsConstantValid(container, i))
        {
            return false;
        }
    }
    return IsMetalValid(container) && IsD3d12Valid(container);
}

// Takes the meta, strings and code sections. An absent section has size
// 0, so the size checks require the SPIR-V; an absent meta is a root
// block of 0 bytes, which a present one never holds; the strings are
// required by the entries' names, and the WGSL unless an entry uses a heap
// or the view index or the SPIR-V declares 64-bit integers.
// A metallib has its magic; the Metal map, MSL, D3D12 map and DXIL are
// checked with the records.
static bool TakeParts(const uint8_t* bytes, const mrhiContainerSection* sections, uint32_t count,
                      mrhiContainer* container)
{
    uint64_t size = 0;
    const uint8_t* meta = FindSection(bytes, sections, count, SECTION_META, &size);
    if (meta != nullptr && (size != 16 || mrhiRead32(meta) == 0 || !mrhiIsZero(meta + 4, 12)))
    {
        return false;
    }
    container->rootBlockBytes = meta != nullptr ? mrhiRead32(meta) : 0;
    container->strings = FindSection(bytes, sections, count, SECTION_STRINGS, &size);
    container->stringBytes = (uint32_t)size;
    bool strings = size <= UINT32_MAX;
    container->spirv = FindSection(bytes, sections, count, SECTION_SPIRV, &size);
    container->spirvBytes = size;
    bool spirv = size >= 20 && size % 4 == 0 && mrhiRead32(container->spirv) == SPIRV_MAGIC;
    if (spirv)
    {
        mrhiReadSpirvCapabilities(container->spirv, size, &container->float16,
                                  &container->subgroups, &container->int64);
    }
    container->wgsl = FindSection(bytes, sections, count, SECTION_WGSL, &size);
    container->wgslBytes = size;
    bool wgsl = size == 0 || mrhiIsTextValid((const char*)container->wgsl, size);
    container->wgsl = size > 0 ? container->wgsl : nullptr;
    container->metalMap = FindSection(bytes, sections, count, SECTION_METAL_MAP, &size);
    container->metalMapBytes = size;
    container->msl = FindSection(bytes, sections, count, SECTION_MSL, &size);
    container->mslBytes = size;
    container->msl = size > 0 ? container->msl : nullptr;
    container->metallib = FindSection(bytes, sections, count, SECTION_METALLIB, &size);
    container->metallibBytes = size;
    container->metallib = size > 0 ? container->metallib : nullptr;
    bool metallib = size == 0 || (size >= 4 && memcmp(container->metallib, "MTLB", 4) == 0);
    container->d3d12Map = FindSection(bytes, sections, count, SECTION_D3D12_MAP, &size);
    container->d3d12MapBytes = size;
    container->dxil = FindSection(bytes, sections, count, SECTION_DXIL, &size);
    container->dxilBytes = size;
    container->dxil = size > 0 ? container->dxil : nullptr;
    return container->rootBlockBytes % 4 == 0 &&
           container->rootBlockBytes <= MRHI_MAX_CONTAINER_ROOT_BLOCK_BYTES && strings && spirv &&
           wgsl && metallib;
}

// Whether every offset the library follows after the parse lies inside
// its section: each entry's name in the strings, its interface ranges in
// their arrays, its MSL and DXIL in theirs. The record checks above make
// it so; only an assertion calls it.
[[maybe_unused]] static bool IsSliced(const mrhiContainer* container)
{
    for (uint32_t i = 0; i < container->entryCount; ++i)
    {
        mrhiShaderEntry entry = mrhiContainerEntry(container, i);
        bool sliced = Inside(container->strings, container->stringBytes,
                             container->strings + entry.nameOffset, entry.nameLength) &&
                      InArray(entry.firstInput, entry.inputCount, container->inputCount) &&
                      InArray(entry.firstOutput, entry.outputCount, container->outputCount) &&
                      InArray(entry.firstVariable, entry.variableCount, container->variableCount);
        if (container->msl != nullptr)
        {
            mrhiMetalEntry metal = mrhiContainerMetalEntry(container, i);
            sliced = sliced && Inside(container->msl, container->mslBytes,
                                      container->msl + metal.mslOffset, metal.mslLength);
        }
        if (container->dxil != nullptr)
        {
            mrhiD3d12Entry d3d12 = mrhiContainerD3d12Entry(container, i);
            sliced = sliced && Inside(container->dxil, container->dxilBytes,
                                      container->dxil + d3d12.dxilOffset, d3d12.dxilLength);
        }
        if (!sliced)
        {
            return false;
        }
    }
    return true;
}

mrhiResult mrhiParseContainer(const void* bytes, size_t size, mrhiContainer* containerOut)
{
    const uint8_t* data = bytes;
    uint32_t count = 0;
    mrhiContainerSection sections[MRHI_MAX_CONTAINER_SECTIONS];
    mrhiResult status = mrhiReadContainerTable(data, size, sections, &count);
    if (status != mrhi_success)
    {
        return status;
    }
    mrhiContainer container = {0};
    for (int i = 0; i < MRHI_DIGEST_BYTES; ++i)
    {
        container.digest[i] = data[16 + i];
    }
    bool valid = TakeParts(data, sections, count, &container) &&
                 TakeArray(data, sections, count, SECTION_ENTRIES, ENTRY_BYTES, &container.entries,
                           &container.entryCount) &&
                 TakeArray(data, sections, count, SECTION_BINDINGS, BINDING_BYTES,
                           &container.bindings, &container.bindingCount) &&
                 TakeArray(data, sections, count, SECTION_INPUTS, MRHI_VARIABLE_BYTES,
                           &container.inputs, &container.inputCount) &&
                 TakeArray(data, sections, count, SECTION_OUTPUTS, MRHI_VARIABLE_BYTES,
                           &container.outputs, &container.outputCount) &&
                 TakeArray(data, sections, count, SECTION_CONSTANTS, CONSTANT_BYTES,
                           &container.constants, &container.constantCount) &&
                 TakeArray(data, sections, count, SECTION_VARIABLES, MRHI_VARIABLE_BYTES,
                           &container.variables, &container.variableCount) &&
                 AreRecordsValid(&container);
    if (!valid)
    {
        return mrhi_errorFormat;
    }
    MRHI_ASSERT(IsSliced(&container));
    *containerOut = container;
    return mrhi_success;
}
