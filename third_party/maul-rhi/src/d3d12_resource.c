// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's tables, buffers, textures and query sets
// (mrhi-0003); views and samplers are in d3d12_view.c. Buffers and
// textures are committed resources in the GPU's default heap, as the
// frame graph fills and reads them, and start in the common state. A
// buffer's size rounds up to 256 bytes, so that a uniform binding's
// constant buffer view, whose size D3D12 rounds up alike, stays inside
// it. A texture whose views see it in other formats, or a depth
// texture, which views read as a color format, is made in its typeless
// family.

#include "d3d12_resource.h"

#include "d3d12_names.h"
#include "invariant.h"

#include <stdalign.h>

// The bytes D3D12 reads a constant buffer view in.
#define CONSTANT_BYTES 256

static const D3D12_HEAP_PROPERTIES s_defaultHeap = {.Type = D3D12_HEAP_TYPE_DEFAULT};

mrhiD3d12ObjectRoom mrhiD3d12PlanObjects(mrhiLayout* layout, const mrhiDeviceLimits* limits)
{
    uint64_t slots = (uint64_t)limits->buffers + limits->textures + limits->views +
                     limits->samplers + limits->querySets + limits->heaps;
    return (mrhiD3d12ObjectRoom){
        .buffers = mrhiLayoutAdd(layout, limits->buffers, sizeof(mrhiD3d12Buffer),
                                 alignof(mrhiD3d12Buffer)),
        .textures = mrhiLayoutAdd(layout, limits->textures, sizeof(mrhiD3d12Texture),
                                  alignof(mrhiD3d12Texture)),
        .views =
            mrhiLayoutAdd(layout, limits->views, sizeof(mrhiD3d12View), alignof(mrhiD3d12View)),
        .querySets = mrhiLayoutAdd(layout, limits->querySets, sizeof(mrhiD3d12QuerySet),
                                   alignof(mrhiD3d12QuerySet)),
        .slots = mrhiLayoutAdd(layout, (size_t)slots, sizeof(uint32_t), alignof(uint32_t)),
    };
}

void mrhiD3d12LayObjects(mrhiD3d12Objects* objects, unsigned char* block,
                         const mrhiD3d12ObjectRoom* room, const mrhiDeviceLimits* limits)
{
    objects->buffers = (mrhiD3d12Buffer*)(block + room->buffers);
    objects->textures = (mrhiD3d12Texture*)(block + room->textures);
    objects->views = (mrhiD3d12View*)(block + room->views);
    objects->querySets = (mrhiD3d12QuerySet*)(block + room->querySets);
    uint32_t* next = (uint32_t*)(block + room->slots);
    next = mrhiD3d12InitSlots(&objects->bufferSlots, next, limits->buffers);
    next = mrhiD3d12InitSlots(&objects->textureSlots, next, limits->textures);
    next = mrhiD3d12InitSlots(&objects->viewSlots, next, limits->views);
    next = mrhiD3d12InitSlots(&objects->samplerSlots, next, limits->samplers);
    next = mrhiD3d12InitSlots(&objects->querySetSlots, next, limits->querySets);
    (void)mrhiD3d12InitSlots(&objects->heapSlots, next, limits->heaps);
}

// A CPU-only descriptor heap of count descriptors, and where it starts.
static ID3D12DescriptorHeap* MakeHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                                      uint32_t count, D3D12_CPU_DESCRIPTOR_HANDLE* startOut)
{
    // At least one descriptor, which D3D12 requires of a heap.
    D3D12_DESCRIPTOR_HEAP_DESC desc = {.Type = type, .NumDescriptors = count > 0 ? count : 1};
    ID3D12DescriptorHeap* heap = nullptr;
    if (FAILED(ID3D12Device_CreateDescriptorHeap(device, &desc, &IID_ID3D12DescriptorHeap,
                                                 (void**)&heap)))
    {
        return nullptr;
    }
    (void)ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap, startOut);
    return heap;
}

mrhiResult mrhiD3d12OpenObjects(mrhiD3d12Objects* objects)
{
    ID3D12Device* device = objects->device;
    objects->viewHeap = MakeHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                 2 * objects->viewSlots.capacity, &objects->viewStart);
    objects->samplerHeap = MakeHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                                    objects->samplerSlots.capacity, &objects->samplerStart);
    if (objects->viewHeap == nullptr || objects->samplerHeap == nullptr)
    {
        mrhiD3d12CloseObjects(objects);
        return mrhi_errorCapacity;
    }
    objects->viewStep = ID3D12Device_GetDescriptorHandleIncrementSize(
        device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    objects->samplerStep =
        ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    return mrhi_success;
}

void mrhiD3d12CloseObjects(mrhiD3d12Objects* objects)
{
    if (objects->viewHeap != nullptr)
    {
        ID3D12DescriptorHeap_Release(objects->viewHeap);
    }
    if (objects->samplerHeap != nullptr)
    {
        ID3D12DescriptorHeap_Release(objects->samplerHeap);
    }
    objects->viewHeap = nullptr;
    objects->samplerHeap = nullptr;
}

