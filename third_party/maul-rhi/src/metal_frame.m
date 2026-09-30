// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Metal driver's frames (mrhi-0003). A frame is recorded at
// submission, one command buffer on the device's queue: its uploads are
// copied into the slot's shared staging buffer, and its readbacks land
// in the slot's shared mirror of the readback ring, copied into the ring
// once the frame has finished. Declared resources are made for the frame
// in private memory, each its own object, and released as soon as the
// command buffer is committed, since it holds them until it finishes.
// The query sets a frame resolves are cleared to 0 before its passes, so
// a query it never writes resolves to 0. The command buffer's
// completion only signals a semaphore, which waits take with their
// deadline; polls read the command buffer's status.

#include "metal_frame.h"

#include "invariant.h"
#include "metal_encode.h"
#include "metal_resource.h"
#include "metal_surface.h"

#include <string.h>

mrhiResult mrhiMetalOpenFrames(mrhiMetalFrames* frames)
{
    @autoreleasepool
    {
        MTLDepthStencilDescriptor* descriptor =
            [[[MTLDepthStencilDescriptor alloc] init] autorelease];
        frames->noDepth = [frames->device newDepthStencilStateWithDescriptor:descriptor];
    }
    return frames->noDepth != nil ? mrhi_success : mrhi_errorPlatform;
}

static void Finish(mrhiMetalSlot* slot)
{
    if (slot->high > slot->low)
    {
        memcpy(slot->ring + slot->low, (const uint8_t*)slot->readback.contents + slot->low,
               (size_t)(slot->high - slot->low));
    }
    [slot->commands release];
    dispatch_release(slot->done);
    slot->commands = nil;
    slot->done = nullptr;
    slot->tag = 0;
}

void mrhiMetalCloseFrames(mrhiMetalFrames* frames)
{
    for (uint64_t serial = frames->reported; serial < frames->submitted; ++serial)
    {
        mrhiMetalSlot* slot = &frames->slots[serial % MRHI_METAL_FRAMES];
        [slot->commands waitUntilCompleted];
        [slot->commands release];
        dispatch_release(slot->done);
    }
    for (int i = 0; i < MRHI_METAL_FRAMES; ++i)
    {
        [frames->slots[i].staging release];
        [frames->slots[i].readback release];
    }
    [frames->noDepth release];
}

// Grows a slot's shared buffer to hold bytes: false when Metal makes
// none.
static bool Reserve(id<MTLDevice> device, id<MTLBuffer>* buffer, uint64_t bytes)
{
    if (bytes == 0 || (*buffer != nil && (*buffer).length >= bytes))
    {
        return true;
    }
    [*buffer release];
    *buffer = [device newBufferWithLength:(NSUInteger)bytes options:MTLResourceStorageModeShared];
    return *buffer != nil;
}

// Makes a declared resource for the frame, retained: nil when Metal
// makes nothing.
static id MakeTransient(id<MTLDevice> device, const mrhiDriverResource* resource)
{
    uint64_t handle = 0;
    if (resource->kind == mrhiDriverTransientBuffer)
    {
        mrhiBufferDef def = mrhiDefaultBufferDef();
        def.size = resource->size;
        def.usage = resource->usage;
        (void)mrhiMetalCreateBuffer(device, &def, &handle);
    }
    else
    {
        mrhiTextureDef def = *resource->texture;
        def.usage = resource->usage;
        (void)mrhiMetalCreateTexture(device, &def, &handle);
    }
    return handle != 0 ? mrhiMetalObject(handle) : nil;
}

// Releases the frame's declared resources, from the first up to end.
static void ReleaseTransients(mrhiMetalFrames* frames, const mrhiDriverFrame* frame, uint32_t end)
{
    for (uint32_t i = 0; i < end; ++i)
    {
        mrhiDriverResourceKind kind = frame->resources[i].kind;
        if (kind == mrhiDriverTransientBuffer || kind == mrhiDriverTransientTexture)
        {
            [frames->objects[i] release];
        }
    }
}

