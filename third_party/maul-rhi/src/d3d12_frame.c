// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A D3D12 device's frames (d3d12_frame.h). Frames run in submission
// order on one queue, each signalling the fence with its serial, so a
// poll reads one counter to know every frame finished, and a slot is
// reused only once the core has been told its frame finished. A frame's
// transients are made at its submit and released when it finishes:
// placed where the core put them in the slot's heap, which grows as
// frames need, or committed on their own where the heap's tier cannot
// mix buffers and textures, or D3D12 makes no heap.

#include "d3d12_frame.h"

#include "allocator.h"
#include "d3d12_barrier.h"
#include "d3d12_record.h"
#include "invariant.h"

#include "generated/d3d12_expand.h"

#include <stdalign.h>
#include <string.h>

// Makes a buffer in an upload or readback heap, in the state that heap
// keeps, and maps it: nullptr when D3D12 makes none.
static ID3D12Resource* MakeMapped(ID3D12Device* device, D3D12_HEAP_TYPE type, uint64_t size,
                                  uint8_t** bytesOut)
{
    const D3D12_HEAP_PROPERTIES heap = {.Type = type};
    const D3D12_RESOURCE_DESC desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Width = size,
        .Height = 1,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .SampleDesc = {.Count = 1},
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
    };
    D3D12_RESOURCE_STATES state = type == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ
                                                                 : D3D12_RESOURCE_STATE_COPY_DEST;
    ID3D12Resource* resource = nullptr;
    if (FAILED(ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                    state, nullptr, &IID_ID3D12Resource,
                                                    (void**)&resource)))
    {
        return nullptr;
    }
    // The host never reads an upload buffer, and reads the readback
    // buffer whole.
    const D3D12_RANGE none = {0, 0};
    void* mapped = nullptr;
    if (FAILED(ID3D12Resource_Map(resource, 0, type == D3D12_HEAP_TYPE_UPLOAD ? &none : nullptr,
                                  &mapped)))
    {
        ID3D12Resource_Release(resource);
        return nullptr;
    }
    *bytesOut = mapped;
    return resource;
}

static ID3D12DescriptorHeap* MakeHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                                      uint32_t count, bool visible)
{
    const D3D12_DESCRIPTOR_HEAP_DESC desc = {
        .Type = type,
        .NumDescriptors = count,
        .Flags =
            visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
    };
    ID3D12DescriptorHeap* heap = nullptr;
    return SUCCEEDED(ID3D12Device_CreateDescriptorHeap(device, &desc, &IID_ID3D12DescriptorHeap,
                                                       (void**)&heap))
               ? heap
               : nullptr;
}

// The descriptors a shader-visible heap of total leaves its ring past
// count regions of each: 0 when they fill it.
static uint32_t RingRoom(uint32_t total, uint32_t count, uint32_t each)
{
    uint64_t regions = (uint64_t)count * each;
    return regions < total ? total - (uint32_t)regions : 0;
}

bool mrhiD3d12HeapsFit(uint32_t count, uint32_t entries, uint32_t samplers)
{
    return RingRoom(D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1, count, entries) >=
               MRHI_D3D12_RING_FLOOR &&
           RingRoom(D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE, count, samplers) >=
               MRHI_D3D12_RING_FLOOR;
}

mrhiD3d12FrameRoom mrhiD3d12PlanFrames(mrhiLayout* layout, const mrhiDeviceLimits* limits,
                                       uint32_t slots)
{
    mrhiD3d12FrameRoom room = {0};
    room.slots = mrhiLayoutAdd(layout, slots, sizeof(mrhiD3d12Slot), alignof(mrhiD3d12Slot));
    room.table = mrhiLayoutAdd(layout, limits->frameResources, sizeof(mrhiD3d12Object),
                               alignof(mrhiD3d12Object));
    room.transients = mrhiLayoutAdd(layout, (size_t)slots * limits->frameResources,
                                    sizeof(ID3D12Resource*), alignof(ID3D12Resource*));
    room.readbacks = mrhiLayoutAdd(layout, (size_t)slots * limits->readbacks,
                                   sizeof(mrhiD3d12Range), alignof(mrhiD3d12Range));
    uint64_t retirees = (uint64_t)limits->buffers + limits->textures + limits->views +
                        limits->samplers + limits->querySets + limits->pipelines + limits->heaps;
    room.retireCapacity = retirees < UINT32_MAX ? (uint32_t)retirees : UINT32_MAX;
    room.retirees = mrhiLayoutAdd(layout, room.retireCapacity, sizeof(mrhiD3d12Retiree),
                                  alignof(mrhiD3d12Retiree));
    return room;
}

