// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's root signatures (d3d12_root.h), version 1.1: root
// constants for the root block, the constants (in rows of four, as the
// constants' buffer declares them) and the vertex information where the
// container has them, then per table a descriptor table of its
// resources and one of its samplers where it has them, each binding a
// one-descriptor range at its register and space, in the container's
// order, then a heap table of each class of the container's heap ranges
// (mrhi-0015), each range unbounded from the table's start: D3D12 lets
// ranges of one class alias, not of two. Every parameter is visible to
// every stage. Resource data is volatile, since frames may write a
// resource between passes that read it; heap descriptors are volatile
// too, since entries are written while other frames run.

#include "d3d12_root.h"

#include "container.h"
#include "invariant.h"

#include <limits.h>
#include <stdalign.h>

// The words a root signature holds, and the parameters it can have.
#define ROOT_WORDS      64
#define ROOT_PARAMETERS (3 + 2 * MRHI_D3D12_TABLES + MRHI_D3D12_HEAP_CLASSES)

static D3D12_DESCRIPTOR_RANGE_TYPE RangeOf(mrhiBindingKind kind)
{
    switch (kind)
    {
    case mrhi_bindingUniformBuffer:
        return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    case mrhi_bindingSampledTexture:
    case mrhi_bindingReadOnlyStorageBuffer:
        return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    case mrhi_bindingSampler:
        return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    default:
        return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    }
}

static D3D12_ROOT_PARAMETER1 Constants(mrhiD3d12Place place, uint32_t words)
{
    return (D3D12_ROOT_PARAMETER1){
        .ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
        .Constants = {.ShaderRegister = place.reg,
                      .RegisterSpace = place.space,
                      .Num32BitValues = words},
        .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
    };
}

// Lays each binding in its table's ranges, resources and samplers
// apart, and counts each table's descriptors.
static void LayRanges(const mrhiContainer* container, mrhiD3d12Layout* layout,
                      mrhiD3d12Binding* bindings, D3D12_DESCRIPTOR_RANGE1* ranges)
{
    for (uint32_t i = 0; i < container->bindingCount; ++i)
    {
        mrhiShaderBinding binding = mrhiContainerBinding(container, i);
        mrhiD3d12Place place = mrhiContainerD3d12Binding(container, i);
        MRHI_ASSERT(binding.table < MRHI_D3D12_TABLES);
        D3D12_DESCRIPTOR_RANGE_TYPE type = RangeOf(binding.kind);
        bool sampler = type == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        uint16_t* count = sampler ? &layout->samplerCounts[binding.table]
                                  : &layout->resourceCounts[binding.table];
        bindings[i] = (mrhiD3d12Binding){
            .slot = binding.slot,
            .table = binding.table,
            .kind = (uint8_t)binding.kind,
            .offset = *count,
        };
        ranges[i] = (D3D12_DESCRIPTOR_RANGE1){
            .RangeType = type,
            .NumDescriptors = 1,
            .BaseShaderRegister = place.reg,
            .RegisterSpace = place.space,
            .Flags = sampler ? D3D12_DESCRIPTOR_RANGE_FLAG_NONE
                             : D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE,
            .OffsetInDescriptorsFromTableStart = *count,
        };
        ++*count;
    }
}

// Sorts the ranges by table and class, sampler ranges after resources,
// into sorted, keeping the container's order within each.
static void SortRanges(const mrhiContainer* container, const D3D12_DESCRIPTOR_RANGE1* ranges,
                       D3D12_DESCRIPTOR_RANGE1* sorted)
{
    uint32_t at = 0;
    for (uint32_t t = 0; t < 2 * MRHI_D3D12_TABLES; ++t)
    {
        bool samplers = t % 2 == 1;
        for (uint32_t i = 0; i < container->bindingCount; ++i)
        {
            bool sampler = ranges[i].RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
            if (mrhiContainerBinding(container, i).table == t / 2 && sampler == samplers)
            {
                sorted[at++] = ranges[i];
            }
        }
    }
}

