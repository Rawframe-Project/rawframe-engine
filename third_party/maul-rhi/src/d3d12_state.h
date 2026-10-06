// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The state of a D3D12 frame's recording (mrhi-0003): its command list,
// the frame's objects by slot with each buffer's D3D12 state, the
// staging and readback buffers copies name as object 0, the descriptor
// rings the pass's targets and bindings take, and what the pass has
// set. Included by the driver's files only.

#ifndef MAUL_RHI_SRC_D3D12_STATE_H
#define MAUL_RHI_SRC_D3D12_STATE_H

#include "d3d12_pipeline.h"
#include "d3d12_resource.h"
#include "driver.h"

#include "maul-rhi/shader.h"

// The resource barriers gathered before they are recorded together.
#define MRHI_D3D12_BARRIERS 64
// The vertex buffers a pipeline reads at most.
#define MRHI_D3D12_VERTEX_BUFFERS D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT

// A frame's object: its resource, whether it is a texture (with its def)
// or a buffer, and a buffer's D3D12 state. Every buffer starts a frame
// in the common state, since D3D12 decays buffers to it when a command
// list finishes. A texture's undefined parts are in the initial state;
// a placed target is discarded at its first use, which D3D12 asks of
// placed render target and depth textures before anything else.
// The bytes of a device's zeros, which resolves and clears copy in
// pieces (mrhi-0022, mrhi-0023): D3D12's placement alignment, so no
// smaller buffer takes less memory.
#define MRHI_D3D12_ZERO_BYTES UINT64_C(65536)

typedef struct mrhiD3d12Object
{
    ID3D12Resource* resource;
    const mrhiTextureDef* texture;
    D3D12_RESOURCE_STATES state;
    D3D12_RESOURCE_STATES initial;
    bool discard;
} mrhiD3d12Object;

// A range of the readback ring a frame fills.
typedef struct mrhiD3d12Range
{
    uint64_t offset;
    uint64_t size;
} mrhiD3d12Range;

// A CPU-only descriptor ring of a frame slot: its heap's start, step,
// the descriptors it holds and those taken.
typedef struct mrhiD3d12Ring
{
    D3D12_CPU_DESCRIPTOR_HANDLE start;
    UINT step;
    uint32_t capacity;
    uint32_t taken;
} mrhiD3d12Ring;

// A shader-visible descriptor ring of a frame slot: its heap, the heap's
// starts on the CPU and the GPU, its step, the descriptors it holds and
// those taken.
typedef struct mrhiD3d12GpuRing
{
    ID3D12DescriptorHeap* heap;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu;
    UINT step;
    uint32_t capacity;
    uint32_t taken;
} mrhiD3d12GpuRing;

// A buffer the bound tables use, by frame slot plus one, and the state
// the use needs.
typedef struct mrhiD3d12Bound
{
    uint32_t object;
    D3D12_RESOURCE_STATES state;
} mrhiD3d12Bound;

// A vertex buffer set, by frame slot plus one (0 for none), with its
// offset and bytes.
typedef struct mrhiD3d12Vertices
{
    uint32_t object;
    uint64_t offset;
    uint64_t size;
} mrhiD3d12Vertices;

// The command signatures of indirect draws and dispatches without the
// vertex information.
typedef enum mrhiD3d12Indirect
{
    mrhiD3d12IndirectDraw,
    mrhiD3d12IndirectDrawIndexed,
    mrhiD3d12IndirectDispatch,
    mrhiD3d12IndirectCount,
} mrhiD3d12Indirect;

typedef struct mrhiD3d12Recorder
{
    ID3D12Device* device;
    ID3D12GraphicsCommandList* list;
    // The device's objects; its query sets keep the queries the frame
    // writes.
    mrhiD3d12Objects* objects;
    const mrhiD3d12Pipelines* pipelines;
    const mrhiDriverFrame* frame;
    const mrhiDriverPass* pass;
    // The frame's serial, whose queries' written bits the sets keep.
    uint64_t serial;
    // The frame's objects by slot less one; a null resource for one no
    // pass uses.
    mrhiD3d12Object* table;
    ID3D12Resource* staging;
    ID3D12Resource* readback;
    mrhiD3d12Ring targets;
    mrhiD3d12Ring depths;
    mrhiD3d12GpuRing views;
    mrhiD3d12GpuRing samplers;
    // Each program heap's resource and sampler descriptors, and the
    // regions of the pass's heap, which heap tables point at.
    uint32_t heapEntries;
    uint32_t heapSamplers;
    D3D12_GPU_DESCRIPTOR_HANDLE heapViews;
    D3D12_GPU_DESCRIPTOR_HANDLE heapSamplerViews;
    D3D12_RESOURCE_BARRIER barriers[MRHI_D3D12_BARRIERS];
    uint32_t barrierCount;
    // The frame's next barrier to record.
    size_t barrierAt;
    uint32_t labelCount;
    // The ranges of the readback ring the frame fills.
    mrhiD3d12Range* readbacks;
    uint32_t readbackCount;
    uint32_t readbackLimit;
    // The pipeline set, and the root signatures set for each bind point.
    const mrhiD3d12Pipeline* pipeline;
    ID3D12RootSignature* graphicsRoot;
    ID3D12RootSignature* computeRoot;
    // The buffers each table bound for graphics (0) and compute (1) uses.
    mrhiD3d12Bound bound[2][MRHI_D3D12_TABLES][MRHI_TABLE_BINDINGS];
    uint32_t boundCounts[2][MRHI_D3D12_TABLES];
    // The last sampler table written, which a table of the same samplers
    // takes again, since a slot has few sampler descriptors.
    D3D12_GPU_DESCRIPTOR_HANDLE samplerRun;
    uint64_t samplerHandles[MRHI_TABLE_BINDINGS];
    uint32_t samplerCount;
    // The vertex buffers set, set on the list at the next draw since
    // their strides are the pipeline's, and the index buffer.
    mrhiD3d12Vertices vertices[MRHI_D3D12_VERTEX_BUFFERS];
    bool verticesChanged;
    uint32_t indexObject;
    // The query the pass has open.
    uint32_t openQuery;
    ID3D12CommandSignature* const* signatures;
    // The zeros a resolve copies for queries the frame did not write,
    // and a clear copies (mrhi-0022), MRHI_D3D12_ZERO_BYTES of them.
    ID3D12Resource* zeros;
    // The slot's buffer that indirect draws setting the vertex
    // information copy their arguments into, made at its first use, and
    // its state.
    ID3D12Resource** scratch;
    uint64_t scratchBytes;
    uint64_t scratchUsed;
    D3D12_RESOURCE_STATES scratchState;
    // The kernel expanding counted draws' records (mrhi-0020), on a
    // device that draws them.
    ID3D12RootSignature* expandRoot;
    ID3D12PipelineState* expand;
    // mrhi_errorCapacity once a descriptor ring or the scratch buffer
    // runs out, which fails the frame.
    mrhiResult status;
} mrhiD3d12Recorder;

#endif // MAUL_RHI_SRC_D3D12_STATE_H
