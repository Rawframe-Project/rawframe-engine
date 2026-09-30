// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's shaders and pipelines (d3d12_pipeline.h). A
// pipeline's constants are the words of the constants' root constants:
// each constant's bits in the container's order, its default where the
// pipeline gives none, padded to rows of four. A fixed constant was
// compiled with its default, so a pipeline giving it other bits is
// refused.

#include "d3d12_pipeline.h"

#include "container.h"
#include "d3d12_graphics.h"
#include "d3d12_names.h"
#include "invariant.h"
#include "reflection.h"

#include <stdalign.h>
#include <stddef.h>
#include <string.h>

mrhiD3d12PipelineRoom mrhiD3d12PlanPipelines(mrhiLayout* layout, const mrhiDeviceLimits* limits)
{
    uint64_t slots = (uint64_t)limits->shaders + limits->pipelines;
    return (mrhiD3d12PipelineRoom){
        .shaders = mrhiLayoutAdd(layout, limits->shaders, sizeof(mrhiD3d12Shader),
                                 alignof(mrhiD3d12Shader)),
        .pipelines = mrhiLayoutAdd(layout, limits->pipelines, sizeof(mrhiD3d12Pipeline),
                                   alignof(mrhiD3d12Pipeline)),
        .pending = mrhiLayoutAdd(layout, limits->pipelines, sizeof(mrhiDriverEvent),
                                 alignof(mrhiDriverEvent)),
        .pendingHandles =
            mrhiLayoutAdd(layout, limits->pipelines, sizeof(uint64_t), alignof(uint64_t)),
        .slots = mrhiLayoutAdd(layout, (size_t)slots, sizeof(uint32_t), alignof(uint32_t)),
    };
}

void mrhiD3d12LayPipelines(mrhiD3d12Pipelines* pipelines, unsigned char* block,
                           const mrhiD3d12PipelineRoom* room, const mrhiDeviceLimits* limits)
{
    pipelines->shaders = (mrhiD3d12Shader*)(block + room->shaders);
    pipelines->pipelines = (mrhiD3d12Pipeline*)(block + room->pipelines);
    pipelines->pending = (mrhiDriverEvent*)(block + room->pending);
    pipelines->pendingHandles = (uint64_t*)(block + room->pendingHandles);
    uint32_t* next = (uint32_t*)(block + room->slots);
    next = mrhiD3d12InitSlots(&pipelines->shaderSlots, next, limits->shaders);
    (void)mrhiD3d12InitSlots(&pipelines->pipelineSlots, next, limits->pipelines);
}

// Where a shader's parts lie in its block.
typedef struct ShaderRoom
{
    mrhiLayout layout;
    size_t code;
    size_t vertexInfo;
    size_t bindings;
    size_t defaults;
    size_t fixed;
    size_t dxil;
} ShaderRoom;

static ShaderRoom ShaderRoomOf(const mrhiContainer* container)
{
    ShaderRoom room = {0};
    mrhiLayout* layout = &room.layout;
    uint32_t entries = container->entryCount;
    room.code = mrhiLayoutAdd(layout, entries, sizeof(D3D12_SHADER_BYTECODE),
                              alignof(D3D12_SHADER_BYTECODE));
    room.vertexInfo = mrhiLayoutAdd(layout, entries, sizeof(bool), alignof(bool));
    room.bindings = mrhiLayoutAdd(layout, container->bindingCount, sizeof(mrhiD3d12Binding),
                                  alignof(mrhiD3d12Binding));
    room.defaults =
        mrhiLayoutAdd(layout, container->constantCount, sizeof(uint32_t), alignof(uint32_t));
    room.fixed = mrhiLayoutAdd(layout, container->constantCount, sizeof(bool), alignof(bool));
    room.dxil = mrhiLayoutAdd(layout, container->dxilBytes, 1, alignof(uint32_t));
    return room;
}

