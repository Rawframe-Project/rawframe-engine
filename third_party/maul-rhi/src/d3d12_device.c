// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's devices (mrhi-0003): a D3D12 device, its direct
// command queue, its objects (d3d12_resource.c), its shaders and
// pipelines (d3d12_pipeline.c), its swapchains (d3d12_swapchain.c) and
// its frames (d3d12_frame.c) and its heaps (d3d12_heap.c). A destroyed
// object, pipeline or heap waits until no frame can name it. A device
// whose heaps would leave the frames' descriptor rings too little is
// refused. A window holds one swapchain at a time, so
// configuring a surface again and unconfiguring it wait for the frames
// to finish and release the swapchain at once.

#include "d3d12_device.h"

#include "allocator.h"
#include "d3d12_frame.h"
#include "d3d12_heap.h"
#include "d3d12_names.h"
#include "d3d12_pipeline.h"
#include "d3d12_resource.h"
#include "d3d12_swapchain.h"
#include "invariant.h"

#include <stdalign.h>

typedef struct D3d12Device
{
    mrhiAllocator allocator;
    size_t bytes;
    mrhiD3d12Api api;
    ID3D12Device* device;
    ID3D12CommandQueue* queue;
    mrhiD3d12Objects objects;
    mrhiD3d12Pipelines pipelines;
    mrhiD3d12Swapchains swapchains;
    mrhiD3d12Frames frames;
} D3d12Device;

static void Retire(D3d12Device* device, uint64_t handle, mrhiD3d12Kind kind)
{
    mrhiD3d12RetireLater(&device->frames, kind, handle);
}

static void Destroy(void* self)
{
    D3d12Device* device = self;
    mrhiD3d12CloseFrames(&device->frames);
    for (uint32_t i = 1; i <= device->swapchains.slots.capacity; ++i)
    {
        if (device->swapchains.swapchains[i - 1].swapchain != nullptr)
        {
            mrhiD3d12ReleaseSwapchain(&device->swapchains, i);
        }
    }
    mrhiD3d12CloseObjects(&device->objects);
    IDXGIFactory4_Release(device->swapchains.factory);
    ID3D12CommandQueue_Release(device->queue);
    ID3D12Device_Release(device->device);
    mrhiAllocator allocator = device->allocator;
    mrhiRelease(&allocator, device, device->bytes, alignof(D3d12Device));
}

static mrhiResult CreateSampler(void* self, const mrhiSamplerDef* def, uint64_t* handleOut)
{
    D3d12Device* device = self;
    return mrhiD3d12CreateSampler(&device->objects, def, handleOut);
}

static mrhiResult CreateBuffer(void* self, const mrhiBufferDef* def, uint64_t* handleOut)
{
    D3d12Device* device = self;
    return mrhiD3d12CreateBuffer(&device->objects, def, handleOut);
}

static mrhiResult CreateTexture(void* self, const mrhiTextureDef* def, uint64_t* handleOut)
{
    D3d12Device* device = self;
    return mrhiD3d12CreateTexture(&device->objects, def, handleOut);
}

static mrhiResult CreateView(void* self, uint64_t texture, const mrhiViewDef* def,
                             uint64_t* handleOut)
{
    D3d12Device* device = self;
    return mrhiD3d12CreateView(&device->objects, texture, def, handleOut);
}

static void DestroySampler(void* self, uint64_t handle)
{
    Retire(self, handle, mrhiD3d12KindSampler);
}

static void DestroyBuffer(void* self, uint64_t handle)
{
    Retire(self, handle, mrhiD3d12KindBuffer);
}

static void DestroyTexture(void* self, uint64_t handle)
{
    Retire(self, handle, mrhiD3d12KindTexture);
}

static void DestroyView(void* self, uint64_t handle)
{
    Retire(self, handle, mrhiD3d12KindView);
}

static void DestroyQuerySet(void* self, uint64_t handle)
{
    Retire(self, handle, mrhiD3d12KindQuerySet);
}

