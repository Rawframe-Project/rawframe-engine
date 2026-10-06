// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Draws and dispatches (mrhi-0011): vertex and index buffers set per
// slot, and each draw or dispatch checked against the pass's state as
// WebGPU checks it: its pipeline, the tables that pipeline reads, and
// buffers large enough for the elements it names. Indirect forms check
// the same state and where their arguments lie, but not the arguments,
// which are on the GPU.

#include "encoder_core.h"

#include <string.h>

// A buffer of the open frame a pass declares with an access kind,
// resolving a range of it: its slot, or 0 with the refusal, misuse
// counted under its check.
static uint32_t DeclaredRange(mrhiDevice* device, const mrhiFramePass* pass, mrhiResourceId id,
                              uint8_t kind, uint64_t offset, uint64_t* sizeInOut,
                              mrhiResult* statusOut)
{
    uint32_t object = mrhiFindFrameResource(device, id);
    if (object == 0)
    {
        *statusOut = mrhi_errorStale;
        return 0;
    }
    // Only a buffer is declared with the vertex or index access.
    bool declared = mrhiPassDeclares(device, pass, object, MRHI_KIND(kind), nullptr);
    uint64_t total = declared ? mrhiBufferBytesOf(&device->frameResources[object - 1]) : 0;
    uint64_t size = *sizeInOut == MRHI_WHOLE_SIZE && offset <= total ? total - offset : *sizeInOut;
    mrhiDiagnosticCode fault = !declared ? mrhi_diagnosticUndeclaredAccess
                               : offset > total || size > total - offset
                                   ? mrhi_diagnosticTransferRange
                                   : 0;
    if (fault != 0)
    {
        *statusOut = mrhiDeviceMisuse(device, fault);
        return 0;
    }
    *sizeInOut = size;
    return object;
}

mrhiResult mrhiSetVertexBuffer(mrhiDevice* device, mrhiPassId id, uint32_t slot,
                               mrhiResourceId resource, uint64_t offset, uint64_t size)
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
    if (slot >= device->limits.vertexBuffers)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticVertexBufferSlot);
    }
    if (offset % 4 != 0)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticTransferAlignment);
    }
    uint32_t object =
        DeclaredRange(device, pass, resource, mrhi_accessVertex, offset, &size, &status);
    if (object == 0)
    {
        return status;
    }
    mrhiCommand* record = mrhiTakeCommands(device, pass, 1);
    if (record == nullptr)
    {
        return mrhi_errorCapacity;
    }
    *record = (mrhiCommand){
        .type = mrhiCommandVertexBuffer,
        .a = slot,
        .b = object,
        .c = offset,
        .d = size,
    };
    device->frameVertexBytes[(size_t)(id.index1 - 1) * device->limits.vertexBuffers + slot] =
        size + 1;
    return mrhi_success;
}

mrhiResult mrhiSetIndexBuffer(mrhiDevice* device, mrhiPassId id, mrhiResourceId resource,
                              mrhiIndexFormat format, uint64_t offset, uint64_t size)
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
    uint64_t width = format == mrhi_indexUint16 ? 2 : 4;
    if (mrhiWorkOf(pass) != mrhiWorkRender)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticRenderStateOutsideRenderPass);
    }
    if (format == mrhi_indexNone || format > mrhi_indexUint32)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticIndexBufferFormat);
    }
    if (offset % width != 0)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticTransferAlignment);
    }
    uint32_t object =
        DeclaredRange(device, pass, resource, mrhi_accessIndex, offset, &size, &status);
    if (object == 0)
    {
        return status;
    }
    if (size % width != 0)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticTransferAlignment);
    }
    mrhiCommand* record = mrhiTakeCommands(device, pass, 1);
    if (record == nullptr)
    {
        return mrhi_errorCapacity;
    }
    *record = (mrhiCommand){
        .type = mrhiCommandIndexBuffer,
        .a = format,
        .b = object,
        .c = offset,
        .d = size,
    };
    pass->indexFormat = format;
    pass->indexBytes = size;
    return mrhi_success;
}