// Copies the container's DXIL, each entry's code, and its constants'
// defaults and fixed flags into a shader's block.
static void FillShader(mrhiD3d12Shader* shader, const ShaderRoom* room,
                       const mrhiContainer* container)
{
    unsigned char* block = shader->block;
    unsigned char* dxil = block + room->dxil;
    memcpy(dxil, container->dxil, container->dxilBytes);
    D3D12_SHADER_BYTECODE* code = (D3D12_SHADER_BYTECODE*)(block + room->code);
    bool* vertexInfo = (bool*)(block + room->vertexInfo);
    for (uint32_t i = 0; i < container->entryCount; ++i)
    {
        mrhiD3d12Entry entry = mrhiContainerD3d12Entry(container, i);
        code[i] = (D3D12_SHADER_BYTECODE){.pShaderBytecode = dxil + entry.dxilOffset,
                                          .BytecodeLength = entry.dxilLength};
        vertexInfo[i] = entry.vertexInfo;
    }
    uint32_t* defaults = (uint32_t*)(block + room->defaults);
    bool* fixed = (bool*)(block + room->fixed);
    for (uint32_t i = 0; i < container->constantCount; ++i)
    {
        defaults[i] = mrhiContainerConstant(container, i).bits;
        fixed[i] = mrhiContainerD3d12Fixed(container, i);
    }
    shader->code = code;
    shader->vertexInfo = vertexInfo;
    shader->defaults = defaults;
    shader->fixed = fixed;
}

mrhiResult mrhiD3d12CreateShader(mrhiD3d12Pipelines* pipelines, const mrhiShaderDef* def,
                                 const mrhiContainer* container, uint64_t* handleOut)
{
    *handleOut = 0;
    if (container->d3d12Map == nullptr)
    {
        return mrhi_errorUnsupported;
    }
    ShaderRoom room = ShaderRoomOf(container);
    unsigned char* block =
        room.layout.overflow
            ? nullptr
            : mrhiAllocate(pipelines->allocator, room.layout.size, alignof(max_align_t));
    uint32_t handle = block != nullptr ? mrhiD3d12TakeSlot(&pipelines->shaderSlots) : 0;
    if (handle == 0)
    {
        if (block != nullptr)
        {
            mrhiRelease(pipelines->allocator, block, room.layout.size, alignof(max_align_t));
        }
        return mrhi_errorCapacity;
    }
    mrhiD3d12Shader* shader = &pipelines->shaders[handle - 1];
    *shader = (mrhiD3d12Shader){
        .bytes = room.layout.size,
        .block = block,
        .entryCount = container->entryCount,
        .bindingCount = container->bindingCount,
        .constantCount = container->constantCount,
        .bindings = (const mrhiD3d12Binding*)(block + room.bindings),
    };
    FillShader(shader, &room, container);
    mrhiResult status = mrhiD3d12MakeRoot(
        pipelines->allocator, pipelines->api, pipelines->device, container, &shader->layout,
        (mrhiD3d12Binding*)(block + room.bindings), &shader->root);
    if (status != mrhi_success)
    {
        mrhiRelease(pipelines->allocator, block, room.layout.size, alignof(max_align_t));
        mrhiD3d12GiveSlot(&pipelines->shaderSlots, handle);
        return status;
    }
    mrhiD3d12Label((ID3D12Object*)shader->root, def->label, def->labelLength);
    *handleOut = handle;
    return mrhi_success;
}

void mrhiD3d12DestroyShader(mrhiD3d12Pipelines* pipelines, uint64_t handle)
{
    mrhiD3d12Shader* shader = &pipelines->shaders[handle - 1];
    ID3D12RootSignature_Release(shader->root);
    mrhiRelease(pipelines->allocator, shader->block, shader->bytes, alignof(max_align_t));
    mrhiD3d12GiveSlot(&pipelines->shaderSlots, handle);
}

