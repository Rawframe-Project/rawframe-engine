// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Metal driver's frames (mrhi-0003): each submitted frame recorded
// into a command buffer and committed, then polled for completion in
// order. Included by the driver's Objective-C files only.

#ifndef MAUL_RHI_SRC_METAL_FRAME_H
#define MAUL_RHI_SRC_METAL_FRAME_H

#include "metal_state.h"

#include "maul-rhi/frame.h"

#include <stdatomic.h>

// The frames a device lets run at once.
#define MRHI_METAL_FRAMES 3

// A running frame: its tag, command buffer and the semaphore its
// completion signals; its staging and readback buffers, kept for the
// frames that use the slot after it; and the core's readback ring, with
// the range the frame writes.
typedef struct mrhiMetalSlot
{
    uint64_t tag;
    // The command buffers the frame committed, retained, in order: its
    // own, with each native pass's between them (mrhi-0019). The frame is
    // done when all have completed; the last completion handler to run
    // signals done.
    id<MTLCommandBuffer> runs[1 + 2 * MRHI_NATIVE_PASSES];
    uint32_t runCount;
    _Atomic uint32_t running;
    dispatch_semaphore_t done;
    id<MTLBuffer> staging;
    id<MTLBuffer> readback;
    // The records counted multi-draws draw from, clamped (mrhi-0020).
    id<MTLBuffer> clamped;
    uint8_t* ring;
    uint64_t low;
    uint64_t high;
} mrhiMetalSlot;

typedef struct mrhiMetalFrames
{
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    // The depth and stencil state of a pipeline without one.
    id<MTLDepthStencilState> noDepth;
    // Whether the device draws counted multi-draws, and the kernel that
    // clamps their records (mrhi-0020), made when it does.
    bool counted;
    id<MTLComputePipelineState> clamp;
    mrhiMetalSlot slots[MRHI_METAL_FRAMES];
    uint64_t submitted;
    uint64_t reported;
    // Room for a frame's objects, as many as a frame has resources.
    id* objects;
    uint32_t objectLimit;
    mrhiMetalEncoder* encoder;
    bool lost;
    char message[MRHI_LOSS_MESSAGE_BYTES];
    uint32_t messageLength;
} mrhiMetalFrames;

// Starts the frames of a device: success, or mrhi_errorPlatform when
// Metal makes no depth and stencil state.
mrhiResult mrhiMetalOpenFrames(mrhiMetalFrames* frames);

// Waits for every running frame and releases what the frames keep.
void mrhiMetalCloseFrames(mrhiMetalFrames* frames);

// Records and commits a frame: success, or mrhi_errorCapacity when Metal
// makes no buffer or transient the frame needs.
mrhiResult mrhiMetalSubmitFrame(mrhiMetalFrames* frames, const mrhiDriverFrame* frame,
                                uint64_t tag);

// Moves finished frames into events in order, their readbacks copied
// into the ring first; a failed command buffer is the device's loss.
size_t mrhiMetalPollFrames(mrhiMetalFrames* frames, mrhiDriverEvent* events, size_t capacity);

// Waits up to timeoutNs for a frame; true when it has finished.
bool mrhiMetalWaitFrame(mrhiMetalFrames* frames, uint64_t tag, uint64_t timeoutNs);

#endif // MAUL_RHI_SRC_METAL_FRAME_H
