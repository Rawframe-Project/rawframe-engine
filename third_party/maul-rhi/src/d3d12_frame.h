// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A D3D12 device's frames (mrhi-0003, mrhi-0013): a slot per frame in
// flight with its command allocator and list, its mapped staging, the
// descriptor heaps its targets' views and its bindings take, its
// scratch buffer, its transients with the heap they are placed in, and
// the readbacks it fills; the fence
// whose value counts the frames finished; the readback buffer, the
// command signatures of indirect work and the zeros of unwritten
// queries; the recorder; and the queue of destroyed objects, released
// once the next frame submitted after their destruction finishes.
// Included by the driver's files only.

#ifndef MAUL_RHI_SRC_D3D12_FRAME_H
#define MAUL_RHI_SRC_D3D12_FRAME_H

#include "d3d12_pipeline.h"
#include "d3d12_state.h"
#include "d3d12_swapchain.h"

#include "maul-rhi/device.h"

// A destroyed object or pipeline waiting to be released: the frame
// whose end releases it, the frames submitted when it was destroyed
// plus one.
typedef struct mrhiD3d12Retiree
{
    uint64_t serial;
    uint64_t handle;
    mrhiD3d12Kind kind;
} mrhiD3d12Retiree;

typedef struct mrhiD3d12Slot
{
    ID3D12CommandAllocator* allocator;
    ID3D12GraphicsCommandList* list;
    // The core's tag while its frame runs; 0 when idle.
    uint64_t tag;
    // The frame's serial, the fence value its end signals.
    uint64_t serial;
    ID3D12Resource* staging;
    uint8_t* stagingBytes;
    ID3D12DescriptorHeap* targetHeap;
    ID3D12DescriptorHeap* depthHeap;
    // The shader-visible heaps its bindings take, after the program's
    // heaps' regions.
    ID3D12DescriptorHeap* viewHeap;
    ID3D12DescriptorHeap* samplerHeap;
    // Where indirect draws setting the vertex information copy their
    // arguments; made at its first use.
    ID3D12Resource* scratch;
    // The heap its transients are placed in, kept while large enough,
    // with its bytes and whether it takes multisampled textures.
    ID3D12Heap* heap;
    uint64_t heapBytes;
    bool heapSamples;
    // The frame's transients by frame slot, null where it has none.
    ID3D12Resource** transients;
    uint32_t transientCount;
    // The core's ring and the ranges of it the frame fills.
    uint8_t* ring;
    mrhiD3d12Range* readbacks;
    uint32_t readbackCount;
} mrhiD3d12Slot;

typedef struct mrhiD3d12Frames
{
    ID3D12Device* device;
    ID3D12CommandQueue* queue;
    mrhiD3d12Objects* objects;
    mrhiD3d12Pipelines* pipelines;
    mrhiD3d12Swapchains* swapchains;
    ID3D12Fence* fence;
    HANDLE event;
    mrhiD3d12Slot* slots;
    uint32_t slotCount;
    // The frame being recorded's objects, by frame slot less one.
    mrhiD3d12Object* table;
    // The program's heaps: how many, and each one's resource and sampler
    // descriptors, the regions at the front of every slot's
    // shader-visible heaps; set before the frames are laid out.
    uint32_t heapCount;
    uint32_t heapEntries;
    uint32_t heapSamplers;
    uint32_t resourceLimit;
    uint32_t targetLimit;
    uint32_t depthLimit;
    uint32_t viewLimit;
    uint32_t samplerLimit;
    uint32_t readbackLimit;
    uint64_t uploadBytes;
    uint64_t scratchBytes;
    ID3D12Resource* readback;
    uint8_t* readbackBytes;
    uint64_t readbackSize;
    ID3D12CommandSignature* signatures[mrhiD3d12IndirectCount];
    ID3D12Resource* zeros;
    uint64_t zeroBytes;
    // The frame being recorded's state, kept here for its size.
    mrhiD3d12Recorder recorder;
    // Frames submitted, and frames reported finished.
    uint64_t submitted;
    uint64_t finished;
    mrhiD3d12Retiree* retirees;
    uint32_t retireFirst;
    uint32_t retireCount;
    uint32_t retireCapacity;
    // Whether transients are placed as the core places them, in one heap
    // of buffers and textures, which resource heap tier 2 allows; on tier
    // 1 each is committed.
    bool placing;
    // Why D3D12 removed the device, once it has.
    HRESULT removed;
    bool lost;
} mrhiD3d12Frames;

// Where the frames' tables lie in a device's block.
typedef struct mrhiD3d12FrameRoom
{
    size_t slots;
    size_t table;
    size_t transients;
    size_t readbacks;
    size_t retirees;
    uint32_t retireCapacity;
} mrhiD3d12FrameRoom;

// The descriptors of each kind a slot's rings keep at least beside the
// program's heaps.
#define MRHI_D3D12_RING_FLOOR 64

// Whether count heaps of entries and samplers each leave every slot's
// rings their floor.
bool mrhiD3d12HeapsFit(uint32_t count, uint32_t entries, uint32_t samplers);

// Adds the tables a device's limits need to its layout, and sets them
// up in its block.
mrhiD3d12FrameRoom mrhiD3d12PlanFrames(mrhiLayout* layout, const mrhiDeviceLimits* limits,
                                       uint32_t slots);
void mrhiD3d12LayFrames(mrhiD3d12Frames* frames, unsigned char* block,
                        const mrhiD3d12FrameRoom* room, const mrhiDeviceLimits* limits,
                        uint32_t slots);

// Makes the fence, each slot's allocator, list, staging and descriptor
// heaps, and the readback buffer: success, or mrhi_errorCapacity when
// D3D12 makes one of them not.
mrhiResult mrhiD3d12OpenFrames(mrhiD3d12Frames* frames);

// Waits for every frame, then releases everything the frames hold, the
// retiring objects included.
void mrhiD3d12CloseFrames(mrhiD3d12Frames* frames);

// Records and submits a frame, then presents its surface images:
// success, mrhi_errorCapacity when D3D12 makes no transient or a ring
// runs out, or mrhi_errorDeviceLost.
mrhiResult mrhiD3d12Submit(mrhiD3d12Frames* frames, const mrhiDriverFrame* frame, uint64_t tag);

// Moves up to capacity finished frames into events, in order, their
// readbacks filled and their transients and the objects they retire
// gone; or reports the device's loss with tag 0.
size_t mrhiD3d12PollFrames(mrhiD3d12Frames* frames, mrhiDriverEvent* events, size_t capacity);

// Waits for every frame submitted to finish, or the device to be lost.
void mrhiD3d12WaitIdle(mrhiD3d12Frames* frames);

// Waits up to timeoutNs for a running frame: whether it finished.
bool mrhiD3d12WaitFrame(mrhiD3d12Frames* frames, uint64_t tag, uint64_t timeoutNs);

// Releases an object or pipeline once the next frame submitted
// finishes.
void mrhiD3d12RetireLater(mrhiD3d12Frames* frames, mrhiD3d12Kind kind, uint64_t handle);

// What is known of the device's loss.
void mrhiD3d12LossReport(const mrhiD3d12Frames* frames, mrhiDeviceLossReport* reportOut);

#endif // MAUL_RHI_SRC_D3D12_FRAME_H
