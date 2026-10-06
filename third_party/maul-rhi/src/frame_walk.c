// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The walk of a submitted frame (mrhi-0013): each resource, pass and
// barrier of the view, and each command record of each pass, checked
// against the frame and the device's handles. The test driver traps on
// what it finds; the validation layer counts it (mrhi-0025).

#include "frame_walk.h"

#include "label.h"

#include <string.h>

// What a walk checks names against, what it has counted, and the
// faults it has found: records the core made that no driver could
// translate.
typedef struct Walk
{
    const mrhiDriverFrame* frame;
    mrhiHandleCheck isHandle;
    const void* handles;
    uint64_t faults;
    mrhiTestFrameLog counts;
} Walk;

// A check: whether it held, counting a fault when it did not. Where a
// failed check leaves later reads unsafe, the walk stops there.
static bool Holds(Walk* walk, bool condition)
{
    walk->faults += condition ? 0 : 1;
    return condition;
}

#define WALK_CHECK(cond) Holds(walk, (cond))

static bool IsHandle(const Walk* walk, uint64_t handle)
{
    return walk->isHandle(walk->handles, handle);
}

static bool IsBuffer(mrhiDriverResourceKind kind)
{
    return kind == mrhiDriverTransientBuffer || kind == mrhiDriverDeviceBuffer;
}

// Whether a frame resource's slot plus one names a buffer, or a texture,
// of the frame.
static bool Names(const Walk* walk, uint64_t object, bool buffer)
{
    return object != 0 && object <= walk->frame->resourceCount &&
           IsBuffer(walk->frame->resources[object - 1].kind) == buffer;
}

// Checks a table's bindings: frame resources, or samplers by handle,
// each with its slot's kind.
static void CheckBindings(Walk* walk, const mrhiCommand* command)
{
    for (uint32_t i = 0; i < command->payload; ++i)
    {
        mrhiCommandBinding binding;
        memcpy(&binding, &command[1 + i], sizeof(binding));
        WALK_CHECK(binding.object == 0 ? IsHandle(walk, binding.offset)
                                       : binding.object <= walk->frame->resourceCount);
        WALK_CHECK(binding.kind >= mrhi_bindingUniformBuffer &&
                   binding.kind <= mrhi_bindingStorageTexture &&
                   (binding.kind == mrhi_bindingSampler) == (binding.object == 0));
    }
}

// The bytes a buffer side of a copy covers: a buffer's bytes, or a
// texture's rows at the side's pitch.
static uint64_t SideBytes(const mrhiCommand* command, const mrhiCommandBufferSide* side,
                          bool texture)
{
    return texture ? (uint64_t)side->bytesPerRow * side->rowsPerImage * command->d : command->b;
}

// Checks a copy's two sides: each a frame resource of its type, or, for
// an upload's source or a readback's destination, bytes inside the
// frame's staging or readback ring.
static void CheckCopy(Walk* walk, const mrhiCommand* command, bool fromBuffer, bool toBuffer)
{
    if (!WALK_CHECK(command->payload == 2))
    {
        return;
    }
    mrhiCommandBufferSide from;
    mrhiCommandBufferSide to;
    memcpy(&from, &command[1], sizeof(from));
    memcpy(&to, &command[2], sizeof(to));
    bool upload =
        command->type == mrhiCommandWriteBuffer || command->type == mrhiCommandWriteTexture;
    bool readback =
        command->type == mrhiCommandReadBuffer || command->type == mrhiCommandReadTexture;
    const mrhiDriverFrame* frame = walk->frame;
    if (upload)
    {
        uint64_t bytes = SideBytes(command, &from, !toBuffer);
        WALK_CHECK(from.object == 0 && from.offset <= frame->stagingBytes &&
                   bytes <= frame->stagingBytes - from.offset);
        for (uint64_t i = 0; i < bytes; ++i)
        {
            walk->counts.uploadSum += frame->staging[from.offset + i];
        }
    }
    else
    {
        WALK_CHECK(Names(walk, from.object, fromBuffer));
    }
    if (readback)
    {
        uint64_t bytes = SideBytes(command, &to, !fromBuffer);
        WALK_CHECK(to.object == 0 && to.offset <= frame->readbackBytes &&
                   bytes <= frame->readbackBytes - to.offset);
    }
    else
    {
        WALK_CHECK(Names(walk, to.object, toBuffer));
    }
}

