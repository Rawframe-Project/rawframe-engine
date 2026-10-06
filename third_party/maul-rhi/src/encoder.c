// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Encoders (mrhi-0011): a kept pass of a compiled frame, begun by the
// thread that claims it, records commands into chunks of the frame's
// arena, which it takes with one atomic counter, and is ended before
// the frame is submitted. Every command is checked against its pass.

#include "encoder_core.h"
#include "invariant.h"
#include "label.h"

#include <math.h>
#include <stdatomic.h>
#include <string.h>

mrhiPassWork mrhiWorkOf(const mrhiFramePass* pass)
{
    if (pass->passClass == mrhi_passTransfer)
    {
        return mrhiWorkTransfer;
    }
    bool targets = pass->colorTargetCount > 0 || pass->depthTarget.resource.index1 != 0;
    return targets ? mrhiWorkRender : mrhiWorkCompute;
}

// The pass an id names in the open, compiled frame: or NULL with the
// refusal.
static mrhiFramePass* Find(mrhiDevice* device, mrhiPassId id, mrhiResult* statusOut)
{
    if (!device->frameOpen || !device->frameCompiled)
    {
        *statusOut = mrhi_errorState;
        return nullptr;
    }
    if (id.generation != device->frameSerial || id.index1 == 0 ||
        id.index1 > device->framePassCount)
    {
        *statusOut = mrhi_errorStale;
        return nullptr;
    }
    return &device->framePasses[id.index1 - 1];
}

bool mrhiPassDeclares(const mrhiDevice* device, const mrhiFramePass* pass, uint32_t object,
                      uint32_t kinds, const mrhiFrameUse* part)
{
    for (uint32_t i = pass->firstUse; i < pass->firstUse + pass->useCount; ++i)
    {
        const mrhiFrameUse* use = &device->frameUses[i];
        if (use->resource != object || (kinds & MRHI_KIND(use->use)) == 0)
        {
            continue;
        }
        if (part == nullptr ||
            ((use->planes & part->planes) == part->planes && use->baseMip <= part->baseMip &&
             (uint64_t)part->baseMip + part->mipCount <= (uint64_t)use->baseMip + use->mipCount &&
             use->baseLayer <= part->baseLayer &&
             (uint64_t)part->baseLayer + part->layerCount <=
                 (uint64_t)use->baseLayer + use->layerCount))
        {
            return true;
        }
    }
    return false;
}

uint64_t mrhiBufferBytesOf(const mrhiFrameResource* resource)
{
    return resource->size;
}

mrhiFramePass* mrhiOpenPass(mrhiDevice* device, mrhiPassId id, mrhiResult* statusOut)
{
    mrhiFramePass* pass = Find(device, id, statusOut);
    if (pass != nullptr &&
        atomic_load_explicit(&pass->recording, memory_order_relaxed) != mrhiRecordingOpen)
    {
        *statusOut = mrhi_errorState;
        return nullptr;
    }
    return pass;
}

mrhiResult mrhiSetNativeCommands(mrhiDevice* device, mrhiPassId id, void* commands)
{
    mrhiResult status = mrhi_success;
    mrhiFramePass* pass = mrhiOpenPass(device, id, &status);
    if (pass == nullptr)
    {
        return status;
    }
    if (commands == nullptr || !pass->native)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNativeCommands);
    }
    if (pass->nativeCommands != nullptr)
    {
        return mrhi_errorState;
    }
    pass->nativeCommands = commands;
    return mrhi_success;
}

mrhiFramePass* mrhiRecordingPass(mrhiDevice* device, mrhiPassId id, mrhiResult* statusOut)
{
    mrhiFramePass* pass = mrhiOpenPass(device, id, statusOut);
    // A native pass's commands are the program's own (mrhi-0018).
    if (pass != nullptr && pass->native)
    {
        *statusOut = mrhiDeviceMisuse(device, mrhi_diagnosticRecordedInNativePass);
        return nullptr;
    }
    return pass;
}