// The pass's pipeline, of a kind, with every table its reflection reads
// set under a container of the same digest: or NULL with the refusal.
static const mrhiPipelineSlot* ReadyToRun(const mrhiDevice* device, const mrhiFramePass* pass,
                                          mrhiResult* statusOut)
{
    if (pass->pipeline == 0)
    {
        *statusOut = mrhi_errorState;
        return nullptr;
    }
    if (!mrhiPoolIsLive(&device->pipelines, pass->pipeline, pass->pipelineGeneration))
    {
        *statusOut = mrhi_errorStale;
        return nullptr;
    }
    const mrhiPipelineSlot* slot = &device->pipelineSlots[pass->pipeline - 1];
    const mrhiReflection* reflection = slot->reflection;
    for (uint32_t i = 0; i < reflection->bindingCount; ++i)
    {
        uint32_t table = reflection->bindings[i].table;
        if ((pass->tablesSet & (1u << table)) == 0 ||
            memcmp(pass->tableDigests[table], reflection->digest, MRHI_DIGEST_BYTES) != 0)
        {
            *statusOut = mrhi_errorState;
            return nullptr;
        }
    }
    return slot;
}

// Whether a vertex buffer of bytes holds count elements from first:
// every element's stride but the last's, which needs only the bytes its
// attributes reach.
static bool Holds(const mrhiVertexFacts* facts, uint64_t bytes, uint32_t first, uint32_t count)
{
    uint64_t elements = (uint64_t)first + count;
    return elements == 0 || (elements - 1) * facts->stride + facts->lastStride <= bytes;
}

// Checks the pipeline's vertex buffers for a draw of vertices and
// instances: success, mrhi_errorState for a buffer not set, or
// mrhi_errorInvalid for one too small. An indexed draw names no
// vertices: which it reads depends on its indices.
static mrhiResult CheckVertexBuffers(const mrhiDevice* device, mrhiPassId id,
                                     const mrhiPipelineSlot* slot, uint32_t firstVertex,
                                     uint32_t vertexCount, uint32_t firstInstance,
                                     uint32_t instanceCount)
{
    uint32_t slots = device->limits.vertexBuffers;
    const mrhiVertexFacts* facts =
        &device->pipelineVertex[(size_t)(slot - device->pipelineSlots) * slots];
    const uint64_t* bytes = &device->frameVertexBytes[(size_t)(id.index1 - 1) * slots];
    mrhiResult status = mrhi_success;
    for (uint32_t i = 0; i < slot->vertexBufferCount && status != mrhi_errorState; ++i)
    {
        if (bytes[i] == 0)
        {
            status = mrhi_errorState;
        }
        else if (facts[i].stepMode == mrhi_stepInstance)
        {
            status = Holds(&facts[i], bytes[i] - 1, firstInstance, instanceCount)
                         ? status
                         : mrhi_errorInvalid;
        }
        else
        {
            status = Holds(&facts[i], bytes[i] - 1, firstVertex, vertexCount) ? status
                                                                              : mrhi_errorInvalid;
        }
    }
    return status;
}

// The render pass a draw records in, with its pipeline ready to run:
// or NULL with the refusal.
static mrhiFramePass* DrawPass(mrhiDevice* device, mrhiPassId id, const mrhiPipelineSlot** slotOut,
                               mrhiResult* statusOut)
{
    mrhiFramePass* pass = mrhiRecordingPass(device, id, statusOut);
    if (pass == nullptr)
    {
        return nullptr;
    }
    if (mrhiWorkOf(pass) != mrhiWorkRender)
    {
        *statusOut = mrhiDeviceMisuse(device, mrhi_diagnosticRenderStateOutsideRenderPass);
        return nullptr;
    }
    *slotOut = ReadyToRun(device, pass, statusOut);
    return *slotOut == nullptr ? nullptr : pass;
}

mrhiResult mrhiDraw(mrhiDevice* device, mrhiPassId id, uint32_t vertexCount, uint32_t instanceCount,
                    uint32_t firstVertex, uint32_t firstInstance)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = mrhi_success;
    const mrhiPipelineSlot* slot = nullptr;
    mrhiFramePass* pass = DrawPass(device, id, &slot, &status);
    if (pass == nullptr)
    {
        return status;
    }
    status = CheckVertexBuffers(device, id, slot, firstVertex, vertexCount, firstInstance,
                                instanceCount);
    if (status != mrhi_success)
    {
        return status == mrhi_errorInvalid
                   ? mrhiDeviceMisuse(device, mrhi_diagnosticVertexBufferTooSmall)
                   : status;
    }
    mrhiCommand* record = mrhiTakeCommands(device, pass, 1);
    if (record == nullptr)
    {
        return mrhi_errorCapacity;
    }
    *record = (mrhiCommand){
        .type = mrhiCommandDraw,
        .a = vertexCount,
        .b = instanceCount,
        .c = firstVertex,
        .d = firstInstance,
    };
    return mrhi_success;
}