// Releases the frame's images once its command buffer, which presents
// them, holds them.
static void ReleaseImages(const mrhiDriverFrame* frame)
{
    for (uint32_t i = 0; i < frame->resourceCount; ++i)
    {
        if (frame->resources[i].kind == mrhiDriverSurfaceImage)
        {
            mrhiMetalReleaseImage(frame->resources[i].image);
        }
    }
}

// Names the frame's objects by slot: device objects as they are,
// declared ones made; false when Metal makes one not, with those made
// released.
static bool TakeObjects(mrhiMetalFrames* frames, const mrhiDriverFrame* frame)
{
    MRHI_ASSERT(frame->resourceCount <= frames->objectLimit);
    for (uint32_t i = 0; i < frame->resourceCount; ++i)
    {
        const mrhiDriverResource* resource = &frame->resources[i];
        bool transient = resource->kind == mrhiDriverTransientBuffer ||
                         resource->kind == mrhiDriverTransientTexture;
        if (resource->kind == mrhiDriverSurfaceImage)
        {
            frames->objects[i] = mrhiMetalImageTexture(resource->image);
            continue;
        }
        frames->objects[i] = !resource->needed ? nil
                             : transient       ? MakeTransient(frames->device, resource)
                                               : mrhiMetalObject(resource->handle);
        if (transient && resource->needed && frames->objects[i] == nil)
        {
            ReleaseTransients(frames, frame, i);
            return false;
        }
    }
    return true;
}

// Clears a query set, opening the blit encoder that clears the frame's
// sets on the first.
static void ClearSet(id<MTLCommandBuffer> commands, id<MTLBlitCommandEncoder>* blit, uint64_t set)
{
    if (*blit == nil)
    {
        *blit = [commands blitCommandEncoder];
    }
    id<MTLBuffer> results = mrhiMetalObject(set);
    [*blit fillBuffer:results range:NSMakeRange(0, results.length) value:0];
}

// Clears every query set the frame resolves: a query the frame has not
// written reads 0, and a set's results are seen only through a resolve.
static void ClearQueries(id<MTLCommandBuffer> commands, const mrhiDriverFrame* frame)
{
    id<MTLBlitCommandEncoder> blit = nil;
    for (uint32_t p = 0; p < frame->passCount; ++p)
    {
        const mrhiDriverPass* pass = &frame->passes[p];
        for (uint32_t chunk = pass->firstChunk; chunk != 0; chunk = frame->chunks[chunk - 1].next)
        {
            const mrhiCommandChunk* at = &frame->chunks[chunk - 1];
            for (uint32_t i = 0; i < at->count; i += 1u + at->commands[i].payload)
            {
                if (at->commands[i].type == mrhiCommandResolveQueries)
                {
                    ClearSet(commands, &blit, at->commands[i].b);
                }
            }
        }
    }
    [blit endEncoding];
}

// Encodes the frame's passes into a command buffer and commits it, its
// completion signalling the slot's semaphore.
static void Record(mrhiMetalFrames* frames, mrhiMetalSlot* slot, const mrhiDriverFrame* frame)
{
    id<MTLCommandBuffer> commands = [frames->queue commandBuffer];
    mrhiMetalEncoder* encoder = frames->encoder;
    *encoder = (mrhiMetalEncoder){
        .frame = frame,
        .objects = frames->objects,
        .staging = slot->staging,
        .readback = slot->readback,
        .commands = commands,
        .noDepth = frames->noDepth,
        .low = UINT64_MAX,
    };
    ClearQueries(commands, frame);
    for (uint32_t p = 0; p < frame->passCount; ++p)
    {
        mrhiMetalEncodePass(encoder, &frame->passes[p]);
    }
    // Every image the frame acquired is presented after its work, used or
    // not, as the core expects.
    for (uint32_t i = 0; i < frame->resourceCount; ++i)
    {
        if (frame->resources[i].kind == mrhiDriverSurfaceImage)
        {
            mrhiMetalPresent(commands, frame->resources[i].image);
        }
    }
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    [commands addCompletedHandler:^(id<MTLCommandBuffer> finished) {
      (void)finished;
      dispatch_semaphore_signal(done);
    }];
    [commands commit];
    slot->commands = [commands retain];
    slot->done = done;
    slot->ring = frame->readbackRing;
    slot->low = encoder->low;
    slot->high = encoder->high;
}