// A constant's value in its type's 32 bits, as the core checked it.
static uint32_t BitsOf(mrhiConstantType type, double value)
{
    switch (type)
    {
    case mrhi_constantBool:
        return value != 0.0 ? 1u : 0u;
    case mrhi_constantInt32:
        return (uint32_t)(int32_t)value;
    case mrhi_constantUint32:
        return (uint32_t)value;
    default:
    {
        float single = (float)value;
        uint32_t bits = 0;
        memcpy(&bits, &single, sizeof(bits));
        return bits;
    }
    }
}

// Fills a pipeline's constant words from the shader's defaults and the
// values given: false when a fixed constant is given other bits.
static bool Specialize(const mrhiD3d12Shader* shader, const mrhiReflection* reflection,
                       const mrhiConstantValue* values, uint32_t count, uint32_t* words)
{
    if (shader->layout.constantWords > 0)
    {
        memset(words, 0, shader->layout.constantWords * sizeof(uint32_t));
        memcpy(words, shader->defaults, shader->constantCount * sizeof(uint32_t));
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t c = 0;
        while (c < reflection->constantCount && reflection->constants[c].id != values[i].id)
        {
            ++c;
        }
        MRHI_ASSERT(c < reflection->constantCount && c < shader->constantCount);
        uint32_t bits = BitsOf(reflection->constants[c].type, values[i].value);
        if (shader->fixed[c] && bits != shader->defaults[c])
        {
            return false;
        }
        words[c] = bits;
    }
    return true;
}

// Takes a pipeline slot and a block holding a copy of the shader's
// bindings and the pipeline's constant words: the slot's handle, or 0
// when either runs out.
static uint32_t NewPipeline(mrhiD3d12Pipelines* pipelines, const mrhiD3d12Shader* shader,
                            bool compute)
{
    size_t bindingBytes = shader->bindingCount * sizeof(mrhiD3d12Binding);
    size_t bytes = bindingBytes + shader->layout.constantWords * sizeof(uint32_t);
    unsigned char* block =
        bytes > 0 ? mrhiAllocate(pipelines->allocator, bytes, alignof(mrhiD3d12Binding)) : nullptr;
    uint32_t handle =
        bytes == 0 || block != nullptr ? mrhiD3d12TakeSlot(&pipelines->pipelineSlots) : 0;
    if (handle == 0)
    {
        if (block != nullptr)
        {
            mrhiRelease(pipelines->allocator, block, bytes, alignof(mrhiD3d12Binding));
        }
        return 0;
    }
    if (bindingBytes > 0 && block != nullptr)
    {
        memcpy(block, shader->bindings, bindingBytes);
    }
    ID3D12RootSignature_AddRef(shader->root);
    pipelines->pipelines[handle - 1] = (mrhiD3d12Pipeline){
        .root = shader->root,
        .layout = shader->layout,
        .compute = compute,
        .bytes = bytes,
        .block = block,
        .bindingCount = shader->bindingCount,
        .bindings = (const mrhiD3d12Binding*)block,
        .constants = block != nullptr ? (uint32_t*)(block + bindingBytes) : nullptr,
    };
    return handle;
}

static void FreePipeline(mrhiD3d12Pipelines* pipelines, uint64_t handle)
{
    mrhiD3d12Pipeline* pipeline = &pipelines->pipelines[handle - 1];
    if (pipeline->state != nullptr)
    {
        ID3D12PipelineState_Release(pipeline->state);
    }
    ID3D12RootSignature_Release(pipeline->root);
    if (pipeline->draw != nullptr)
    {
        ID3D12CommandSignature_Release(pipeline->draw);
    }
    if (pipeline->drawIndexed != nullptr)
    {
        ID3D12CommandSignature_Release(pipeline->drawIndexed);
    }
    if (pipeline->bytes > 0)
    {
        mrhiRelease(pipelines->allocator, pipeline->block, pipeline->bytes,
                    alignof(mrhiD3d12Binding));
    }
    mrhiD3d12GiveSlot(&pipelines->pipelineSlots, handle);
}