void mrhiD3d12LayFrames(mrhiD3d12Frames* frames, unsigned char* block,
                        const mrhiD3d12FrameRoom* room, const mrhiDeviceLimits* limits,
                        uint32_t slots)
{
    frames->slots = (mrhiD3d12Slot*)(block + room->slots);
    frames->slotCount = slots;
    frames->table = (mrhiD3d12Object*)(block + room->table);
    frames->resourceLimit = limits->frameResources;
    // Each pass takes a view of each color target and one of its depth
    // target at most.
    frames->targetLimit = limits->framePasses * MRHI_COLOR_TARGETS;
    frames->depthLimit = limits->framePasses;
    // Each binding of a table is a record of the frame's commands, and
    // takes one descriptor at most; a slot's rings take what its
    // shader-visible heaps hold past the program's heaps at most.
    uint32_t records = limits->frameCommandBytes / (uint32_t)sizeof(mrhiCommand);
    records = records > 0 ? records : 1;
    uint32_t views = RingRoom(D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1,
                              frames->heapCount, frames->heapEntries);
    uint32_t samplers = RingRoom(D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE, frames->heapCount,
                                 frames->heapSamplers);
    frames->viewLimit = records < views ? records : views;
    frames->samplerLimit = records < samplers ? records : samplers;
    // Each indirect draw is a record, larger than the arguments it
    // copies; each counted draw expanded takes an indexed draw's
    // arguments and the vertex information.
    frames->scratchBytes = limits->frameCommandBytes;
    if (frames->counted)
    {
        frames->scratchBytes += (uint64_t)limits->frameIndirectDraws *
                                (sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) + 2 * sizeof(uint32_t));
    }
    frames->readbackLimit = limits->readbacks;
    frames->uploadBytes = limits->frameUploadBytes;
    frames->readbackSize = limits->readbackBytes;
    frames->retirees = (mrhiD3d12Retiree*)(block + room->retirees);
    frames->retireCapacity = room->retireCapacity;
    ID3D12Resource** transients = (ID3D12Resource**)(block + room->transients);
    mrhiD3d12Range* readbacks = (mrhiD3d12Range*)(block + room->readbacks);
    for (uint32_t i = 0; i < slots; ++i)
    {
        frames->slots[i] = (mrhiD3d12Slot){
            .transients = transients + (size_t)i * limits->frameResources,
            .readbacks = readbacks + (size_t)i * limits->readbacks,
        };
    }
}

static bool OpenSlot(mrhiD3d12Frames* frames, mrhiD3d12Slot* slot)
{
    ID3D12Device* device = frames->device;
    if (FAILED(ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   &IID_ID3D12CommandAllocator,
                                                   (void**)&slot->allocator)))
    {
        return false;
    }
    for (uint32_t i = 0; i < 1 + MRHI_NATIVE_PASSES; ++i)
    {
        if (FAILED(ID3D12Device_CreateCommandList(
                device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot->allocator, nullptr,
                &IID_ID3D12GraphicsCommandList, (void**)&slot->parts[i])) ||
            FAILED(ID3D12GraphicsCommandList_Close(slot->parts[i])))
        {
            return false;
        }
    }
    slot->targetHeap = MakeHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                                frames->targetLimit > 0 ? frames->targetLimit : 1, false);
    slot->depthHeap = MakeHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                               frames->depthLimit > 0 ? frames->depthLimit : 1, false);
    slot->viewHeap = MakeHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                              frames->heapCount * frames->heapEntries + frames->viewLimit, true);
    slot->samplerHeap =
        MakeHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                 frames->heapCount * frames->heapSamplers + frames->samplerLimit, true);
    if (frames->uploadBytes > 0)
    {
        slot->staging =
            MakeMapped(device, D3D12_HEAP_TYPE_UPLOAD, frames->uploadBytes, &slot->stagingBytes);
    }
    return slot->targetHeap != nullptr && slot->depthHeap != nullptr && slot->viewHeap != nullptr &&
           slot->samplerHeap != nullptr && (frames->uploadBytes == 0 || slot->staging != nullptr);
}

