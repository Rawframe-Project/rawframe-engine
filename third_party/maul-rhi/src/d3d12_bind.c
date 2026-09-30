// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's bindings (d3d12_bind.h). A table takes its
// resource descriptors, and its sampler descriptors, together from the
// slot's shader-visible rings, each binding at the offset its pipeline's
// layout gives, and sets them as its descriptor tables. A uniform buffer
// is a constant buffer view, 256 bytes a unit as buffers are made; a
// storage buffer a raw unordered access view, read-only a raw shader
// resource view; a texture a view of the binding's shape; a sampler a
// copy of its descriptor. A table of the samplers the last one wrote
// takes those again. Pipelines of one container share a root signature,
// so their tables and root blocks stay set between them; a pipeline of
// another container needs its tables set again, as on Vulkan. A
// pipeline reading heaps points its heap tables at the pass's heap's
// regions, its shader resource and unordered access tables at the same
// descriptors. Each bind point keeps the buffers its tables use, moved
// at each draw or dispatch to the states they need.

#include "d3d12_bind.h"

#include "d3d12_barrier.h"
#include "invariant.h"

#include <string.h>

// The bind point a pipeline uses: 0 for graphics, 1 for compute.
static uint32_t PointOf(const mrhiD3d12Pipeline* pipeline)
{
    return pipeline->compute ? 1 : 0;
}

static void SetConstants(const mrhiD3d12Recorder* recorder, UINT parameter, UINT words,
                         const void* data, UINT offset)
{
    if (recorder->pipeline->compute)
    {
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(recorder->list, parameter, words,
                                                               data, offset);
    }
    else
    {
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(recorder->list, parameter, words,
                                                                data, offset);
    }
}

static void SetTable(const mrhiD3d12Recorder* recorder, UINT parameter,
                     D3D12_GPU_DESCRIPTOR_HANDLE table)
{
    if (recorder->pipeline->compute)
    {
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(recorder->list, parameter, table);
    }
    else
    {
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(recorder->list, parameter, table);
    }
}

void mrhiD3d12SetPipeline(mrhiD3d12Recorder* recorder, uint64_t handle)
{
    const mrhiD3d12Pipeline* pipeline = &recorder->pipelines->pipelines[handle - 1];
    recorder->pipeline = pipeline;
    ID3D12GraphicsCommandList* list = recorder->list;
    ID3D12GraphicsCommandList_SetPipelineState(list, pipeline->state);
    ID3D12RootSignature** root =
        pipeline->compute ? &recorder->computeRoot : &recorder->graphicsRoot;
    if (*root != pipeline->root)
    {
        *root = pipeline->root;
        if (pipeline->compute)
        {
            ID3D12GraphicsCommandList_SetComputeRootSignature(list, pipeline->root);
        }
        else
        {
            ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, pipeline->root);
        }
        // D3D12 drops the root arguments set before.
        memset(recorder->boundCounts[PointOf(pipeline)], 0,
               sizeof(recorder->boundCounts[PointOf(pipeline)]));
    }
    const mrhiD3d12Layout* layout = &pipeline->layout;
    if (layout->constantsParameter != MRHI_D3D12_NO_PARAMETER)
    {
        SetConstants(recorder, layout->constantsParameter, layout->constantWords,
                     pipeline->constants, 0);
    }
    // The core sets a pipeline reading heaps only in a pass with one.
    const D3D12_GPU_DESCRIPTOR_HANDLE heaps[MRHI_D3D12_HEAP_CLASSES] = {
        recorder->heapViews, recorder->heapViews, recorder->heapSamplerViews};
    for (uint32_t c = 0; c < MRHI_D3D12_HEAP_CLASSES; ++c)
    {
        if (layout->heapParameters[c] != MRHI_D3D12_NO_PARAMETER)
        {
            SetTable(recorder, layout->heapParameters[c], heaps[c]);
        }
    }
    if (!pipeline->compute)
    {
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, pipeline->topology);
        recorder->verticesChanged = true;
    }
}