mrhiCommand* mrhiTakeCommands(mrhiDevice* device, mrhiFramePass* pass, uint32_t count)
{
    MRHI_ASSERT(count <= MRHI_CHUNK_COMMANDS);
    if (pass->overflowed)
    {
        return nullptr;
    }
    mrhiCommandChunk* chunk =
        pass->lastChunk == 0 ? nullptr : &device->frameChunks[pass->lastChunk - 1];
    if (chunk == nullptr || chunk->count + count > MRHI_CHUNK_COMMANDS)
    {
        uint32_t index =
            atomic_fetch_add_explicit(&device->frameChunksTaken, 1, memory_order_relaxed);
        if (index >= device->frameChunkCount)
        {
            pass->overflowed = true;
            return nullptr;
        }
        mrhiCommandChunk* fresh = &device->frameChunks[index];
        fresh->next = 0;
        fresh->count = 0;
        if (chunk == nullptr)
        {
            pass->firstChunk = index + 1;
        }
        else
        {
            chunk->next = index + 1;
        }
        pass->lastChunk = index + 1;
        chunk = fresh;
    }
    mrhiCommand* records = &chunk->commands[chunk->count];
    chunk->count += count;
    return records;
}

// Records a command with bytes of payload after it: success, or
// mrhi_errorCapacity.
static mrhiResult Record(mrhiDevice* device, mrhiFramePass* pass, mrhiCommand command,
                         const void* payload, size_t bytes)
{
    uint32_t records = (uint32_t)((bytes + sizeof(mrhiCommand) - 1) / sizeof(mrhiCommand));
    mrhiCommand* at = mrhiTakeCommands(device, pass, 1 + records);
    if (at == nullptr)
    {
        return mrhi_errorCapacity;
    }
    command.payload = (uint16_t)records;
    at[0] = command;
    if (bytes > 0)
    {
        memcpy(&at[1], payload, bytes);
    }
    return mrhi_success;
}

mrhiResult mrhiBeginPass(mrhiDevice* device, mrhiPassId id)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = mrhi_success;
    mrhiFramePass* pass = Find(device, id, &status);
    if (pass == nullptr)
    {
        return status;
    }
    uint32_t idle = mrhiRecordingIdle;
    if (!pass->kept ||
        !atomic_compare_exchange_strong_explicit(&pass->recording, &idle, mrhiRecordingOpen,
                                                 memory_order_acquire, memory_order_relaxed))
    {
        return mrhi_errorState;
    }
    uint32_t slots = device->limits.vertexBuffers;
    memset(&device->frameVertexBytes[(size_t)(id.index1 - 1) * slots], 0, slots * sizeof(uint64_t));
    return mrhi_success;
}

mrhiResult mrhiEndPass(mrhiDevice* device, mrhiPassId id)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = mrhi_success;
    mrhiFramePass* pass = mrhiOpenPass(device, id, &status);
    if (pass == nullptr)
    {
        return status;
    }
    if (pass->debugDepth > 0 || pass->occlusionOpen || pass->statisticsOpen)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticPassEndOpen);
    }
    atomic_store_explicit(&pass->recording, mrhiRecordingEnded, memory_order_release);
    return mrhi_success;
}

// Whether two render layouts are the same targets: formats by location,
// depth format and sample count.
static bool AreTargetsEqual(const mrhiRenderLayout* a, const mrhiRenderLayout* b)
{
    for (uint32_t i = 0; i < MRHI_COLOR_TARGETS; ++i)
    {
        if (a->colors[i] != b->colors[i])
        {
            return false;
        }
    }
    return a->depth == b->depth && a->samples == b->samples && a->views == b->views;
}

// The pipeline slot a live, ready id of a kind names: or NULL with the
// refusal.
static const mrhiPipelineSlot* ReadyPipeline(const mrhiDevice* device, mrhiPipelineKind kind,
                                             uint32_t index1, uint32_t generation,
                                             mrhiResult* statusOut)
{
    if (!mrhiPoolIsLive(&device->pipelines, index1, generation) ||
        device->pipelineSlots[index1 - 1].kind != kind)
    {
        *statusOut = mrhi_errorStale;
        return nullptr;
    }
    const mrhiPipelineSlot* slot = &device->pipelineSlots[index1 - 1];
    if (slot->state != mrhiPipelineReady)
    {
        *statusOut = mrhi_errorState;
        return nullptr;
    }
    return slot;
}