// The command signature of an indirect draw or dispatch reading its
// arguments alone: nullptr when D3D12 makes none.
static ID3D12CommandSignature* SignatureOf(ID3D12Device* device, mrhiD3d12Indirect kind)
{
    static const D3D12_INDIRECT_ARGUMENT_TYPE types[] = {
        [mrhiD3d12IndirectDraw] = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW,
        [mrhiD3d12IndirectDrawIndexed] = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED,
        [mrhiD3d12IndirectDispatch] = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH,
    };
    static const UINT strides[] = {
        [mrhiD3d12IndirectDraw] = sizeof(D3D12_DRAW_ARGUMENTS),
        [mrhiD3d12IndirectDrawIndexed] = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS),
        [mrhiD3d12IndirectDispatch] = sizeof(D3D12_DISPATCH_ARGUMENTS),
    };
    const D3D12_INDIRECT_ARGUMENT_DESC argument = {.Type = types[kind]};
    const D3D12_COMMAND_SIGNATURE_DESC desc = {
        .ByteStride = strides[kind],
        .NumArgumentDescs = 1,
        .pArgumentDescs = &argument,
    };
    ID3D12CommandSignature* signature = nullptr;
    return SUCCEEDED(ID3D12Device_CreateCommandSignature(
               device, &desc, nullptr, &IID_ID3D12CommandSignature, (void**)&signature))
               ? signature
               : nullptr;
}

// Makes the kernel expanding counted draws' records, its root signature
// embedded in its DXIL.
static bool MakeExpand(mrhiD3d12Frames* frames)
{
    if (FAILED(ID3D12Device_CreateRootSignature(
            frames->device, 0, mrhiD3d12ExpandDxil, sizeof(mrhiD3d12ExpandDxil),
            &IID_ID3D12RootSignature, (void**)&frames->expandRoot)))
    {
        return false;
    }
    const D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {
        .pRootSignature = frames->expandRoot,
        .CS = {mrhiD3d12ExpandDxil, sizeof(mrhiD3d12ExpandDxil)},
    };
    return SUCCEEDED(ID3D12Device_CreateComputePipelineState(
        frames->device, &desc, &IID_ID3D12PipelineState, (void**)&frames->expand));
}

// Makes the command signatures, the kernel of a device drawing counted
// multi-draws and the zeros resolves and clears copy, which D3D12 zeroes
// as it commits them.
static bool OpenShared(mrhiD3d12Frames* frames)
{
    bool made = true;
    for (int i = 0; i < mrhiD3d12IndirectCount; ++i)
    {
        frames->signatures[i] = SignatureOf(frames->device, (mrhiD3d12Indirect)i);
        made = made && frames->signatures[i] != nullptr;
    }
    if (made && frames->counted)
    {
        made = MakeExpand(frames);
    }
    if (made)
    {
        const mrhiBufferDef def = {.size = MRHI_D3D12_ZERO_BYTES};
        frames->zeros = mrhiD3d12CommitBuffer(frames->objects, &def);
        made = frames->zeros != nullptr;
    }
    return made;
}

mrhiResult mrhiD3d12OpenFrames(mrhiD3d12Frames* frames)
{
    if (FAILED(ID3D12Device_CreateFence(frames->device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence,
                                        (void**)&frames->fence)))
    {
        return mrhi_errorCapacity;
    }
    frames->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {0};
    frames->placing =
        SUCCEEDED(ID3D12Device_CheckFeatureSupport(frames->device, D3D12_FEATURE_D3D12_OPTIONS,
                                                   &options, sizeof(options))) &&
        options.ResourceHeapTier >= D3D12_RESOURCE_HEAP_TIER_2;
    bool made = frames->event != nullptr && OpenShared(frames);
    for (uint32_t i = 0; i < frames->slotCount && made; ++i)
    {
        made = OpenSlot(frames, &frames->slots[i]);
    }
    if (made && frames->readbackSize > 0)
    {
        frames->readback = MakeMapped(frames->device, D3D12_HEAP_TYPE_READBACK,
                                      frames->readbackSize, &frames->readbackBytes);
        made = frames->readback != nullptr;
    }
    return made ? mrhi_success : mrhi_errorCapacity;
}

// Releases a slot's transients.
static void DropTransients(mrhiD3d12Slot* slot)
{
    for (uint32_t i = 0; i < slot->transientCount; ++i)
    {
        if (slot->transients[i] != nullptr)
        {
            ID3D12Resource_Release(slot->transients[i]);
            slot->transients[i] = nullptr;
        }
    }
    slot->transientCount = 0;
}