// Adds the root constants and the descriptor tables to parameters, and
// counts the root signature's words.
static uint32_t AddParameters(const mrhiContainer* container, mrhiD3d12Layout* layout,
                              const D3D12_DESCRIPTOR_RANGE1* sorted,
                              D3D12_ROOT_PARAMETER1* parameters, uint32_t* countOut)
{
    uint32_t count = 0;
    uint32_t words = 0;
    bool vertexInfo = false;
    for (uint32_t i = 0; i < container->entryCount; ++i)
    {
        vertexInfo = vertexInfo || mrhiContainerD3d12Entry(container, i).vertexInfo;
    }
    const uint32_t constantWords[3] = {layout->rootWords, layout->constantWords,
                                       vertexInfo ? 2 : 0};
    uint8_t* indices[3] = {&layout->rootParameter, &layout->constantsParameter,
                           &layout->vertexInfoParameter};
    for (int b = 0; b < 3; ++b)
    {
        if (constantWords[b] > 0)
        {
            *indices[b] = (uint8_t)count;
            parameters[count++] = Constants(
                mrhiContainerD3d12Buffer(container, (mrhiD3d12MapBuffer)b), constantWords[b]);
            words += constantWords[b];
        }
    }
    uint32_t first = 0;
    for (uint32_t t = 0; t < 2 * MRHI_D3D12_TABLES; ++t)
    {
        uint16_t ranges = t % 2 == 0 ? layout->resourceCounts[t / 2] : layout->samplerCounts[t / 2];
        if (ranges > 0)
        {
            (t % 2 == 0 ? layout->resourceParameters : layout->samplerParameters)[t / 2] =
                (uint8_t)count;
            parameters[count++] = (D3D12_ROOT_PARAMETER1){
                .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
                .DescriptorTable = {.NumDescriptorRanges = ranges,
                                    .pDescriptorRanges = sorted + first},
                .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
            };
            first += ranges;
            ++words;
        }
    }
    *countOut = count;
    return words;
}

// Adds a heap table of each class the container's heap ranges have to
// parameters, laying their ranges in ranges by class, and answers the
// words they take.
static uint32_t AddHeapTables(const mrhiContainer* container, mrhiD3d12Layout* layout,
                              D3D12_DESCRIPTOR_RANGE1* ranges, D3D12_ROOT_PARAMETER1* parameters,
                              uint32_t* countOut)
{
    static const D3D12_DESCRIPTOR_RANGE_TYPE types[MRHI_D3D12_HEAP_CLASSES] = {
        [mrhiD3d12HeapResource] = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
        [mrhiD3d12HeapStorage] = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
        [mrhiD3d12HeapSampler] = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,
    };
    uint32_t words = 0;
    uint32_t at = 0;
    for (uint32_t c = 0; c < MRHI_D3D12_HEAP_CLASSES; ++c)
    {
        uint32_t first = at;
        for (uint32_t i = 0; i < mrhiContainerD3d12HeapRangeCount(container); ++i)
        {
            mrhiD3d12HeapRange range = mrhiContainerD3d12HeapRange(container, i);
            if ((uint32_t)range.rangeClass == c)
            {
                ranges[at++] = (D3D12_DESCRIPTOR_RANGE1){
                    .RangeType = types[c],
                    .NumDescriptors = UINT_MAX,
                    .BaseShaderRegister = range.reg,
                    .RegisterSpace = range.space,
                    .Flags = c == mrhiD3d12HeapSampler
                                 ? D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE
                                 : D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
                                       D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE,
                };
            }
        }
        if (at > first)
        {
            layout->heapParameters[c] = (uint8_t)*countOut;
            parameters[(*countOut)++] = (D3D12_ROOT_PARAMETER1){
                .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
                .DescriptorTable = {.NumDescriptorRanges = at - first,
                                    .pDescriptorRanges = ranges + first},
                .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
            };
            ++words;
        }
    }
    return words;
}