mrhiResult mrhiSetGraphicsPipeline(mrhiDevice* device, mrhiPassId id,
                                   mrhiGraphicsPipelineId pipeline)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = mrhi_success;
    mrhiFramePass* pass = mrhiRecordingPass(device, id, &status);
    if (pass == nullptr)
    {
        return status;
    }
    if (mrhiWorkOf(pass) != mrhiWorkRender)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticRenderStateOutsideRenderPass);
    }
    const mrhiPipelineSlot* slot =
        ReadyPipeline(device, mrhiPipelineGraphics, pipeline.index1, pipeline.generation, &status);
    if (slot == nullptr)
    {
        return status;
    }
    bool readOnly = pass->depthTarget.readOnly;
    if (!AreTargetsEqual(&slot->layout, &pass->layout) ||
        (readOnly && (slot->layout.writesDepth || slot->layout.writesStencil)) ||
        (slot->heapUses != 0 && pass->heap == 0))
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticGraphicsPipelineMismatch);
    }
    pass->pipeline = pipeline.index1;
    pass->pipelineGeneration = pipeline.generation;
    mrhiCommand command = {.type = mrhiCommandGraphicsPipeline, .b = slot->handle};
    return Record(device, pass, command, nullptr, 0);
}

mrhiResult mrhiSetComputePipeline(mrhiDevice* device, mrhiPassId id, mrhiComputePipelineId pipeline)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = mrhi_success;
    mrhiFramePass* pass = mrhiRecordingPass(device, id, &status);
    if (pass == nullptr)
    {
        return status;
    }
    if (mrhiWorkOf(pass) != mrhiWorkCompute)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticComputeOutsideComputePass);
    }
    const mrhiPipelineSlot* slot =
        ReadyPipeline(device, mrhiPipelineCompute, pipeline.index1, pipeline.generation, &status);
    if (slot == nullptr)
    {
        return status;
    }
    // A pipeline reading a heap needs the pass's.
    if (slot->heapUses != 0 && pass->heap == 0)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticComputePipelineHeap);
    }
    pass->pipeline = pipeline.index1;
    pass->pipelineGeneration = pipeline.generation;
    mrhiCommand command = {.type = mrhiCommandComputePipeline, .b = slot->handle};
    return Record(device, pass, command, nullptr, 0);
}

mrhiResult mrhiSetRootBlock(mrhiDevice* device, mrhiPassId id, uint32_t offset, const void* bytes,
                            uint32_t size)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (bytes == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    mrhiResult status = mrhi_success;
    mrhiFramePass* pass = mrhiRecordingPass(device, id, &status);
    if (pass == nullptr)
    {
        return status;
    }
    if (mrhiWorkOf(pass) == mrhiWorkTransfer || offset % 4 != 0 || size % 4 != 0 || size == 0)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticRootBlock);
    }
    if ((uint64_t)offset + size > device->limits.rootBlockBytes)
    {
        return mrhi_errorUnsupported;
    }
    mrhiCommand command = {.type = mrhiCommandRootBlock, .a = offset, .b = size};
    return Record(device, pass, command, bytes, size);
}

// The render pass a draw-state command records in: or NULL with the
// refusal, a pass without targets counted as misuse.
static mrhiFramePass* RenderPass(mrhiDevice* device, mrhiPassId id, mrhiResult* statusOut)
{
    mrhiFramePass* pass = mrhiRecordingPass(device, id, statusOut);
    if (pass != nullptr && mrhiWorkOf(pass) != mrhiWorkRender)
    {
        *statusOut = mrhiDeviceMisuse(device, mrhi_diagnosticRenderStateOutsideRenderPass);
        return nullptr;
    }
    return pass;
}

// Whether a viewport is within WebGPU's ranges: its size within the 2D
// texture limit, its edges within twice it, and its depth range ordered
// within 0 to 1. The comparisons refuse NaN and infinities.
static bool IsViewportValid(const mrhiDevice* device, const mrhiViewport* viewport)
{
    float limit = (float)device->limits.textureDimension2d;
    float range = 2.0f * limit;
    return viewport->x >= -range && viewport->y >= -range && viewport->width >= 0.0f &&
           viewport->width <= limit && viewport->height >= 0.0f && viewport->height <= limit &&
           viewport->x + viewport->width <= range - 1.0f &&
           viewport->y + viewport->height <= range - 1.0f && viewport->minDepth >= 0.0f &&
           viewport->maxDepth <= 1.0f && viewport->minDepth <= viewport->maxDepth;
}