static void Retire(mrhiD3d12Frames* frames, const mrhiD3d12Retiree* retiree)
{
    if (retiree->kind == mrhiD3d12KindPipeline)
    {
        mrhiD3d12ReleasePipeline(frames->pipelines, retiree->handle);
    }
    else
    {
        mrhiD3d12ReleaseObject(frames->objects, retiree->kind, retiree->handle);
    }
}

// Releases the objects whose frame has finished: all of them with no
// frame left to wait for.
static void RetireUpTo(mrhiD3d12Frames* frames, uint64_t serial)
{
    while (frames->retireCount > 0 && frames->retirees[frames->retireFirst].serial <= serial)
    {
        Retire(frames, &frames->retirees[frames->retireFirst]);
        frames->retireFirst = (frames->retireFirst + 1) % frames->retireCapacity;
        --frames->retireCount;
    }
}

void mrhiD3d12RetireLater(mrhiD3d12Frames* frames, mrhiD3d12Kind kind, uint64_t handle)
{
    // Every handle waits at most once, so the queue never fills.
    MRHI_ASSERT(frames->retireCount < frames->retireCapacity);
    uint32_t at = (frames->retireFirst + frames->retireCount) % frames->retireCapacity;
    frames->retirees[at] =
        (mrhiD3d12Retiree){.serial = frames->submitted + 1, .handle = handle, .kind = kind};
    ++frames->retireCount;
}

// Waits, without end, until the fence reaches a value or the device is
// removed.
static void WaitFor(mrhiD3d12Frames* frames, uint64_t serial)
{
    if (ID3D12Fence_GetCompletedValue(frames->fence) >= serial)
    {
        return;
    }
    (void)ResetEvent(frames->event);
    if (SUCCEEDED(ID3D12Fence_SetEventOnCompletion(frames->fence, serial, frames->event)))
    {
        (void)WaitForSingleObject(frames->event, INFINITE);
    }
}

// Releases a COM object, if there is one.
static void Drop(void* object)
{
    if (object != nullptr)
    {
        IUnknown_Release((IUnknown*)object);
    }
}

void mrhiD3d12WaitIdle(mrhiD3d12Frames* frames)
{
    WaitFor(frames, frames->submitted);
}

void mrhiD3d12CloseFrames(mrhiD3d12Frames* frames)
{
    if (frames->fence != nullptr && frames->event != nullptr)
    {
        WaitFor(frames, frames->submitted);
    }
    RetireUpTo(frames, UINT64_MAX);
    for (uint32_t i = 0; i < frames->slotCount; ++i)
    {
        mrhiD3d12Slot* slot = &frames->slots[i];
        DropTransients(slot);
        Drop(slot->heap);
        Drop(slot->scratch);
        Drop(slot->staging);
        Drop(slot->targetHeap);
        Drop(slot->depthHeap);
        Drop(slot->viewHeap);
        Drop(slot->samplerHeap);
        for (uint32_t part = 0; part < 1 + MRHI_NATIVE_PASSES; ++part)
        {
            Drop(slot->parts[part]);
        }
        Drop(slot->allocator);
    }
    for (int i = 0; i < mrhiD3d12IndirectCount; ++i)
    {
        Drop(frames->signatures[i]);
    }
    Drop(frames->expand);
    Drop(frames->expandRoot);
    Drop(frames->zeros);
    Drop(frames->readback);
    if (frames->event != nullptr)
    {
        (void)CloseHandle(frames->event);
    }
    Drop(frames->fence);
}

// The slot's heap, made anew for a frame whose placed transients need
// more bytes or take multisampled textures: nullptr when there is none.
static ID3D12Heap* Reserve(const mrhiD3d12Frames* frames, mrhiD3d12Slot* slot,
                           const mrhiDriverFrame* frame)
{
    bool samples = false;
    for (uint32_t i = 0; i < frame->resourceCount; ++i)
    {
        const mrhiDriverResource* resource = &frame->resources[i];
        samples = samples || (resource->kind == mrhiDriverTransientTexture && resource->needed &&
                              resource->memoryBytes > 0 && resource->texture->sampleCount > 1);
    }
    if (slot->heap != nullptr && slot->heapBytes >= frame->memoryBytes &&
        (slot->heapSamples || !samples))
    {
        return slot->heap;
    }
    Drop(slot->heap);
    slot->heap = nullptr;
    slot->heapBytes = 0;
    uint64_t alignment = samples ? D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT
                                 : D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    const D3D12_HEAP_DESC desc = {
        .SizeInBytes = (frame->memoryBytes + alignment - 1) & ~(alignment - 1),
        .Properties = {.Type = D3D12_HEAP_TYPE_DEFAULT},
        .Alignment = alignment,
    };
    if (SUCCEEDED(
            ID3D12Device_CreateHeap(frames->device, &desc, &IID_ID3D12Heap, (void**)&slot->heap)))
    {
        slot->heapBytes = desc.SizeInBytes;
        slot->heapSamples = samples;
    }
    return slot->heap;
}