// Serializes and makes a root signature: nullptr when D3D12 refuses.
static ID3D12RootSignature* Make(const mrhiD3d12Api* api, ID3D12Device* device,
                                 const D3D12_ROOT_PARAMETER1* parameters, uint32_t count)
{
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {
        .Version = D3D_ROOT_SIGNATURE_VERSION_1_1,
        .Desc_1_1 = {.NumParameters = count,
                     .pParameters = parameters,
                     .Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT},
    };
    ID3DBlob* blob = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT result = api->serializeRootSignature(&desc, &blob, &errors);
    if (errors != nullptr)
    {
        ID3D10Blob_Release(errors);
    }
    ID3D12RootSignature* root = nullptr;
    if (SUCCEEDED(result))
    {
        result = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
                                                  ID3D10Blob_GetBufferSize(blob),
                                                  &IID_ID3D12RootSignature, (void**)&root);
        ID3D10Blob_Release(blob);
    }
    return SUCCEEDED(result) ? root : nullptr;
}

mrhiResult mrhiD3d12MakeRoot(const mrhiAllocator* allocator, const mrhiD3d12Api* api,
                             ID3D12Device* device, const mrhiContainer* container,
                             mrhiD3d12Layout* layoutOut, mrhiD3d12Binding* bindingsOut,
                             ID3D12RootSignature** rootOut)
{
    *rootOut = nullptr;
    *layoutOut = (mrhiD3d12Layout){
        .rootParameter = MRHI_D3D12_NO_PARAMETER,
        .constantsParameter = MRHI_D3D12_NO_PARAMETER,
        .vertexInfoParameter = MRHI_D3D12_NO_PARAMETER,
        .resourceParameters = {MRHI_D3D12_NO_PARAMETER, MRHI_D3D12_NO_PARAMETER,
                               MRHI_D3D12_NO_PARAMETER, MRHI_D3D12_NO_PARAMETER},
        .samplerParameters = {MRHI_D3D12_NO_PARAMETER, MRHI_D3D12_NO_PARAMETER,
                              MRHI_D3D12_NO_PARAMETER, MRHI_D3D12_NO_PARAMETER},
        .heapParameters = {MRHI_D3D12_NO_PARAMETER, MRHI_D3D12_NO_PARAMETER,
                           MRHI_D3D12_NO_PARAMETER},
        .rootWords = container->rootBlockBytes / 4,
        .constantWords = (container->constantCount + 3) / 4 * 4,
    };
    // Past the words, the root signature fails before its ranges matter.
    if (layoutOut->rootWords + layoutOut->constantWords > ROOT_WORDS)
    {
        return mrhi_errorUnsupported;
    }
    // Room for the bindings' ranges twice, unsorted and sorted, then the
    // heap ranges, and never none.
    size_t room = container->bindingCount > 0 ? container->bindingCount : 1;
    size_t bytes =
        (2 * room + mrhiContainerD3d12HeapRangeCount(container)) * sizeof(D3D12_DESCRIPTOR_RANGE1);
    D3D12_DESCRIPTOR_RANGE1* ranges =
        mrhiAllocate(allocator, bytes, alignof(D3D12_DESCRIPTOR_RANGE1));
    if (ranges == nullptr)
    {
        return mrhi_errorCapacity;
    }
    D3D12_DESCRIPTOR_RANGE1* sorted = ranges + room;
    LayRanges(container, layoutOut, bindingsOut, ranges);
    SortRanges(container, ranges, sorted);
    D3D12_ROOT_PARAMETER1 parameters[ROOT_PARAMETERS];
    uint32_t count = 0;
    uint32_t words = AddParameters(container, layoutOut, sorted, parameters, &count);
    words += AddHeapTables(container, layoutOut, sorted + room, parameters, &count);
    mrhiResult status = mrhi_errorUnsupported;
    if (words <= ROOT_WORDS)
    {
        *rootOut = Make(api, device, parameters, count);
        status = *rootOut != nullptr ? mrhi_success : mrhi_errorPlatform;
    }
    mrhiRelease(allocator, ranges, bytes, alignof(D3D12_DESCRIPTOR_RANGE1));
    return status;
}