// Queues a made pipeline's answer for the next poll: one per pipeline
// the device holds at most, so the queue never fills.
static void Answer(mrhiD3d12Pipelines* pipelines, uint32_t handle, uint64_t tag,
                   uint64_t* handleOut)
{
    MRHI_ASSERT(pipelines->pendingCount < pipelines->pipelineSlots.capacity);
    pipelines->pending[pipelines->pendingCount] =
        (mrhiDriverEvent){.tag = tag, .outcome = mrhi_success};
    pipelines->pendingHandles[pipelines->pendingCount] = handle;
    ++pipelines->pendingCount;
    *handleOut = handle;
}

// Finishes a pipeline once D3D12 has made or refused its state: its
// answer queued, or its slot freed with the status.
static mrhiResult Finish(mrhiD3d12Pipelines* pipelines, uint32_t handle, mrhiResult status,
                         const char* label, size_t labelLength, uint64_t tag, uint64_t* handleOut)
{
    mrhiD3d12Pipeline* made = &pipelines->pipelines[handle - 1];
    if (status == mrhi_success && made->state == nullptr)
    {
        status = mrhi_errorPlatform;
    }
    if (status != mrhi_success)
    {
        FreePipeline(pipelines, handle);
        return status;
    }
    mrhiD3d12Label((ID3D12Object*)made->state, label, labelLength);
    Answer(pipelines, handle, tag, handleOut);
    return mrhi_success;
}

mrhiResult mrhiD3d12CreateCompute(mrhiD3d12Pipelines* pipelines,
                                  const mrhiDriverComputePipeline* pipeline, uint64_t tag,
                                  uint64_t* handleOut)
{
    *handleOut = 0;
    const mrhiD3d12Shader* shader = &pipelines->shaders[pipeline->shader - 1];
    uint32_t handle = NewPipeline(pipelines, shader, true);
    if (handle == 0)
    {
        return mrhi_errorCapacity;
    }
    mrhiD3d12Pipeline* made = &pipelines->pipelines[handle - 1];
    made->rootBytes = pipeline->reflection->rootBlockBytes;
    mrhiResult status = mrhi_errorUnsupported;
    if (Specialize(shader, pipeline->reflection, pipeline->constants, pipeline->constantCount,
                   made->constants))
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {.pRootSignature = shader->root,
                                                  .CS = shader->code[pipeline->entry]};
        status = FAILED(ID3D12Device_CreateComputePipelineState(
                     pipelines->device, &desc, &IID_ID3D12PipelineState, (void**)&made->state))
                     ? mrhi_errorPlatform
                     : mrhi_success;
    }
    return Finish(pipelines, handle, status, pipeline->label, pipeline->labelLength, tag,
                  handleOut);
}

// An indirect draw's command signature that first sets the vertex
// information, the two words before the draw's arguments: nullptr when
// D3D12 makes none.
static ID3D12CommandSignature* SignatureOf(ID3D12Device* device, const mrhiD3d12Pipeline* pipeline,
                                           bool indexed)
{
    const D3D12_INDIRECT_ARGUMENT_DESC arguments[] = {
        {.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT,
         .Constant = {.RootParameterIndex = pipeline->layout.vertexInfoParameter,
                      .Num32BitValuesToSet = 2}},
        {.Type = indexed ? D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED
                         : D3D12_INDIRECT_ARGUMENT_TYPE_DRAW},
    };
    const D3D12_COMMAND_SIGNATURE_DESC desc = {
        .ByteStride = 2 * sizeof(uint32_t) + (indexed ? sizeof(D3D12_DRAW_INDEXED_ARGUMENTS)
                                                      : sizeof(D3D12_DRAW_ARGUMENTS)),
        .NumArgumentDescs = 2,
        .pArgumentDescs = arguments,
    };
    ID3D12CommandSignature* signature = nullptr;
    return SUCCEEDED(ID3D12Device_CreateCommandSignature(
               device, &desc, pipeline->root, &IID_ID3D12CommandSignature, (void**)&signature))
               ? signature
               : nullptr;
}