static mrhiResult ConfigureSurface(void* self, uint64_t surface, const mrhiSurfaceConfig* config,
                                   uint64_t oldSwapchain, uint64_t* swapchainOut)
{
    D3d12Device* device = self;
    if (oldSwapchain != 0)
    {
        mrhiD3d12WaitIdle(&device->frames);
        mrhiD3d12ReleaseSwapchain(&device->swapchains, oldSwapchain);
    }
    return mrhiD3d12Configure(&device->swapchains, surface, config, swapchainOut);
}

static void UnconfigureSurface(void* self, uint64_t swapchain)
{
    D3d12Device* device = self;
    mrhiD3d12WaitIdle(&device->frames);
    mrhiD3d12ReleaseSwapchain(&device->swapchains, swapchain);
}

static mrhiResult CreateShader(void* self, const mrhiShaderDef* def, const mrhiContainer* container,
                               uint64_t* handleOut)
{
    D3d12Device* device = self;
    return mrhiD3d12CreateShader(&device->pipelines, def, container, handleOut);
}

static void DestroyShader(void* self, uint64_t handle)
{
    D3d12Device* device = self;
    mrhiD3d12DestroyShader(&device->pipelines, handle);
}

static mrhiResult CreateComputePipeline(void* self, const mrhiDriverComputePipeline* pipeline,
                                        uint64_t tag, uint64_t* handleOut)
{
    D3d12Device* device = self;
    return mrhiD3d12CreateCompute(&device->pipelines, pipeline, tag, handleOut);
}

static mrhiResult CreateGraphicsPipeline(void* self, const mrhiDriverGraphicsPipeline* pipeline,
                                         uint64_t tag, uint64_t* handleOut)
{
    D3d12Device* device = self;
    return mrhiD3d12CreateGraphics(&device->pipelines, pipeline, tag, handleOut);
}

static void DestroyPipeline(void* self, uint64_t handle)
{
    D3d12Device* device = self;
    mrhiD3d12ForgetPipeline(&device->pipelines, handle);
    mrhiD3d12RetireLater(&device->frames, mrhiD3d12KindPipeline, handle);
}

static void LossReport(void* self, mrhiDeviceLossReport* reportOut)
{
    const D3d12Device* device = self;
    mrhiD3d12LossReport(&device->frames, reportOut);
}

static mrhiResult AcquireImage(void* self, uint64_t swapchain, uint64_t* imageOut)
{
    D3d12Device* device = self;
    return mrhiD3d12Acquire(&device->swapchains, swapchain, imageOut);
}

static void ReleaseImage(void* self, uint64_t swapchain, uint64_t image)
{
    D3d12Device* device = self;
    mrhiD3d12GiveBack(&device->swapchains, swapchain, image);
}

static mrhiResult CreateQuerySet(void* self, const mrhiQuerySetDef* def, uint64_t* handleOut)
{
    D3d12Device* device = self;
    return mrhiD3d12CreateQuerySet(&device->objects, def, handleOut);
}

// Nanoseconds per tick of the queue's timestamps.
static double TimestampPeriod(void* self)
{
    const D3d12Device* device = self;
    UINT64 frequency = 0;
    return SUCCEEDED(ID3D12CommandQueue_GetTimestampFrequency(device->queue, &frequency)) &&
                   frequency > 0
               ? 1e9 / (double)frequency
               : 0.0;
}

// No pipeline is made yet, so the driver's cache is empty: it takes an
// empty one and declines any other.
static bool ImportPipelineCache(void* self, const void* bytes, size_t size)
{
    (void)self;
    (void)bytes;
    return size == 0;
}

static size_t ExportPipelineCache(void* self, void* bytes, size_t capacity)
{
    (void)self;
    (void)bytes;
    (void)capacity;
    return 0;
}

static void TextureMemory(const void* self, const mrhiTextureDef* def, uint64_t* bytesOut,
                          uint64_t* alignmentOut)
{
    const D3d12Device* device = self;
    mrhiD3d12TextureMemory(&device->objects, def, bytesOut, alignmentOut);
}

