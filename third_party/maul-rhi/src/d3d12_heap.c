// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Heaps on D3D12 (d3d12_heap.h). A heap is only its slot: its regions
// lie at the slot's place in every frame slot's shader-visible heaps,
// each region as large as the device's heap limits, so a heap made
// again over a retired one's slot takes its regions once no frame reads
// them. An entry is written into every slot's region, which no running
// frame reads at that index, so frames submitted after it read it
// whichever slot they take. A sampled texture is its view's shader
// resource view and a storage texture its unordered access view; a
// storage buffer is a raw unordered access view when shaders may write
// it, else a raw shader resource view. Emptying an entry writes
// nothing: shaders may not read an empty entry.

#include "d3d12_heap.h"

#include "invariant.h"

mrhiResult mrhiD3d12CreateHeap(mrhiD3d12Frames* frames, uint64_t* handleOut)
{
    *handleOut = mrhiD3d12TakeSlot(&frames->objects->heapSlots);
    return *handleOut != 0 ? mrhi_success : mrhi_errorCapacity;
}

// A descriptor of a shader-visible heap, a region's entry.
static D3D12_CPU_DESCRIPTOR_HANDLE At(ID3D12DescriptorHeap* heap, UINT step, uint64_t index)
{
    D3D12_CPU_DESCRIPTOR_HANDLE start;
    (void)ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap, &start);
    return (D3D12_CPU_DESCRIPTOR_HANDLE){.ptr = start.ptr + (SIZE_T)(step * index)};
}

// Writes a storage buffer entry's raw view.
static void WriteBuffer(const mrhiD3d12Frames* frames, const mrhiDriverHeapEntry* entry,
                        D3D12_CPU_DESCRIPTOR_HANDLE at)
{
    ID3D12Resource* resource = frames->objects->buffers[entry->handle - 1].resource;
    UINT64 first = entry->offset / sizeof(uint32_t);
    UINT count = (UINT)(entry->size / sizeof(uint32_t));
    if (entry->writable)
    {
        const D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {
            .Format = DXGI_FORMAT_R32_TYPELESS,
            .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
            .Buffer = {.FirstElement = first,
                       .NumElements = count,
                       .Flags = D3D12_BUFFER_UAV_FLAG_RAW},
        };
        ID3D12Device_CreateUnorderedAccessView(frames->device, resource, nullptr, &desc, at);
        return;
    }
    const D3D12_SHADER_RESOURCE_VIEW_DESC desc = {
        .Format = DXGI_FORMAT_R32_TYPELESS,
        .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        .Buffer = {.FirstElement = first, .NumElements = count, .Flags = D3D12_BUFFER_SRV_FLAG_RAW},
    };
    ID3D12Device_CreateShaderResourceView(frames->device, resource, &desc, at);
}

void mrhiD3d12WriteHeapEntry(const mrhiD3d12Frames* frames, uint64_t heap, uint32_t index,
                             const mrhiDriverHeapEntry* entry)
{
    MRHI_ASSERT(heap != 0 && index < frames->heapEntries);
    uint64_t place = (heap - 1) * frames->heapEntries + index;
    for (uint32_t i = 0; i < frames->slotCount; ++i)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE at =
            At(frames->slots[i].viewHeap, frames->objects->viewStep, place);
        if (entry->kind == mrhi_heapStorageBuffer)
        {
            WriteBuffer(frames, entry, at);
            continue;
        }
        ID3D12Device_CopyDescriptorsSimple(
            frames->device, 1, at,
            mrhiD3d12ViewDescriptor(frames->objects, entry->handle,
                                    entry->kind == mrhi_heapStorageTexture),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
}

void mrhiD3d12WriteHeapSampler(const mrhiD3d12Frames* frames, uint64_t heap, uint32_t index,
                               uint64_t sampler)
{
    MRHI_ASSERT(heap != 0 && index < frames->heapSamplers);
    uint64_t place = (heap - 1) * frames->heapSamplers + index;
    for (uint32_t i = 0; i < frames->slotCount; ++i)
    {
        ID3D12Device_CopyDescriptorsSimple(
            frames->device, 1,
            At(frames->slots[i].samplerHeap, frames->objects->samplerStep, place),
            mrhiD3d12SamplerDescriptor(frames->objects, sampler),
            D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    }
}