// Checks one command and its payload records.
static void CheckCommand(Walk* walk, const mrhiCommand* command)
{
    switch (command->type)
    {
    case mrhiCommandGraphicsPipeline:
    case mrhiCommandComputePipeline:
        WALK_CHECK(IsHandle(walk, command->b));
        break;
    case mrhiCommandBindings:
        CheckBindings(walk, command);
        break;
    case mrhiCommandVertexBuffer:
    case mrhiCommandIndexBuffer:
        WALK_CHECK(Names(walk, command->b, true));
        break;
    case mrhiCommandDrawIndirect:
    case mrhiCommandDrawIndexedIndirect:
    case mrhiCommandDispatchIndirect:
        WALK_CHECK(Names(walk, command->a, true));
        break;
    case mrhiCommandDrawIndirectCount:
    case mrhiCommandDrawIndexedIndirectCount:
        WALK_CHECK(Names(walk, command->a, true) && Names(walk, (uint32_t)command->b, true));
        break;
    case mrhiCommandBeginOcclusionQuery:
    case mrhiCommandBeginStatisticsQuery:
    case mrhiCommandEndStatisticsQuery:
        WALK_CHECK(IsHandle(walk, command->b));
        break;
    case mrhiCommandResolveQueries:
        WALK_CHECK(Names(walk, command->a, true) && IsHandle(walk, command->b));
        break;
    case mrhiCommandCopyBuffer:
    case mrhiCommandWriteBuffer:
    case mrhiCommandReadBuffer:
        CheckCopy(walk, command, true, true);
        break;
    case mrhiCommandCopyBufferToTexture:
    case mrhiCommandWriteTexture:
        CheckCopy(walk, command, true, false);
        break;
    case mrhiCommandCopyTextureToBuffer:
    case mrhiCommandReadTexture:
        CheckCopy(walk, command, false, true);
        break;
    case mrhiCommandCopyTexture:
        CheckCopy(walk, command, false, false);
        break;
    case mrhiCommandClearBuffer:
        WALK_CHECK(Names(walk, command->a, true) && command->c % 4 == 0 && command->d > 0 &&
                   command->d % 4 == 0);
        break;
    default:
        WALK_CHECK(command->type != 0 && command->type < mrhiCommandTypeEnd);
        break;
    }
}

// Walks a pass's chunks, each command's payload inside its chunk.
static void WalkCommands(Walk* walk, const mrhiDriverPass* pass)
{
    const mrhiDriverFrame* frame = walk->frame;
    uint32_t visited = 0;
    for (uint32_t chunk = pass->firstChunk; chunk != 0; chunk = frame->chunks[chunk - 1].next)
    {
        if (!WALK_CHECK(chunk <= frame->chunkCount && ++visited <= frame->chunkCount))
        {
            return;
        }
        const mrhiCommandChunk* at = &frame->chunks[chunk - 1];
        if (!WALK_CHECK(at->count <= MRHI_CHUNK_COMMANDS))
        {
            return;
        }
        uint32_t i = 0;
        while (i < at->count)
        {
            const mrhiCommand* command = &at->commands[i];
            if (!WALK_CHECK(command->payload < at->count - i))
            {
                return;
            }
            CheckCommand(walk, command);
            ++walk->counts.commands;
            walk->counts.records += 1u + command->payload;
            i += 1u + command->payload;
        }
        ++walk->counts.chunks;
    }
}