void mrhiD3d12EnterHeap(mrhiD3d12Recorder* recorder, uint64_t heap)
{
    if (heap == 0)
    {
        return;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE views;
    D3D12_GPU_DESCRIPTOR_HANDLE samplers;
    (void)ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(recorder->views.heap, &views);
    (void)ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(recorder->samplers.heap,
                                                                  &samplers);
    recorder->heapViews.ptr =
        views.ptr + (UINT64)recorder->views.step * (heap - 1) * recorder->heapEntries;
    recorder->heapSamplerViews.ptr =
        samplers.ptr + (UINT64)recorder->samplers.step * (heap - 1) * recorder->heapSamplers;
}

// Takes count descriptors from a ring: false, failing the frame, when
// it has too few.
static bool Take(mrhiD3d12Recorder* recorder, mrhiD3d12GpuRing* ring, uint32_t count,
                 uint32_t* firstOut)
{
    if (ring->capacity - ring->taken < count)
    {
        recorder->status = mrhi_errorCapacity;
        return false;
    }
    *firstOut = ring->taken;
    ring->taken += count;
    return true;
}

static D3D12_CPU_DESCRIPTOR_HANDLE CpuAt(const mrhiD3d12GpuRing* ring, uint32_t index)
{
    return (D3D12_CPU_DESCRIPTOR_HANDLE){.ptr = ring->cpu.ptr + (SIZE_T)ring->step * index};
}

static D3D12_GPU_DESCRIPTOR_HANDLE GpuAt(const mrhiD3d12GpuRing* ring, uint32_t index)
{
    return (D3D12_GPU_DESCRIPTOR_HANDLE){.ptr = ring->gpu.ptr + (UINT64)ring->step * index};
}

// The pipeline's binding of a table's slot.
static const mrhiD3d12Binding* SlotOf(const mrhiD3d12Pipeline* pipeline, uint32_t table,
                                      uint32_t slot)
{
    for (uint32_t i = 0; i < pipeline->bindingCount; ++i)
    {
        const mrhiD3d12Binding* binding = &pipeline->bindings[i];
        if (binding->table == table && binding->slot == slot)
        {
            return binding;
        }
    }
    MRHI_ASSERT(false);
    return nullptr;
}

// Writes a buffer binding's view, and returns the state its use needs.
static D3D12_RESOURCE_STATES WriteBuffer(const mrhiD3d12Recorder* recorder,
                                         const mrhiCommandBinding* binding,
                                         D3D12_CPU_DESCRIPTOR_HANDLE at)
{
    ID3D12Resource* resource = recorder->table[binding->object - 1].resource;
    if (binding->kind == mrhi_bindingUniformBuffer)
    {
        const D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {
            .BufferLocation = ID3D12Resource_GetGPUVirtualAddress(resource) + binding->offset,
            .SizeInBytes =
                (UINT)((binding->size + D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1) &
                       ~(uint64_t)(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1)),
        };
        ID3D12Device_CreateConstantBufferView(recorder->device, &desc, at);
        return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    }
    const D3D12_BUFFER_SRV raw = {
        .FirstElement = binding->offset / sizeof(uint32_t),
        .NumElements = (UINT)(binding->size / sizeof(uint32_t)),
        .Flags = D3D12_BUFFER_SRV_FLAG_RAW,
    };
    if (binding->kind == mrhi_bindingReadOnlyStorageBuffer)
    {
        const D3D12_SHADER_RESOURCE_VIEW_DESC desc = {
            .Format = DXGI_FORMAT_R32_TYPELESS,
            .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Buffer = raw,
        };
        ID3D12Device_CreateShaderResourceView(recorder->device, resource, &desc, at);
        return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    const D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {
        .Format = DXGI_FORMAT_R32_TYPELESS,
        .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
        .Buffer = {.FirstElement = raw.FirstElement,
                   .NumElements = raw.NumElements,
                   .Flags = D3D12_BUFFER_UAV_FLAG_RAW},
    };
    ID3D12Device_CreateUnorderedAccessView(recorder->device, resource, nullptr, &desc, at);
    return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
}

// Writes a texture binding's view of the shape the binding gives.
static void WriteTexture(const mrhiD3d12Recorder* recorder, const mrhiCommandBinding* binding,
                         D3D12_CPU_DESCRIPTOR_HANDLE at)
{
    const mrhiD3d12Object* object = &recorder->table[binding->object - 1];
    MRHI_ASSERT(object->texture != nullptr);
    const mrhiViewDef view = {
        .kind = binding->viewKind,
        .format = binding->viewFormat,
        .aspect = binding->aspect,
        .baseMip = binding->baseMip,
        .mipCount = binding->mipCount,
        .baseLayer = (uint32_t)binding->offset,
        .layerCount = (uint32_t)binding->size,
    };
    if (binding->kind == mrhi_bindingStorageTexture)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc = mrhiD3d12DescribeWrite(&view);
        ID3D12Device_CreateUnorderedAccessView(recorder->device, object->resource, nullptr, &desc,
                                               at);
    }
    else
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc =
            mrhiD3d12DescribeRead(&view, object->texture->sampleCount);
        ID3D12Device_CreateShaderResourceView(recorder->device, object->resource, &desc, at);
    }
}