mrhiResult mrhiDrawIndexed(mrhiDevice* device, mrhiPassId id, uint32_t indexCount,
                           uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex,
                           uint32_t firstInstance)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = mrhi_success;
    const mrhiPipelineSlot* slot = nullptr;
    mrhiFramePass* pass = DrawPass(device, id, &slot, &status);
    if (pass == nullptr)
    {
        return status;
    }
    if (pass->indexFormat == mrhi_indexNone ||
        (slot->stripIndexFormat != mrhi_indexNone && slot->stripIndexFormat != pass->indexFormat))
    {
        return mrhi_errorState;
    }
    status = CheckVertexBuffers(device, id, slot, 0, 0, firstInstance, instanceCount);
    uint64_t width = pass->indexFormat == mrhi_indexUint16 ? 2 : 4;
    if (status == mrhi_success && (uint64_t)firstIndex + indexCount > pass->indexBytes / width)
    {
        status = mrhi_errorInvalid;
    }
    if (status != mrhi_success)
    {
        return status == mrhi_errorInvalid
                   ? mrhiDeviceMisuse(device, mrhi_diagnosticVertexBufferTooSmall)
                   : status;
    }
    mrhiCommand* record = mrhiTakeCommands(device, pass, 1);
    if (record == nullptr)
    {
        return mrhi_errorCapacity;
    }
    *record = (mrhiCommand){
        .type = mrhiCommandDrawIndexed,
        .a = indexCount,
        .b = instanceCount,
        .c = firstIndex | (uint64_t)(uint32_t)baseVertex << 32,
        .d = firstInstance,
    };
    return mrhi_success;
}

mrhiResult mrhiDispatch(mrhiDevice* device, mrhiPassId id, uint32_t x, uint32_t y, uint32_t z)
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
    uint32_t most = device->limits.workgroupsPerDimension;
    if (mrhiWorkOf(pass) != mrhiWorkCompute)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticComputeOutsideComputePass);
    }
    if (x > most || y > most || z > most)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticDispatchSize);
    }
    if (ReadyToRun(device, pass, &status) == nullptr)
    {
        return status;
    }
    mrhiCommand* record = mrhiTakeCommands(device, pass, 1);
    if (record == nullptr)
    {
        return mrhi_errorCapacity;
    }
    *record = (mrhiCommand){.type = mrhiCommandDispatch, .a = x, .b = y, .c = z};
    return mrhi_success;
}

// Checks the indirect arguments of bytes a command reads, then records
// the command: success or the refusal.
static mrhiResult RecordIndirect(mrhiDevice* device, mrhiFramePass* pass, mrhiCommandType type,
                                 mrhiResourceId resource, uint64_t offset, uint64_t bytes)
{
    mrhiResult status = mrhi_success;
    if (offset % 4 != 0)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticTransferAlignment);
    }
    uint32_t object =
        DeclaredRange(device, pass, resource, mrhi_accessIndirect, offset, &bytes, &status);
    if (object == 0)
    {
        return status;
    }
    mrhiCommand* record = mrhiTakeCommands(device, pass, 1);
    if (record == nullptr)
    {
        return mrhi_errorCapacity;
    }
    *record = (mrhiCommand){.type = type, .a = object, .c = offset};
    return mrhi_success;
}

// The pass of an indirect draw, recording, with what the draw reads set:
// a pipeline, each vertex buffer (no counts, so only whether each is
// set) and, for an indexed draw, an index buffer of the pipeline's strip
// index format.
static mrhiFramePass* IndirectPass(mrhiDevice* device, mrhiPassId id, bool indexed,
                                   mrhiResult* statusOut)
{
    const mrhiPipelineSlot* slot = nullptr;
    mrhiFramePass* pass = DrawPass(device, id, &slot, statusOut);
    if (pass == nullptr)
    {
        return nullptr;
    }
    if (indexed &&
        (pass->indexFormat == mrhi_indexNone ||
         (slot->stripIndexFormat != mrhi_indexNone && slot->stripIndexFormat != pass->indexFormat)))
    {
        *statusOut = mrhi_errorState;
        return nullptr;
    }
    *statusOut = CheckVertexBuffers(device, id, slot, 0, 0, 0, 0);
    return *statusOut == mrhi_success ? pass : nullptr;
}