// A committed resource in the default heap: nullptr when D3D12 makes
// none.
static ID3D12Resource* Commit(const mrhiD3d12Objects* objects, const D3D12_RESOURCE_DESC* desc,
                              const char* label, size_t labelLength)
{
    ID3D12Resource* resource = nullptr;
    if (FAILED(ID3D12Device_CreateCommittedResource(
            objects->device, &s_defaultHeap, D3D12_HEAP_FLAG_NONE, desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, &IID_ID3D12Resource, (void**)&resource)))
    {
        return nullptr;
    }
    mrhiD3d12Label((ID3D12Object*)resource, label, labelLength);
    return resource;
}

static D3D12_RESOURCE_DESC DescribeBuffer(const mrhiBufferDef* def)
{
    uint64_t size = (def->size + CONSTANT_BYTES - 1) & ~(uint64_t)(CONSTANT_BYTES - 1);
    return (D3D12_RESOURCE_DESC){
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Width = size > 0 ? size : CONSTANT_BYTES,
        .Height = 1,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .SampleDesc = {.Count = 1},
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags = (def->usage & mrhi_bufferStorage) != 0 ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
                                                        : D3D12_RESOURCE_FLAG_NONE,
    };
}

mrhiResult mrhiD3d12CreateBuffer(mrhiD3d12Objects* objects, const mrhiBufferDef* def,
                                 uint64_t* handleOut)
{
    *handleOut = 0;
    uint32_t handle = mrhiD3d12TakeSlot(&objects->bufferSlots);
    if (handle == 0)
    {
        return mrhi_errorCapacity;
    }
    D3D12_RESOURCE_DESC desc = DescribeBuffer(def);
    ID3D12Resource* resource = Commit(objects, &desc, def->label, def->labelLength);
    if (resource == nullptr)
    {
        mrhiD3d12GiveSlot(&objects->bufferSlots, handle);
        return mrhi_errorCapacity;
    }
    objects->buffers[handle - 1] = (mrhiD3d12Buffer){.resource = resource, .size = def->size};
    *handleOut = handle;
    return mrhi_success;
}

static bool IsDepth(mrhiFormat format)
{
    return format == mrhi_formatDepth32Float || format == mrhi_formatDepthStencil;
}