// Sets a table's samplers, by their handles in the order of its sampler
// descriptors: the last sampler table's descriptors when they are the
// same, else new ones.
static void BindSamplers(mrhiD3d12Recorder* recorder, UINT parameter, const uint64_t* handles,
                         uint32_t count)
{
    bool same = count == recorder->samplerCount &&
                memcmp(handles, recorder->samplerHandles, count * sizeof(uint64_t)) == 0;
    if (!same)
    {
        uint32_t first = 0;
        if (!Take(recorder, &recorder->samplers, count, &first))
        {
            return;
        }
        for (uint32_t i = 0; i < count; ++i)
        {
            ID3D12Device_CopyDescriptorsSimple(
                recorder->device, 1, CpuAt(&recorder->samplers, first + i),
                mrhiD3d12SamplerDescriptor(recorder->objects, handles[i]),
                D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        }
        memcpy(recorder->samplerHandles, handles, count * sizeof(uint64_t));
        recorder->samplerCount = count;
        recorder->samplerRun = GpuAt(&recorder->samplers, first);
    }
    SetTable(recorder, parameter, recorder->samplerRun);
}

void mrhiD3d12BindTable(mrhiD3d12Recorder* recorder, const mrhiCommand* command)
{
    const mrhiD3d12Pipeline* pipeline = recorder->pipeline;
    MRHI_ASSERT(pipeline != nullptr && command->a < MRHI_D3D12_TABLES);
    uint32_t table = command->a;
    const mrhiD3d12Layout* layout = &pipeline->layout;
    uint32_t first = 0;
    if (!Take(recorder, &recorder->views, layout->resourceCounts[table], &first))
    {
        return;
    }
    uint32_t point = PointOf(pipeline);
    mrhiD3d12Bound* bound = recorder->bound[point][table];
    uint32_t boundCount = 0;
    uint64_t samplers[MRHI_TABLE_BINDINGS] = {0};
    uint32_t count = (uint32_t)command->b;
    MRHI_ASSERT(count <= MRHI_TABLE_BINDINGS);
    for (uint32_t i = 0; i < count; ++i)
    {
        mrhiCommandBinding binding;
        memcpy(&binding, &command[1 + i], sizeof(binding));
        const mrhiD3d12Binding* slot = SlotOf(pipeline, table, binding.slot);
        if (binding.kind == mrhi_bindingSampler)
        {
            MRHI_ASSERT(slot->offset < layout->samplerCounts[table]);
            samplers[slot->offset] = binding.offset;
            continue;
        }
        MRHI_ASSERT(slot->offset < layout->resourceCounts[table]);
        D3D12_CPU_DESCRIPTOR_HANDLE at = CpuAt(&recorder->views, first + slot->offset);
        if (recorder->table[binding.object - 1].texture != nullptr)
        {
            WriteTexture(recorder, &binding, at);
        }
        else
        {
            bound[boundCount++] = (mrhiD3d12Bound){.object = binding.object,
                                                   .state = WriteBuffer(recorder, &binding, at)};
        }
    }
    recorder->boundCounts[point][table] = boundCount;
    if (layout->resourceParameters[table] != MRHI_D3D12_NO_PARAMETER)
    {
        SetTable(recorder, layout->resourceParameters[table], GpuAt(&recorder->views, first));
    }
    if (layout->samplerParameters[table] != MRHI_D3D12_NO_PARAMETER)
    {
        BindSamplers(recorder, layout->samplerParameters[table], samplers,
                     layout->samplerCounts[table]);
    }
}

void mrhiD3d12SetRootBlock(const mrhiD3d12Recorder* recorder, const mrhiCommand* command)
{
    const mrhiD3d12Pipeline* pipeline = recorder->pipeline;
    MRHI_ASSERT(pipeline != nullptr && pipeline->layout.rootParameter != MRHI_D3D12_NO_PARAMETER);
    SetConstants(recorder, pipeline->layout.rootParameter, (UINT)(command->b / sizeof(uint32_t)),
                 &command[1], command->a / (UINT)sizeof(uint32_t));
}

void mrhiD3d12BindBuffer(mrhiD3d12Recorder* recorder, const mrhiCommand* command)
{
    uint32_t object = (uint32_t)command->b;
    MRHI_ASSERT(object != 0 && object <= recorder->frame->resourceCount);
    if (command->type == mrhiCommandIndexBuffer)
    {
        ID3D12Resource* resource = recorder->table[object - 1].resource;
        const D3D12_INDEX_BUFFER_VIEW view = {
            .BufferLocation = ID3D12Resource_GetGPUVirtualAddress(resource) + command->c,
            .SizeInBytes = (UINT)command->d,
            .Format = command->a == mrhi_indexUint16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT,
        };
        ID3D12GraphicsCommandList_IASetIndexBuffer(recorder->list, &view);
        recorder->indexObject = object;
        return;
    }
    MRHI_ASSERT(command->a < MRHI_D3D12_VERTEX_BUFFERS);
    recorder->vertices[command->a] =
        (mrhiD3d12Vertices){.object = object, .offset = command->c, .size = command->d};
    recorder->verticesChanged = true;
}

// Sets the vertex buffers set, up to the last, with the pipeline's
// strides.
static void SetVertices(mrhiD3d12Recorder* recorder)
{
    D3D12_VERTEX_BUFFER_VIEW views[MRHI_D3D12_VERTEX_BUFFERS] = {{0}};
    UINT count = 0;
    for (UINT i = 0; i < MRHI_D3D12_VERTEX_BUFFERS; ++i)
    {
        const mrhiD3d12Vertices* vertices = &recorder->vertices[i];
        if (vertices->object == 0)
        {
            continue;
        }
        ID3D12Resource* resource = recorder->table[vertices->object - 1].resource;
        views[i] = (D3D12_VERTEX_BUFFER_VIEW){
            .BufferLocation = ID3D12Resource_GetGPUVirtualAddress(resource) + vertices->offset,
            .SizeInBytes = (UINT)vertices->size,
            .StrideInBytes = recorder->pipeline->strides[i],
        };
        count = i + 1;
    }
    if (count > 0)
    {
        ID3D12GraphicsCommandList_IASetVertexBuffers(recorder->list, 0, count, views);
    }
    recorder->verticesChanged = false;
}

void mrhiD3d12Ready(mrhiD3d12Recorder* recorder)
{
    const mrhiD3d12Pipeline* pipeline = recorder->pipeline;
    MRHI_ASSERT(pipeline != nullptr);
    uint32_t point = PointOf(pipeline);
    for (uint32_t t = 0; t < MRHI_D3D12_TABLES; ++t)
    {
        for (uint32_t i = 0; i < recorder->boundCounts[point][t]; ++i)
        {
            const mrhiD3d12Bound* bound = &recorder->bound[point][t][i];
            mrhiD3d12Use(recorder, bound->object, bound->state);
        }
    }
    if (!pipeline->compute)
    {
        if (recorder->indexObject != 0)
        {
            mrhiD3d12Use(recorder, recorder->indexObject, D3D12_RESOURCE_STATE_INDEX_BUFFER);
        }
        for (uint32_t i = 0; i < MRHI_D3D12_VERTEX_BUFFERS; ++i)
        {
            if (recorder->vertices[i].object != 0)
            {
                mrhiD3d12Use(recorder, recorder->vertices[i].object,
                             D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
            }
        }
        if (recorder->verticesChanged)
        {
            SetVertices(recorder);
        }
    }
    mrhiD3d12FlushBarriers(recorder);
}