// Makes a transient, placed in the heap where the core put it when
// there is one, else committed.
static void MakeTransient(const mrhiD3d12Frames* frames, ID3D12Heap* heap,
                          const mrhiDriverResource* resource, mrhiD3d12Object* object)
{
    bool placed = heap != nullptr && resource->memoryBytes > 0;
    if (resource->kind == mrhiDriverTransientBuffer)
    {
        const mrhiBufferDef def = {.size = resource->size, .usage = resource->usage};
        object->resource =
            placed ? mrhiD3d12PlaceBuffer(frames->objects, heap, resource->memoryOffset, &def)
                   : mrhiD3d12CommitBuffer(frames->objects, &def);
        return;
    }
    mrhiTextureDef def = *resource->texture;
    def.usage = resource->usage;
    object->resource =
        placed ? mrhiD3d12PlaceTexture(frames->objects, heap, resource->memoryOffset, &def)
               : mrhiD3d12CommitTexture(frames->objects, &def);
    object->texture = resource->texture;
    object->discard = placed && (resource->usage & mrhi_textureRenderTarget) != 0;
}

// Makes a frame's transients in its slot and fills the frame's table:
// whether D3D12 made them all.
static bool TakeObjects(const mrhiD3d12Frames* frames, mrhiD3d12Slot* slot,
                        const mrhiDriverFrame* frame)
{
    MRHI_ASSERT(frame->resourceCount <= frames->resourceLimit);
    ID3D12Heap* heap =
        frames->placing && frame->memoryBytes > 0 ? Reserve(frames, slot, frame) : nullptr;
    slot->transientCount = frame->resourceCount;
    bool made = true;
    for (uint32_t i = 0; i < frame->resourceCount; ++i)
    {
        const mrhiDriverResource* resource = &frame->resources[i];
        mrhiD3d12Object* object = &frames->table[i];
        *object = (mrhiD3d12Object){.state = D3D12_RESOURCE_STATE_COMMON,
                                    .initial = D3D12_RESOURCE_STATE_COMMON};
        slot->transients[i] = nullptr;
        if (!resource->needed || !made)
        {
            continue;
        }
        switch (resource->kind)
        {
        case mrhiDriverTransientTexture:
        case mrhiDriverTransientBuffer:
            MakeTransient(frames, heap, resource, object);
            slot->transients[i] = object->resource;
            break;
        case mrhiDriverDeviceTexture:
        {
            const mrhiD3d12Texture* texture = &frames->objects->textures[resource->handle - 1];
            object->resource = texture->resource;
            object->texture = &texture->def;
            break;
        }
        case mrhiDriverDeviceBuffer:
            object->resource = frames->objects->buffers[resource->handle - 1].resource;
            break;
        default:
            MRHI_ASSERT(resource->kind == mrhiDriverSurfaceImage);
            object->resource =
                mrhiD3d12ImageOf(frames->swapchains, resource->handle, resource->image);
            object->texture = resource->texture;
            break;
        }
        made = object->resource != nullptr;
    }
    return made;
}

static mrhiD3d12Ring RingOf(ID3D12Device* device, ID3D12DescriptorHeap* heap,
                            D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity)
{
    D3D12_CPU_DESCRIPTOR_HANDLE start;
    (void)ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap, &start);
    return (mrhiD3d12Ring){
        .start = start,
        .step = ID3D12Device_GetDescriptorHandleIncrementSize(device, type),
        .capacity = capacity,
    };
}

// A slot's shader-visible ring, after the regions of the program's
// heaps.
static mrhiD3d12GpuRing GpuRingOf(ID3D12Device* device, ID3D12DescriptorHeap* heap,
                                  D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t regions,
                                  uint32_t capacity)
{
    mrhiD3d12GpuRing ring = {
        .heap = heap,
        .step = ID3D12Device_GetDescriptorHandleIncrementSize(device, type),
        .capacity = capacity,
    };
    (void)ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap, &ring.cpu);
    (void)ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap, &ring.gpu);
    ring.cpu.ptr += (SIZE_T)ring.step * regions;
    ring.gpu.ptr += (UINT64)ring.step * regions;
    return ring;
}