mrhiResult mrhiSetViewport(mrhiDevice* device, mrhiPassId id, const mrhiViewport* viewport)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (viewport == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    mrhiResult status = mrhi_success;
    mrhiFramePass* pass = RenderPass(device, id, &status);
    if (pass == nullptr)
    {
        return status;
    }
    if (!IsViewportValid(device, viewport))
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticViewport);
    }
    mrhiCommand command = {.type = mrhiCommandViewport};
    return Record(device, pass, command, viewport, sizeof(*viewport));
}

mrhiResult mrhiSetScissor(mrhiDevice* device, mrhiPassId id, const mrhiScissorRect* rect)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (rect == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    mrhiResult status = mrhi_success;
    mrhiFramePass* pass = RenderPass(device, id, &status);
    if (pass == nullptr)
    {
        return status;
    }
    if ((uint64_t)rect->x + rect->width > pass->width ||
        (uint64_t)rect->y + rect->height > pass->height)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticScissor);
    }
    mrhiCommand command = {
        .type = mrhiCommandScissor,
        .a = rect->x,
        .b = rect->y,
        .c = rect->width,
        .d = rect->height,
    };
    return Record(device, pass, command, nullptr, 0);
}

mrhiResult mrhiSetBlendConstant(mrhiDevice* device, mrhiPassId id, const mrhiClearColor* color)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (color == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    mrhiResult status = mrhi_success;
    mrhiFramePass* pass = RenderPass(device, id, &status);
    if (pass == nullptr)
    {
        return status;
    }
    if (!isfinite(color->red) || !isfinite(color->green) || !isfinite(color->blue) ||
        !isfinite(color->alpha))
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticBlendConstant);
    }
    mrhiCommand command = {.type = mrhiCommandBlendConstant};
    return Record(device, pass, command, color, sizeof(*color));
}

mrhiResult mrhiSetStencilReference(mrhiDevice* device, mrhiPassId id, uint32_t reference)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = mrhi_success;
    mrhiFramePass* pass = RenderPass(device, id, &status);
    if (pass == nullptr)
    {
        return status;
    }
    mrhiCommand command = {.type = mrhiCommandStencilReference, .a = reference};
    return Record(device, pass, command, nullptr, 0);
}

// Records a labelled debug command: success, or the refusal. Groups
// balance over the calls made, a push or pop the arena had no room for
// counted too, so that a pass that found it full still ends.
static mrhiResult Label(mrhiDevice* device, mrhiPassId id, mrhiCommandType type, const char* label,
                        size_t length)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (length == 0 || !mrhiIsLabelValid(label, length))
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticDebugLabel);
    }
    mrhiResult status = mrhi_success;
    mrhiFramePass* pass = mrhiRecordingPass(device, id, &status);
    if (pass == nullptr)
    {
        return status;
    }
    mrhiCommand command = {.type = (uint16_t)type, .b = length};
    if (type == mrhiCommandPushDebugGroup)
    {
        ++pass->debugDepth;
    }
    return Record(device, pass, command, label, length);
}

mrhiResult mrhiPushDebugGroup(mrhiDevice* device, mrhiPassId pass, const char* label,
                              size_t labelLength)
{
    return Label(device, pass, mrhiCommandPushDebugGroup, label, labelLength);
}

mrhiResult mrhiInsertDebugMarker(mrhiDevice* device, mrhiPassId pass, const char* label,
                                 size_t labelLength)
{
    return Label(device, pass, mrhiCommandDebugMarker, label, labelLength);
}

mrhiResult mrhiPopDebugGroup(mrhiDevice* device, mrhiPassId id)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = mrhi_success;
    mrhiFramePass* pass = mrhiRecordingPass(device, id, &status);
    if (pass == nullptr)
    {
        return status;
    }
    if (pass->debugDepth == 0)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticDebugGroupUnderflow);
    }
    --pass->debugDepth;
    mrhiCommand command = {.type = mrhiCommandPopDebugGroup};
    return Record(device, pass, command, nullptr, 0);
}
