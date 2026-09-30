// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's shaders and pipelines (mrhi-0003). A shader holds
// each entry's DXIL, copied from the container, and the root signature
// its bindings make, which every pipeline of the shader shares; a
// pipeline holds its state, a reference on the root signature, and what
// its frames bind, copied, so that it outlives its shader. Pipelines are
// made at the call, with no thread, and answered at the next poll.
// Included by the driver's files only.

#ifndef MAUL_RHI_SRC_D3D12_PIPELINE_H
#define MAUL_RHI_SRC_D3D12_PIPELINE_H

#include "allocator.h"
#include "d3d12_api.h"
#include "d3d12_root.h"
#include "d3d12_slots.h"
#include "driver.h"

// A shader: its root signature and layout, and in one block each entry's
// code and whether it reads the vertex information, its bindings, and
// its constants' default bits and whether each is fixed.
typedef struct mrhiD3d12Shader
{
    ID3D12RootSignature* root;
    mrhiD3d12Layout layout;
    size_t bytes;
    unsigned char* block;
    uint32_t entryCount;
    uint32_t bindingCount;
    uint32_t constantCount;
    const D3D12_SHADER_BYTECODE* code;
    const bool* vertexInfo;
    const mrhiD3d12Binding* bindings;
    const uint32_t* defaults;
    const bool* fixed;
} mrhiD3d12Shader;

// A pipeline: its state, its root signature and layout, whether it
// computes, and for frames the topology, each vertex buffer's stride,
// whether its vertex entry reads the vertex information, with the
// command signatures of its indirect draws that set it, the root
// block's bytes, and in one block its bindings and constants' bits.
typedef struct mrhiD3d12Pipeline
{
    ID3D12PipelineState* state;
    ID3D12RootSignature* root;
    ID3D12CommandSignature* draw;
    ID3D12CommandSignature* drawIndexed;
    mrhiD3d12Layout layout;
    bool compute;
    bool vertexInfo;
    D3D_PRIMITIVE_TOPOLOGY topology;
    uint32_t strides[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    uint32_t rootBytes;
    size_t bytes;
    unsigned char* block;
    uint32_t bindingCount;
    const mrhiD3d12Binding* bindings;
    uint32_t* constants;
} mrhiD3d12Pipeline;

// A device's shaders and pipelines, in tables in its block, and the
// answers not yet polled, at most one per pipeline.
typedef struct mrhiD3d12Pipelines
{
    const mrhiAllocator* allocator;
    const mrhiD3d12Api* api;
    ID3D12Device* device;
    mrhiD3d12Shader* shaders;
    mrhiD3d12Pipeline* pipelines;
    mrhiD3d12Slots shaderSlots;
    mrhiD3d12Slots pipelineSlots;
    mrhiDriverEvent* pending;
    uint64_t* pendingHandles;
    uint32_t pendingCount;
} mrhiD3d12Pipelines;

// Where the tables lie in a device's block.
typedef struct mrhiD3d12PipelineRoom
{
    size_t shaders;
    size_t pipelines;
    size_t pending;
    size_t pendingHandles;
    size_t slots;
} mrhiD3d12PipelineRoom;

// Adds the tables a device's limits need to its layout, and sets them
// up in its block.
mrhiD3d12PipelineRoom mrhiD3d12PlanPipelines(mrhiLayout* layout, const mrhiDeviceLimits* limits);
void mrhiD3d12LayPipelines(mrhiD3d12Pipelines* pipelines, unsigned char* block,
                           const mrhiD3d12PipelineRoom* room, const mrhiDeviceLimits* limits);

// Makes a shader: success with its handle, mrhi_errorUnsupported for a
// container without DXIL or whose root signature passes D3D12's 64
// words, mrhi_errorCapacity when the table or allocator fails, or
// mrhi_errorPlatform when D3D12 refuses the root signature.
mrhiResult mrhiD3d12CreateShader(mrhiD3d12Pipelines* pipelines, const mrhiShaderDef* def,
                                 const mrhiContainer* container, uint64_t* handleOut);
void mrhiD3d12DestroyShader(mrhiD3d12Pipelines* pipelines, uint64_t handle);

// Make pipelines answered at the next poll: success with the handle,
// mrhi_errorUnsupported for a fixed constant given another value,
// mrhi_errorCapacity when the table or allocator fails, or
// mrhi_errorPlatform when D3D12 refuses.
mrhiResult mrhiD3d12CreateCompute(mrhiD3d12Pipelines* pipelines,
                                  const mrhiDriverComputePipeline* pipeline, uint64_t tag,
                                  uint64_t* handleOut);
mrhiResult mrhiD3d12CreateGraphics(mrhiD3d12Pipelines* pipelines,
                                   const mrhiDriverGraphicsPipeline* pipeline, uint64_t tag,
                                   uint64_t* handleOut);

// Drops a destroyed pipeline's answer, if it has not been polled yet, so
// that it is never given.
void mrhiD3d12ForgetPipeline(mrhiD3d12Pipelines* pipelines, uint64_t handle);

// Frees a destroyed pipeline once no frame can name it.
void mrhiD3d12ReleasePipeline(mrhiD3d12Pipelines* pipelines, uint64_t handle);

// Moves up to capacity answers into events and returns how many.
size_t mrhiD3d12PollPipelines(mrhiD3d12Pipelines* pipelines, mrhiDriverEvent* events,
                              size_t capacity);

#endif // MAUL_RHI_SRC_D3D12_PIPELINE_H
