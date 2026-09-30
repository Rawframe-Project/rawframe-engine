// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's objects (mrhi-0003): buffers, textures and query
// sets, each a D3D12 object in its table's slot, and views and
// samplers, whose slots are also their descriptors' in CPU-only
// descriptor heaps, copied to the GPU's heaps when bound. Included by the driver's files only.

#ifndef MAUL_RHI_SRC_D3D12_RESOURCE_H
#define MAUL_RHI_SRC_D3D12_RESOURCE_H

#include "allocator.h"
#include "d3d12_api.h"
#include "d3d12_slots.h"
#include "driver.h"

typedef struct mrhiD3d12Buffer
{
    ID3D12Resource* resource;
    uint64_t size;
} mrhiD3d12Buffer;

// A texture and its def, without its label.
typedef struct mrhiD3d12Texture
{
    ID3D12Resource* resource;
    mrhiTextureDef def;
} mrhiD3d12Texture;

// A view: its texture's handle, its resolved def without its label,
// and whether its slot holds a shader resource view and an unordered
// access view.
typedef struct mrhiD3d12View
{
    uint64_t texture;
    mrhiViewDef def;
    bool read;
    bool write;
} mrhiD3d12View;

// The most queries a set has (mrhi-0012), and the words of a set's
// bits.
#define MRHI_D3D12_SET_QUERIES 4096
#define MRHI_D3D12_SET_WORDS   (MRHI_D3D12_SET_QUERIES / 64)

// A query set: its heap, the type its queries resolve as, its queries,
// and the queries the frame of a serial has written so far as it
// records.
typedef struct mrhiD3d12QuerySet
{
    ID3D12QueryHeap* heap;
    D3D12_QUERY_TYPE type;
    uint32_t count;
    uint64_t serial;
    uint64_t written[MRHI_D3D12_SET_WORDS];
} mrhiD3d12QuerySet;

// What a handle names, for its release.
typedef enum mrhiD3d12Kind
{
    mrhiD3d12KindBuffer,
    mrhiD3d12KindTexture,
    mrhiD3d12KindView,
    mrhiD3d12KindSampler,
    mrhiD3d12KindQuerySet,
    // A heap (d3d12_heap.h), only its slot, which is its region.
    mrhiD3d12KindHeap,
    // A pipeline (d3d12_pipeline.h), which the frames retire too.
    mrhiD3d12KindPipeline,
} mrhiD3d12Kind;

// A device's objects: tables of each kind in the device's block, and
// the descriptor heaps of views and samplers, two descriptors a view
// slot (its shader resource view, then its unordered access view) and
// one a sampler slot. A sampler is only its descriptor.
typedef struct mrhiD3d12Objects
{
    ID3D12Device* device;
    ID3D12DescriptorHeap* viewHeap;
    ID3D12DescriptorHeap* samplerHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE viewStart;
    D3D12_CPU_DESCRIPTOR_HANDLE samplerStart;
    UINT viewStep;
    UINT samplerStep;
    mrhiD3d12Buffer* buffers;
    mrhiD3d12Texture* textures;
    mrhiD3d12View* views;
    mrhiD3d12QuerySet* querySets;
    mrhiD3d12Slots bufferSlots;
    mrhiD3d12Slots textureSlots;
    mrhiD3d12Slots viewSlots;
    mrhiD3d12Slots samplerSlots;
    mrhiD3d12Slots querySetSlots;
    mrhiD3d12Slots heapSlots;
} mrhiD3d12Objects;

// Where the tables lie in a device's block.
typedef struct mrhiD3d12ObjectRoom
{
    size_t buffers;
    size_t textures;
    size_t views;
    size_t querySets;
    size_t slots;
} mrhiD3d12ObjectRoom;

// Adds the tables a device's limits need to its layout, and sets them
// up in its block, every slot free.
mrhiD3d12ObjectRoom mrhiD3d12PlanObjects(mrhiLayout* layout, const mrhiDeviceLimits* limits);
void mrhiD3d12LayObjects(mrhiD3d12Objects* objects, unsigned char* block,
                         const mrhiD3d12ObjectRoom* room, const mrhiDeviceLimits* limits);

// Makes the descriptor heaps of the laid out tables: success, or
// mrhi_errorCapacity when D3D12 makes none.
mrhiResult mrhiD3d12OpenObjects(mrhiD3d12Objects* objects);
void mrhiD3d12CloseObjects(mrhiD3d12Objects* objects);

// Each maker answers success with the handle, never zero, or
// mrhi_errorCapacity when D3D12 or the table makes nothing.
mrhiResult mrhiD3d12CreateBuffer(mrhiD3d12Objects* objects, const mrhiBufferDef* def,
                                 uint64_t* handleOut);
mrhiResult mrhiD3d12CreateTexture(mrhiD3d12Objects* objects, const mrhiTextureDef* def,
                                  uint64_t* handleOut);
mrhiResult mrhiD3d12CreateView(mrhiD3d12Objects* objects, uint64_t texture, const mrhiViewDef* def,
                               uint64_t* handleOut);
mrhiResult mrhiD3d12CreateSampler(mrhiD3d12Objects* objects, const mrhiSamplerDef* def,
                                  uint64_t* handleOut);
mrhiResult mrhiD3d12CreateQuerySet(mrhiD3d12Objects* objects, const mrhiQuerySetDef* def,
                                   uint64_t* handleOut);
// A frame's transient, committed, or placed in a heap at an offset, in
// the common state without a table slot: nullptr when D3D12 makes none.
ID3D12Resource* mrhiD3d12CommitBuffer(const mrhiD3d12Objects* objects, const mrhiBufferDef* def);
ID3D12Resource* mrhiD3d12CommitTexture(const mrhiD3d12Objects* objects, const mrhiTextureDef* def);
ID3D12Resource* mrhiD3d12PlaceBuffer(const mrhiD3d12Objects* objects, ID3D12Heap* heap,
                                     uint64_t offset, const mrhiBufferDef* def);
ID3D12Resource* mrhiD3d12PlaceTexture(const mrhiD3d12Objects* objects, ID3D12Heap* heap,
                                      uint64_t offset, const mrhiTextureDef* def);
void mrhiD3d12ReleaseObject(mrhiD3d12Objects* objects, mrhiD3d12Kind kind, uint64_t handle);

// The CPU descriptors of a view (its shader resource view, or its
// unordered access view) and of a sampler.
D3D12_CPU_DESCRIPTOR_HANDLE mrhiD3d12ViewDescriptor(const mrhiD3d12Objects* objects, uint64_t view,
                                                    bool write);
D3D12_CPU_DESCRIPTOR_HANDLE mrhiD3d12SamplerDescriptor(const mrhiD3d12Objects* objects,
                                                       uint64_t sampler);

// A texture view's shader resource view, on a texture of that many
// samples, and its unordered access view.
D3D12_SHADER_RESOURCE_VIEW_DESC mrhiD3d12DescribeRead(const mrhiViewDef* def, uint32_t samples);
D3D12_UNORDERED_ACCESS_VIEW_DESC mrhiD3d12DescribeWrite(const mrhiViewDef* def);

// The bytes and alignment a texture or buffer takes in a D3D12 heap.
void mrhiD3d12TextureMemory(const mrhiD3d12Objects* objects, const mrhiTextureDef* def,
                            uint64_t* bytesOut, uint64_t* alignmentOut);
void mrhiD3d12BufferMemory(const mrhiD3d12Objects* objects, const mrhiBufferDef* def,
                           uint64_t* bytesOut, uint64_t* alignmentOut);

#endif // MAUL_RHI_SRC_D3D12_RESOURCE_H