// Starts a part of the frame's recording: the slot's shader-visible
// heaps set, and its sealed buffers moved into every read a heap may
// make of them, since a heap may read one in any pass without it being
// declared; buffers start each part in the common state they decayed
// to.
static void StartPart(mrhiD3d12Frames* frames, mrhiD3d12Slot* slot)
{
    mrhiD3d12Recorder* recorder = &frames->recorder;
    const mrhiDriverFrame* frame = recorder->frame;
    ID3D12DescriptorHeap* heaps[] = {slot->viewHeap, slot->samplerHeap};
    ID3D12GraphicsCommandList_SetDescriptorHeaps(recorder->list, 2, heaps);
    for (uint32_t i = 0; i < frame->resourceCount; ++i)
    {
        if (frame->resources[i].sealed && frames->table[i].resource != nullptr &&
            frames->table[i].texture == nullptr)
        {
            mrhiD3d12Use(recorder, i + 1, mrhiD3d12BufferState(mrhi_stateSealed));
        }
    }
    mrhiD3d12FlushBarriers(recorder);
}

// Places a native pass (mrhi-0019): its textures moved into their
// accesses' states, the part recorded so far closed and queued with the
// program's list, and the next part begun. Each list runs in its own
// ExecuteCommandLists, after which D3D12 has finished the earlier ones'
// work and buffers have decayed to the common state.
static void RunNative(mrhiD3d12Frames* frames, mrhiD3d12Slot* slot, const mrhiDriverPass* pass)
{
    mrhiD3d12Recorder* recorder = &frames->recorder;
    mrhiD3d12RecordBarriers(recorder, pass->id);
    mrhiD3d12FlushBarriers(recorder);
    MRHI_ASSERT(slot->partsUsed < 1 + MRHI_NATIVE_PASSES);
    HRESULT result = ID3D12GraphicsCommandList_Close(recorder->list);
    slot->runs[slot->runCount++] = (ID3D12CommandList*)recorder->list;
    if (pass->nativeCommands != nullptr)
    {
        slot->runs[slot->runCount++] = (ID3D12CommandList*)pass->nativeCommands;
    }
    recorder->list = slot->parts[slot->partsUsed++];
    if (SUCCEEDED(result))
    {
        result = ID3D12GraphicsCommandList_Reset(recorder->list, slot->allocator, nullptr);
    }
    if (FAILED(result))
    {
        recorder->status = mrhi_errorDeviceLost;
        return;
    }
    for (uint32_t i = 0; i < recorder->frame->resourceCount; ++i)
    {
        mrhiD3d12Object* object = &frames->table[i];
        object->state = object->texture == nullptr ? D3D12_RESOURCE_STATE_COMMON : object->state;
    }
    recorder->scratchState = D3D12_RESOURCE_STATE_COMMON;
    StartPart(frames, slot);
}

// Records a frame into its slot's list: its passes, each after its
// barriers, then the barriers at its end. Answers success,
// mrhi_errorCapacity when a descriptor ring or the scratch buffer ran
// out, or mrhi_errorDeviceLost when D3D12 fails the list.
static mrhiResult Record(mrhiD3d12Frames* frames, mrhiD3d12Slot* slot, const mrhiDriverFrame* frame)
{
    HRESULT result = ID3D12CommandAllocator_Reset(slot->allocator);
    slot->partsUsed = 1;
    slot->runCount = 0;
    if (SUCCEEDED(result))
    {
        result = ID3D12GraphicsCommandList_Reset(slot->parts[0], slot->allocator, nullptr);
    }
    if (FAILED(result))
    {
        return mrhi_errorDeviceLost;
    }
    mrhiD3d12Recorder* recorder = &frames->recorder;
    *recorder = (mrhiD3d12Recorder){
        .device = frames->device,
        .list = slot->parts[0],
        .objects = frames->objects,
        .pipelines = frames->pipelines,
        .frame = frame,
        .serial = frames->submitted + 1,
        .table = frames->table,
        .staging = slot->staging,
        .readback = frames->readback,
        .targets = RingOf(frames->device, slot->targetHeap, D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                          frames->targetLimit),
        .depths = RingOf(frames->device, slot->depthHeap, D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                         frames->depthLimit),
        .views = GpuRingOf(frames->device, slot->viewHeap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                           frames->heapCount * frames->heapEntries, frames->viewLimit),
        .samplers = GpuRingOf(frames->device, slot->samplerHeap, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                              frames->heapCount * frames->heapSamplers, frames->samplerLimit),
        .heapEntries = frames->heapEntries,
        .heapSamplers = frames->heapSamplers,
        .readbacks = slot->readbacks,
        .readbackLimit = frames->readbackLimit,
        .signatures = frames->signatures,
        .zeros = frames->zeros,
        .scratch = &slot->scratch,
        .scratchBytes = frames->scratchBytes,
        .expandRoot = frames->expandRoot,
        .expand = frames->expand,
        .scratchState = D3D12_RESOURCE_STATE_COMMON,
        .status = mrhi_success,
    };
    StartPart(frames, slot);
    for (uint32_t i = 0; i < frame->passCount && recorder->status == mrhi_success; ++i)
    {
        if (frame->passes[i].native)
        {
            RunNative(frames, slot, &frame->passes[i]);
            continue;
        }
        mrhiD3d12RecordPass(recorder, &frame->passes[i]);
    }
    if (recorder->status == mrhi_success)
    {
        mrhiD3d12RecordBarriers(recorder, (mrhiPassId){0});
        MRHI_ASSERT(recorder->barrierAt == frame->barrierCount);
    }
    slot->readbackCount = recorder->readbackCount;
    // A list is closed even when it is not run, so that it resets.
    result = ID3D12GraphicsCommandList_Close(recorder->list);
    slot->runs[slot->runCount++] = (ID3D12CommandList*)recorder->list;
    return FAILED(result) ? mrhi_errorDeviceLost : recorder->status;
}