mrhiResult mrhiDrawIndirect(mrhiDevice* device, mrhiPassId id, mrhiResourceId resource,
                            uint64_t offset)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = mrhi_success;
    mrhiFramePass* pass = IndirectPass(device, id, false, &status);
    return pass == nullptr
               ? status
               : RecordIndirect(device, pass, mrhiCommandDrawIndirect, resource, offset, 16);
}

mrhiResult mrhiDrawIndexedIndirect(mrhiDevice* device, mrhiPassId id, mrhiResourceId resource,
                                   uint64_t offset)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = mrhi_success;
    mrhiFramePass* pass = IndirectPass(device, id, true, &status);
    return pass == nullptr
               ? status
               : RecordIndirect(device, pass, mrhiCommandDrawIndexedIndirect, resource, offset, 20);
}

// Records a counted multi-draw (mrhi-0020): maxCount packed records and a
// 32-bit count, each in a buffer the pass declares with the indirect
// access.
static mrhiResult RecordCounted(mrhiDevice* device, mrhiPassId id, bool indexed,
                                mrhiResourceId resource, uint64_t offset,
                                mrhiResourceId countResource, uint64_t countOffset,
                                uint32_t maxCount)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (!device->features.multiDrawIndirectCount)
    {
        return mrhi_errorUnsupported;
    }
    if (maxCount == 0 || maxCount > MRHI_INDIRECT_DRAWS)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticIndirectCountLimit);
    }
    mrhiResult status = mrhi_success;
    mrhiFramePass* pass = IndirectPass(device, id, indexed, &status);
    if (pass == nullptr)
    {
        return status;
    }
    uint64_t bytes = (uint64_t)maxCount * (indexed ? 20 : 16);
    uint64_t four = 4;
    if (offset % 4 != 0 || countOffset % 4 != 0)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticTransferAlignment);
    }
    uint32_t object =
        DeclaredRange(device, pass, resource, mrhi_accessIndirect, offset, &bytes, &status);
    uint32_t count = object == 0 ? 0
                                 : DeclaredRange(device, pass, countResource, mrhi_accessIndirect,
                                                 countOffset, &four, &status);
    if (count == 0)
    {
        return status;
    }
    // The frame's counted draws stay within the limit, whatever other
    // passes take meanwhile; draws taken by a call that then finds the
    // commands full stay taken, the frame being full anyway.
    uint64_t taken = atomic_load_explicit(&device->countedTaken, memory_order_relaxed);
    do
    {
        if (maxCount > device->deviceLimits.frameIndirectDraws - taken)
        {
            return mrhi_errorCapacity;
        }
    } while (!atomic_compare_exchange_weak_explicit(&device->countedTaken, &taken, taken + maxCount,
                                                    memory_order_relaxed, memory_order_relaxed));
    mrhiCommand* record = mrhiTakeCommands(device, pass, 1);
    if (record == nullptr)
    {
        return mrhi_errorCapacity;
    }
    *record = (mrhiCommand){
        .type = indexed ? mrhiCommandDrawIndexedIndirectCount : mrhiCommandDrawIndirectCount,
        .a = object,
        .b = (uint64_t)maxCount << 32 | count,
        .c = offset,
        .d = countOffset,
    };
    return mrhi_success;
}

mrhiResult mrhiDrawIndirectCount(mrhiDevice* device, mrhiPassId pass, mrhiResourceId resource,
                                 uint64_t offset, mrhiResourceId countResource,
                                 uint64_t countOffset, uint32_t maxCount)
{
    return RecordCounted(device, pass, false, resource, offset, countResource, countOffset,
                         maxCount);
}

mrhiResult mrhiDrawIndexedIndirectCount(mrhiDevice* device, mrhiPassId pass,
                                        mrhiResourceId resource, uint64_t offset,
                                        mrhiResourceId countResource, uint64_t countOffset,
                                        uint32_t maxCount)
{
    return RecordCounted(device, pass, true, resource, offset, countResource, countOffset,
                         maxCount);
}

mrhiResult mrhiDispatchIndirect(mrhiDevice* device, mrhiPassId id, mrhiResourceId resource,
                                uint64_t offset)
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
    if (ReadyToRun(device, pass, &status) == nullptr)
    {
        return status;
    }
    return RecordIndirect(device, pass, mrhiCommandDispatchIndirect, resource, offset, 12);
}