static D3D12_RESOURCE_DESC DescribeTexture(const mrhiTextureDef* def)
{
    bool depth = IsDepth(def->format);
    bool views = false;
    for (int i = 0; i < MRHI_VIEW_FORMATS; ++i)
    {
        views = views || def->viewFormats[i] != mrhi_formatNone;
    }
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    if ((def->usage & mrhi_textureRenderTarget) != 0)
    {
        flags |= depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
                       : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if ((def->usage & mrhi_textureStorage) != 0)
    {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    return (D3D12_RESOURCE_DESC){
        .Dimension = def->kind == mrhi_texture3d ? D3D12_RESOURCE_DIMENSION_TEXTURE3D
                                                 : D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .Width = def->width,
        .Height = def->height,
        .DepthOrArraySize = (UINT16)def->depthOrLayers,
        .MipLevels = (UINT16)def->mipLevels,
        .Format = depth || views ? mrhiD3d12Typeless(def->format) : mrhiD3d12Format(def->format),
        .SampleDesc = {.Count = def->sampleCount},
        .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
        .Flags = flags,
    };
}

mrhiResult mrhiD3d12CreateTexture(mrhiD3d12Objects* objects, const mrhiTextureDef* def,
                                  uint64_t* handleOut)
{
    *handleOut = 0;
    uint32_t handle = mrhiD3d12TakeSlot(&objects->textureSlots);
    if (handle == 0)
    {
        return mrhi_errorCapacity;
    }
    D3D12_RESOURCE_DESC desc = DescribeTexture(def);
    ID3D12Resource* resource = Commit(objects, &desc, def->label, def->labelLength);
    if (resource == nullptr)
    {
        mrhiD3d12GiveSlot(&objects->textureSlots, handle);
        return mrhi_errorCapacity;
    }
    mrhiD3d12Texture* texture = &objects->textures[handle - 1];
    *texture = (mrhiD3d12Texture){.resource = resource, .def = *def};
    texture->def.label = nullptr;
    texture->def.labelLength = 0;
    *handleOut = handle;
    return mrhi_success;
}

// A resource placed in a heap at an offset: nullptr when D3D12 makes
// none.
static ID3D12Resource* Place(const mrhiD3d12Objects* objects, ID3D12Heap* heap, uint64_t offset,
                             const D3D12_RESOURCE_DESC* desc, const char* label, size_t labelLength)
{
    ID3D12Resource* resource = nullptr;
    if (FAILED(ID3D12Device_CreatePlacedResource(objects->device, heap, offset, desc,
                                                 D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                 &IID_ID3D12Resource, (void**)&resource)))
    {
        return nullptr;
    }
    mrhiD3d12Label((ID3D12Object*)resource, label, labelLength);
    return resource;
}

ID3D12Resource* mrhiD3d12PlaceBuffer(const mrhiD3d12Objects* objects, ID3D12Heap* heap,
                                     uint64_t offset, const mrhiBufferDef* def)
{
    D3D12_RESOURCE_DESC desc = DescribeBuffer(def);
    return Place(objects, heap, offset, &desc, def->label, def->labelLength);
}

ID3D12Resource* mrhiD3d12PlaceTexture(const mrhiD3d12Objects* objects, ID3D12Heap* heap,
                                      uint64_t offset, const mrhiTextureDef* def)
{
    D3D12_RESOURCE_DESC desc = DescribeTexture(def);
    return Place(objects, heap, offset, &desc, def->label, def->labelLength);
}

ID3D12Resource* mrhiD3d12CommitBuffer(const mrhiD3d12Objects* objects, const mrhiBufferDef* def)
{
    D3D12_RESOURCE_DESC desc = DescribeBuffer(def);
    return Commit(objects, &desc, def->label, def->labelLength);
}

ID3D12Resource* mrhiD3d12CommitTexture(const mrhiD3d12Objects* objects, const mrhiTextureDef* def)
{
    D3D12_RESOURCE_DESC desc = DescribeTexture(def);
    return Commit(objects, &desc, def->label, def->labelLength);
}

mrhiResult mrhiD3d12CreateQuerySet(mrhiD3d12Objects* objects, const mrhiQuerySetDef* def,
                                   uint64_t* handleOut)
{
    MRHI_ASSERT(def->count <= MRHI_D3D12_SET_QUERIES);
    bool occlusion = def->type == mrhi_queryOcclusion;
    *handleOut = 0;
    uint32_t handle = mrhiD3d12TakeSlot(&objects->querySetSlots);
    if (handle == 0)
    {
        return mrhi_errorCapacity;
    }
    D3D12_QUERY_HEAP_DESC desc = {
        .Type = occlusion ? D3D12_QUERY_HEAP_TYPE_OCCLUSION : D3D12_QUERY_HEAP_TYPE_TIMESTAMP,
        .Count = def->count,
    };
    ID3D12QueryHeap* heap = nullptr;
    if (FAILED(ID3D12Device_CreateQueryHeap(objects->device, &desc, &IID_ID3D12QueryHeap,
                                            (void**)&heap)))
    {
        mrhiD3d12GiveSlot(&objects->querySetSlots, handle);
        return mrhi_errorCapacity;
    }
    mrhiD3d12Label((ID3D12Object*)heap, def->label, def->labelLength);
    objects->querySets[handle - 1] = (mrhiD3d12QuerySet){
        .heap = heap,
        .type = occlusion ? D3D12_QUERY_TYPE_BINARY_OCCLUSION : D3D12_QUERY_TYPE_TIMESTAMP,
        .count = def->count,
    };
    *handleOut = handle;
    return mrhi_success;
}

void mrhiD3d12ReleaseObject(mrhiD3d12Objects* objects, mrhiD3d12Kind kind, uint64_t handle)
{
    MRHI_ASSERT(handle != 0);
    switch (kind)
    {
    case mrhiD3d12KindBuffer:
        ID3D12Resource_Release(objects->buffers[handle - 1].resource);
        mrhiD3d12GiveSlot(&objects->bufferSlots, handle);
        break;
    case mrhiD3d12KindTexture:
        ID3D12Resource_Release(objects->textures[handle - 1].resource);
        mrhiD3d12GiveSlot(&objects->textureSlots, handle);
        break;
    case mrhiD3d12KindQuerySet:
        ID3D12QueryHeap_Release(objects->querySets[handle - 1].heap);
        mrhiD3d12GiveSlot(&objects->querySetSlots, handle);
        break;
    case mrhiD3d12KindView:
        mrhiD3d12GiveSlot(&objects->viewSlots, handle);
        break;
    case mrhiD3d12KindHeap:
        mrhiD3d12GiveSlot(&objects->heapSlots, handle);
        break;
    default:
        mrhiD3d12GiveSlot(&objects->samplerSlots, handle);
        break;
    }
}

// What D3D12 says a resource takes in a heap.
static void MemoryOf(const mrhiD3d12Objects* objects, const D3D12_RESOURCE_DESC* desc,
                     uint64_t* bytesOut, uint64_t* alignmentOut)
{
    D3D12_RESOURCE_ALLOCATION_INFO info = {0};
    (void)ID3D12Device_GetResourceAllocationInfo(objects->device, &info, 0, 1, desc);
    *bytesOut = info.SizeInBytes;
    *alignmentOut = info.Alignment;
}

void mrhiD3d12TextureMemory(const mrhiD3d12Objects* objects, const mrhiTextureDef* def,
                            uint64_t* bytesOut, uint64_t* alignmentOut)
{
    D3D12_RESOURCE_DESC desc = DescribeTexture(def);
    MemoryOf(objects, &desc, bytesOut, alignmentOut);
}

void mrhiD3d12BufferMemory(const mrhiD3d12Objects* objects, const mrhiBufferDef* def,
                           uint64_t* bytesOut, uint64_t* alignmentOut)
{
    D3D12_RESOURCE_DESC desc = DescribeBuffer(def);
    MemoryOf(objects, &desc, bytesOut, alignmentOut);
}