// Notes the device removed when D3D12 says so: whether it has.
static bool Removed(mrhiD3d12Frames* frames)
{
    HRESULT reason = ID3D12Device_GetDeviceRemovedReason(frames->device);
    if (reason != S_OK)
    {
        frames->removed = reason;
        frames->lost = true;
    }
    return frames->lost;
}

// Presents the frame's surface images after its work: success, or
// mrhi_errorDeviceLost.
static mrhiResult Present(mrhiD3d12Frames* frames, const mrhiDriverFrame* frame)
{
    bool presented = true;
    for (uint32_t i = 0; i < frame->resourceCount; ++i)
    {
        const mrhiDriverResource* resource = &frame->resources[i];
        if (resource->kind == mrhiDriverSurfaceImage)
        {
            presented = mrhiD3d12Present(frames->swapchains, resource->handle) && presented;
        }
    }
    return presented ? mrhi_success : mrhi_errorDeviceLost;
}

mrhiResult mrhiD3d12Submit(mrhiD3d12Frames* frames, const mrhiDriverFrame* frame, uint64_t tag)
{
    if (frames->lost)
    {
        return mrhi_errorDeviceLost;
    }
    mrhiD3d12Slot* slot = &frames->slots[frames->submitted % frames->slotCount];
    MRHI_ASSERT(slot->tag == 0 && tag != 0);
    if (!TakeObjects(frames, slot, frame))
    {
        DropTransients(slot);
        return Removed(frames) ? mrhi_errorDeviceLost : mrhi_errorCapacity;
    }
    if (frame->stagingBytes > 0)
    {
        MRHI_ASSERT(frame->stagingBytes <= frames->uploadBytes);
        memcpy(slot->stagingBytes, frame->staging, frame->stagingBytes);
    }
    mrhiResult status = Record(frames, slot, frame);
    if (status == mrhi_success)
    {
        for (uint32_t i = 0; i < slot->runCount; ++i)
        {
            ID3D12CommandQueue_ExecuteCommandLists(frames->queue, 1, &slot->runs[i]);
        }
        status = Present(frames, frame);
    }
    if (status == mrhi_success &&
        FAILED(ID3D12CommandQueue_Signal(frames->queue, frames->fence, frames->submitted + 1)))
    {
        status = mrhi_errorDeviceLost;
    }
    if (status != mrhi_success)
    {
        DropTransients(slot);
        slot->readbackCount = 0;
        if (status == mrhi_errorDeviceLost)
        {
            // A list D3D12 refuses to close is a recording the core
            // never makes; either way the device is gone to the program.
            (void)Removed(frames);
            frames->lost = true;
        }
        return status;
    }
    ++frames->submitted;
    slot->serial = frames->submitted;
    slot->tag = tag;
    slot->ring = frame->readbackRing;
    return mrhi_success;
}