static void BufferMemory(const void* self, const mrhiBufferDef* def, uint64_t* bytesOut,
                         uint64_t* alignmentOut)
{
    const D3d12Device* device = self;
    mrhiD3d12BufferMemory(&device->objects, def, bytesOut, alignmentOut);
}

static mrhiResult SubmitFrame(void* self, const mrhiDriverFrame* frame, uint64_t tag)
{
    D3d12Device* device = self;
    return mrhiD3d12Submit(&device->frames, frame, tag);
}

static size_t Poll(void* self, mrhiDriverEvent* events, size_t capacity)
{
    D3d12Device* device = self;
    size_t moved = mrhiD3d12PollPipelines(&device->pipelines, events, capacity);
    return moved + mrhiD3d12PollFrames(&device->frames, events + moved, capacity - moved);
}

static bool WaitFrame(void* self, uint64_t tag, uint64_t timeoutNs)
{
    D3d12Device* device = self;
    return mrhiD3d12WaitFrame(&device->frames, tag, timeoutNs);
}

// A heap's regions are the device's heap limits whatever it asks.
static mrhiResult CreateHeap(void* self, const mrhiHeapDef* def, uint64_t* handleOut)
{
    D3d12Device* device = self;
    (void)def;
    return mrhiD3d12CreateHeap(&device->frames, handleOut);
}

static void DestroyHeap(void* self, uint64_t handle)
{
    Retire(self, handle, mrhiD3d12KindHeap);
}

static void WriteHeapEntry(void* self, uint64_t heap, uint32_t index,
                           const mrhiDriverHeapEntry* entry)
{
    const D3d12Device* device = self;
    mrhiD3d12WriteHeapEntry(&device->frames, heap, index, entry);
}

static void WriteHeapSampler(void* self, uint64_t heap, uint32_t index, uint64_t sampler)
{
    const D3d12Device* device = self;
    mrhiD3d12WriteHeapSampler(&device->frames, heap, index, sampler);
}

static const mrhiDeviceDriverVtable s_vtable = {
    .spiVersion = MRHI_SPI_VERSION,
    .size = sizeof(mrhiDeviceDriverVtable),
    .destroy = Destroy,
    .createSampler = CreateSampler,
    .destroySampler = DestroySampler,
    .createBuffer = CreateBuffer,
    .destroyBuffer = DestroyBuffer,
    .createTexture = CreateTexture,
    .destroyTexture = DestroyTexture,
    .createView = CreateView,
    .destroyView = DestroyView,
    .configureSurface = ConfigureSurface,
    .unconfigureSurface = UnconfigureSurface,
    .createShader = CreateShader,
    .destroyShader = DestroyShader,
    .createComputePipeline = CreateComputePipeline,
    .createGraphicsPipeline = CreateGraphicsPipeline,
    .destroyPipeline = DestroyPipeline,
    .lossReport = LossReport,
    .acquireImage = AcquireImage,
    .releaseImage = ReleaseImage,
    .createQuerySet = CreateQuerySet,
    .destroyQuerySet = DestroyQuerySet,
    .timestampPeriod = TimestampPeriod,
    .importPipelineCache = ImportPipelineCache,
    .exportPipelineCache = ExportPipelineCache,
    .textureMemory = TextureMemory,
    .bufferMemory = BufferMemory,
    .submitFrame = SubmitFrame,
    .poll = Poll,
    .waitFrame = WaitFrame,
    .createHeap = CreateHeap,
    .destroyHeap = DestroyHeap,
    .writeHeapEntry = WriteHeapEntry,
    .writeHeapSampler = WriteHeapSampler,
};

// The room a device takes: itself, and its object, pipeline and frame
// tables.
typedef struct Room
{
    mrhiLayout layout;
    mrhiD3d12ObjectRoom objects;
    mrhiD3d12PipelineRoom pipelines;
    mrhiD3d12SwapchainRoom swapchains;
    mrhiD3d12FrameRoom frames;
} Room;