mrhiResult mrhiMetalSubmitFrame(mrhiMetalFrames* frames, const mrhiDriverFrame* frame, uint64_t tag)
{
    mrhiMetalSlot* slot = &frames->slots[frames->submitted % MRHI_METAL_FRAMES];
    MRHI_ASSERT(frames->submitted - frames->reported < MRHI_METAL_FRAMES && slot->tag == 0);
    bool made = false;
    @autoreleasepool
    {
        made = Reserve(frames->device, &slot->staging, frame->stagingBytes) &&
               Reserve(frames->device, &slot->readback, frame->readbackBytes) &&
               TakeObjects(frames, frame);
        if (made)
        {
            if (frame->stagingBytes > 0)
            {
                memcpy(slot->staging.contents, frame->staging, (size_t)frame->stagingBytes);
            }
            Record(frames, slot, frame);
            ReleaseTransients(frames, frame, frame->resourceCount);
            ReleaseImages(frame);
        }
    }
    if (!made)
    {
        return mrhi_errorCapacity;
    }
    slot->tag = tag;
    ++frames->submitted;
    return mrhi_success;
}

// Keeps the failed command buffer's error as the loss's message.
static void Lose(mrhiMetalFrames* frames, const mrhiMetalSlot* slot)
{
    frames->lost = true;
    @autoreleasepool
    {
        const char* text = slot->commands.error.localizedDescription.UTF8String;
        size_t length = text != nullptr ? strlen(text) : 0;
        length = length < MRHI_LOSS_MESSAGE_BYTES ? length : MRHI_LOSS_MESSAGE_BYTES;
        while (length > 0 && length < strlen(text) && ((unsigned char)text[length] & 0xC0) == 0x80)
        {
            --length;
        }
        if (length > 0)
        {
            memcpy(frames->message, text, length);
        }
        frames->messageLength = (uint32_t)length;
    }
}

size_t mrhiMetalPollFrames(mrhiMetalFrames* frames, mrhiDriverEvent* events, size_t capacity)
{
    size_t moved = 0;
    while (moved < capacity && !frames->lost && frames->reported < frames->submitted)
    {
        mrhiMetalSlot* slot = &frames->slots[frames->reported % MRHI_METAL_FRAMES];
        MTLCommandBufferStatus status = slot->commands.status;
        if (status == MTLCommandBufferStatusError)
        {
            Lose(frames, slot);
            events[moved++] = (mrhiDriverEvent){.tag = 0, .outcome = mrhi_errorDeviceLost};
            break;
        }
        if (status != MTLCommandBufferStatusCompleted)
        {
            break;
        }
        events[moved++] = (mrhiDriverEvent){.tag = slot->tag, .outcome = mrhi_success};
        Finish(slot);
        ++frames->reported;
    }
    return moved;
}

bool mrhiMetalWaitFrame(mrhiMetalFrames* frames, uint64_t tag, uint64_t timeoutNs)
{
    for (uint64_t serial = frames->reported; serial < frames->submitted; ++serial)
    {
        mrhiMetalSlot* slot = &frames->slots[serial % MRHI_METAL_FRAMES];
        if (slot->tag != tag)
        {
            continue;
        }
        dispatch_time_t deadline = timeoutNs >= (uint64_t)INT64_MAX
                                       ? DISPATCH_TIME_FOREVER
                                       : dispatch_time(DISPATCH_TIME_NOW, (int64_t)timeoutNs);
        if (dispatch_semaphore_wait(slot->done, deadline) != 0)
        {
            return false;
        }
        // Signalled again, so that later waits for the frame pass.
        dispatch_semaphore_signal(slot->done);
        return true;
    }
    return true;
}