// Checks a pass's label, targets, query sets and declared resources,
// then walks its commands.
static void WalkPass(Walk* walk, const mrhiDriverPass* pass)
{
    const mrhiDriverFrame* frame = walk->frame;
    WALK_CHECK(pass->id.index1 != 0 && mrhiIsLabelValid(pass->label, pass->labelLength) &&
               pass->colorTargetCount <= MRHI_COLOR_TARGETS);
    for (uint32_t i = 0; i < pass->colorTargetCount; ++i)
    {
        WALK_CHECK(Names(walk, pass->colorTargets[i].resource.index1, false));
    }
    WALK_CHECK(pass->depthTarget.resource.index1 <= frame->resourceCount);
    WALK_CHECK(pass->occlusionSet == 0 || IsHandle(walk, pass->occlusionSet));
    WALK_CHECK(pass->timestampSet == 0 || IsHandle(walk, pass->timestampSet));
    WALK_CHECK(pass->heap == 0 || IsHandle(walk, pass->heap));
    for (uint32_t i = 0; i < pass->accessCount; ++i)
    {
        WALK_CHECK(pass->accesses[i].resource != 0 &&
                   pass->accesses[i].resource <= frame->resourceCount &&
                   pass->accesses[i].state <= mrhi_stateSealed);
    }
    walk->counts.labeled += pass->labelLength > 0 ? 1 : 0;
    walk->counts.occlusionPasses += pass->occlusionSet != 0 ? 1 : 0;
    walk->counts.timestampPasses += pass->timestampSet != 0 ? 1 : 0;
    walk->counts.accesses += pass->accessCount;
    walk->counts.labelBytes += pass->labelLength;
    WalkCommands(walk, pass);
}

// Checks a resource: a device object's or swapchain's handle, a surface
// image's image, or a transient's place in the frame's memory.
static void CheckResource(Walk* walk, const mrhiDriverResource* resource)
{
    bool transient =
        resource->kind == mrhiDriverTransientTexture || resource->kind == mrhiDriverTransientBuffer;
    bool image = resource->kind == mrhiDriverSurfaceImage;
    WALK_CHECK(resource->kind <= mrhiDriverSurfaceImage &&
               (image ? IsHandle(walk, resource->image) : resource->image == 0) &&
               (resource->texture != nullptr) == !IsBuffer(resource->kind) &&
               (resource->size > 0) == IsBuffer(resource->kind));
    if (transient)
    {
        WALK_CHECK(resource->handle == 0 && resource->memoryOffset <= walk->frame->memoryBytes &&
                   resource->memoryBytes <= walk->frame->memoryBytes - resource->memoryOffset);
    }
    else
    {
        WALK_CHECK(IsHandle(walk, resource->handle));
    }
    walk->counts.needed += resource->needed ? 1 : 0;
    walk->counts.sealed += resource->sealed ? 1 : 0;
    walk->counts.transients += transient ? 1 : 0;
    walk->counts.presented += image ? 1 : 0;
}

uint64_t mrhiWalkFrame(const mrhiDriverFrame* frame, mrhiHandleCheck isHandle, const void* handles,
                       mrhiTestFrameLog* log)
{
    Walk state = {.frame = frame, .isHandle = isHandle, .handles = handles};
    Walk* walk = &state;
    for (uint32_t i = 0; i < frame->resourceCount; ++i)
    {
        CheckResource(walk, &frame->resources[i]);
    }
    for (uint32_t i = 0; i < frame->passCount; ++i)
    {
        WalkPass(walk, &frame->passes[i]);
    }
    // Every chunk the frame took belongs to a kept pass.
    WALK_CHECK(walk->counts.chunks == frame->chunkCount);
    for (size_t i = 0; i < frame->barrierCount; ++i)
    {
        WALK_CHECK(frame->barriers[i].resource.index1 != 0 &&
                   frame->barriers[i].resource.index1 <= frame->resourceCount &&
                   frame->barriers[i].before <= mrhi_stateSealed &&
                   frame->barriers[i].after <= mrhi_stateSealed);
    }
    if (log != nullptr)
    {
        walk->counts.passes = frame->passCount;
        walk->counts.barriers = (uint32_t)frame->barrierCount;
        walk->counts.resources = frame->resourceCount;
        walk->counts.stagingBytes = frame->stagingBytes;
        walk->counts.memoryBytes = frame->memoryBytes;
        *log = walk->counts;
    }
    return walk->faults;
}