static Room RoomOf(const mrhiDeviceLimits* limits)
{
    Room room = {.layout = {.size = sizeof(D3d12Device)}};
    room.objects = mrhiD3d12PlanObjects(&room.layout, limits);
    room.pipelines = mrhiD3d12PlanPipelines(&room.layout, limits);
    room.swapchains = mrhiD3d12PlanSwapchains(&room.layout, limits);
    room.frames = mrhiD3d12PlanFrames(&room.layout, limits, MRHI_D3D12_FRAMES);
    return room;
}

// Lays out a device's parts in its block, around its D3D12 device and
// queue.
static void Lay(D3d12Device* made, const Room* room, const mrhiDeviceDef* def)
{
    const mrhiDeviceLimits* limits = &def->deviceLimits;
    bool heaps = def->features.bindlessSampling;
    unsigned char* block = (unsigned char*)made;
    made->objects = (mrhiD3d12Objects){.device = made->device};
    mrhiD3d12LayObjects(&made->objects, block, &room->objects, limits);
    made->pipelines = (mrhiD3d12Pipelines){
        .allocator = &made->allocator,
        .api = &made->api,
        .device = made->device,
    };
    mrhiD3d12LayPipelines(&made->pipelines, block, &room->pipelines, limits);
    mrhiD3d12LaySwapchains(&made->swapchains, block, &room->swapchains, limits);
    made->frames = (mrhiD3d12Frames){
        .device = made->device,
        .queue = made->queue,
        .objects = &made->objects,
        .pipelines = &made->pipelines,
        .swapchains = &made->swapchains,
        .heapCount = heaps ? limits->heaps : 0,
        .heapEntries = heaps ? def->limits.heapSize : 0,
        .heapSamplers = heaps ? def->limits.samplerHeapSize : 0,
    };
    mrhiD3d12LayFrames(&made->frames, block, &room->frames, limits, MRHI_D3D12_FRAMES);
}

mrhiResult mrhiCreateD3d12Device(const mrhiAllocator* allocator, const mrhiD3d12Api* api,
                                 IDXGIFactory4* factory, ID3D12Device* device,
                                 const mrhiDeviceDef* def, mrhiDeviceDriver* deviceOut)
{
    if (def->features.bindlessSampling &&
        !mrhiD3d12HeapsFit(def->deviceLimits.heaps, def->limits.heapSize,
                           def->limits.samplerHeapSize))
    {
        ID3D12Device_Release(device);
        return mrhi_errorUnsupported;
    }
    Room room = RoomOf(&def->deviceLimits);
    D3d12Device* made = room.layout.overflow
                            ? nullptr
                            : mrhiAllocate(allocator, room.layout.size, alignof(D3d12Device));
    if (made == nullptr)
    {
        ID3D12Device_Release(device);
        return mrhi_errorCapacity;
    }
    D3D12_COMMAND_QUEUE_DESC desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
    ID3D12CommandQueue* queue = nullptr;
    if (FAILED(ID3D12Device_CreateCommandQueue(device, &desc, &IID_ID3D12CommandQueue,
                                               (void**)&queue)))
    {
        mrhiRelease(allocator, made, room.layout.size, alignof(D3d12Device));
        ID3D12Device_Release(device);
        return mrhi_errorPlatform;
    }
    mrhiD3d12Label((ID3D12Object*)queue, def->label, def->labelLength);
    IDXGIFactory4_AddRef(factory);
    *made = (D3d12Device){
        .allocator = *allocator,
        .bytes = room.layout.size,
        .api = *api,
        .device = device,
        .queue = queue,
        .swapchains = {.factory = factory, .queue = queue},
    };
    Lay(made, &room, def);
    if (mrhiD3d12OpenObjects(&made->objects) != mrhi_success ||
        mrhiD3d12OpenFrames(&made->frames) != mrhi_success)
    {
        Destroy(made);
        return mrhi_errorCapacity;
    }
    *deviceOut = (mrhiDeviceDriver){.vtable = &s_vtable, .self = made};
    return mrhi_success;
}