// Copies a finished frame's readbacks into the core's ring.
static void FillReadbacks(const mrhiD3d12Frames* frames, mrhiD3d12Slot* slot)
{
    for (uint32_t i = 0; i < slot->readbackCount; ++i)
    {
        const mrhiD3d12Range* range = &slot->readbacks[i];
        memcpy(slot->ring + range->offset, frames->readbackBytes + range->offset, range->size);
    }
    slot->readbackCount = 0;
}

size_t mrhiD3d12PollFrames(mrhiD3d12Frames* frames, mrhiDriverEvent* events, size_t capacity)
{
    if (frames->lost || capacity == 0)
    {
        return 0;
    }
    // A removed device's fence reads all ones.
    uint64_t done = ID3D12Fence_GetCompletedValue(frames->fence);
    if (done == UINT64_MAX && Removed(frames))
    {
        events[0] = (mrhiDriverEvent){.tag = 0, .outcome = mrhi_errorDeviceLost};
        return 1;
    }
    size_t moved = 0;
    while (moved < capacity && frames->finished < frames->submitted && frames->finished < done)
    {
        mrhiD3d12Slot* slot = &frames->slots[frames->finished % frames->slotCount];
        FillReadbacks(frames, slot);
        DropTransients(slot);
        RetireUpTo(frames, slot->serial);
        events[moved++] = (mrhiDriverEvent){.tag = slot->tag, .outcome = mrhi_success};
        slot->tag = 0;
        ++frames->finished;
    }
    return moved;
}

// Milliseconds, rounded up, for a wait of timeoutNs; INFINITE for the
// longest.
static DWORD Milliseconds(uint64_t timeoutNs)
{
    if (timeoutNs == UINT64_MAX)
    {
        return INFINITE;
    }
    uint64_t ms = timeoutNs / 1000000 + (timeoutNs % 1000000 != 0 ? 1 : 0);
    return ms < INFINITE ? (DWORD)ms : INFINITE - 1;
}

bool mrhiD3d12WaitFrame(mrhiD3d12Frames* frames, uint64_t tag, uint64_t timeoutNs)
{
    uint64_t serial = 0;
    for (uint32_t i = 0; i < frames->slotCount; ++i)
    {
        serial = frames->slots[i].tag == tag ? frames->slots[i].serial : serial;
    }
    MRHI_ASSERT(serial != 0);
    DWORD total = Milliseconds(timeoutNs);
    ULONGLONG start = GetTickCount64();
    // The event is the device's one, so an earlier wait's completion may
    // wake this one early; it waits again for what time is left.
    for (;;)
    {
        if (ID3D12Fence_GetCompletedValue(frames->fence) >= serial)
        {
            return true;
        }
        ULONGLONG spent = GetTickCount64() - start;
        if (total != INFINITE && spent >= total)
        {
            return false;
        }
        (void)ResetEvent(frames->event);
        if (FAILED(ID3D12Fence_SetEventOnCompletion(frames->fence, serial, frames->event)))
        {
            return false;
        }
        (void)WaitForSingleObject(frames->event,
                                  total == INFINITE ? INFINITE : (DWORD)(total - spent));
    }
}

// The message of a loss: D3D12's reason as eight hex digits.
static uint32_t Describe(char* message, HRESULT result)
{
    static const char text[] = "D3D12 removed the device: HRESULT 0x";
    static const char digits[] = "0123456789ABCDEF";
    static_assert(sizeof(text) - 1 + 8 <= MRHI_LOSS_MESSAGE_BYTES, "the message fits");
    memcpy(message, text, sizeof(text) - 1);
    uint32_t bits = (uint32_t)result;
    for (size_t i = 0; i < 8; ++i)
    {
        message[sizeof(text) - 1 + i] = digits[(bits >> (28 - 4 * i)) & 0xF];
    }
    return (uint32_t)(sizeof(text) - 1 + 8);
}

void mrhiD3d12LossReport(const mrhiD3d12Frames* frames, mrhiDeviceLossReport* reportOut)
{
    mrhiDeviceLossReason reason = mrhi_lossUnknown;
    switch (frames->removed)
    {
    case DXGI_ERROR_DEVICE_HUNG:
        reason = mrhi_lossHung;
        break;
    case DXGI_ERROR_DEVICE_RESET:
        reason = mrhi_lossReset;
        break;
    case DXGI_ERROR_DEVICE_REMOVED:
        reason = mrhi_lossRemoved;
        break;
    case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
        reason = mrhi_lossDriverFault;
        break;
    default:
        break;
    }
    *reportOut = (mrhiDeviceLossReport){.reason = reason};
    reportOut->messageLength = Describe(reportOut->message, frames->removed);
}