mrhiResult mrhiD3d12CreateGraphics(mrhiD3d12Pipelines* pipelines,
                                   const mrhiDriverGraphicsPipeline* pipeline, uint64_t tag,
                                   uint64_t* handleOut)
{
    *handleOut = 0;
    const mrhiD3d12Shader* shader = &pipelines->shaders[pipeline->shader - 1];
    const mrhiGraphicsPipelineDef* def = pipeline->def;
    uint32_t handle = NewPipeline(pipelines, shader, false);
    if (handle == 0)
    {
        return mrhi_errorCapacity;
    }
    mrhiD3d12Pipeline* made = &pipelines->pipelines[handle - 1];
    made->vertexInfo = shader->vertexInfo[pipeline->vertexEntry];
    made->rootBytes = pipeline->reflection->rootBlockBytes;
    for (uint32_t i = 0; i < def->vertexBufferCount; ++i)
    {
        made->strides[i] = def->vertexBuffers[i].stride;
    }
    mrhiResult status = mrhi_errorUnsupported;
    if (Specialize(shader, pipeline->reflection, def->constants, def->constantCount,
                   made->constants))
    {
        mrhiD3d12Graphics graphics;
        mrhiD3d12DescribeGraphics(def, &graphics);
        graphics.desc.pRootSignature = shader->root;
        graphics.desc.VS = shader->code[pipeline->vertexEntry];
        if (pipeline->fragmentEntry < shader->entryCount)
        {
            graphics.desc.PS = shader->code[pipeline->fragmentEntry];
        }
        made->topology = graphics.topology;
        status =
            FAILED(ID3D12Device_CreateGraphicsPipelineState(
                pipelines->device, &graphics.desc, &IID_ID3D12PipelineState, (void**)&made->state))
                ? mrhi_errorPlatform
                : mrhi_success;
    }
    if (status == mrhi_success && made->vertexInfo)
    {
        made->draw = SignatureOf(pipelines->device, made, false);
        made->drawIndexed = SignatureOf(pipelines->device, made, true);
        status = made->draw != nullptr && made->drawIndexed != nullptr ? mrhi_success
                                                                       : mrhi_errorPlatform;
    }
    return Finish(pipelines, handle, status, def->label, def->labelLength, tag, handleOut);
}

void mrhiD3d12ForgetPipeline(mrhiD3d12Pipelines* pipelines, uint64_t handle)
{
    for (uint32_t i = 0; i < pipelines->pendingCount; ++i)
    {
        if (pipelines->pendingHandles[i] == handle)
        {
            --pipelines->pendingCount;
            memmove(&pipelines->pending[i], &pipelines->pending[i + 1],
                    (pipelines->pendingCount - i) * sizeof(mrhiDriverEvent));
            memmove(&pipelines->pendingHandles[i], &pipelines->pendingHandles[i + 1],
                    (pipelines->pendingCount - i) * sizeof(uint64_t));
            break;
        }
    }
}

void mrhiD3d12ReleasePipeline(mrhiD3d12Pipelines* pipelines, uint64_t handle)
{
    FreePipeline(pipelines, handle);
}

size_t mrhiD3d12PollPipelines(mrhiD3d12Pipelines* pipelines, mrhiDriverEvent* events,
                              size_t capacity)
{
    size_t moved = pipelines->pendingCount < capacity ? pipelines->pendingCount : capacity;
    memcpy(events, pipelines->pending, moved * sizeof(mrhiDriverEvent));
    pipelines->pendingCount -= (uint32_t)moved;
    memmove(pipelines->pending, pipelines->pending + moved,
            pipelines->pendingCount * sizeof(mrhiDriverEvent));
    memmove(pipelines->pendingHandles, pipelines->pendingHandles + moved,
            pipelines->pendingCount * sizeof(uint64_t));
    return moved;
}
