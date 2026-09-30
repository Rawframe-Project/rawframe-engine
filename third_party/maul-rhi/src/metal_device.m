// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Metal driver's devices (mrhi-0003): a retained Metal device and
// its command queue, released when the device is destroyed, its objects
// (metal_resource.m), shaders and pipelines (metal_pipeline.m) and
// frames (metal_frame.m). A destroyed object or pipeline may still be
// named by the next frame submitted, so it waits until that frame is
// committed, whose command buffer then holds what it uses; surfaces are
// layers (metal_surface.m). Heaps are not made yet: those calls answer
// mrhi_errorUnsupported.

#include "metal_device.h"

#include "allocator.h"
#include "invariant.h"
#include "metal_frame.h"
#include "metal_pipeline.h"
#include "metal_resource.h"
#include "metal_surface.h"

#include <stdalign.h>
#include <string.h>

// A destroyed object or pipeline waiting for the next frame's commit.
typedef struct Retiree
{
    uint64_t handle;
    bool pipeline;
} Retiree;

typedef struct MetalDevice
{
    mrhiAllocator allocator;
    size_t bytes;
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    mrhiMetalPipelines pipelines;
    mrhiMetalFrames frames;
    Retiree* retirees;
    uint32_t retireeCount;
    uint32_t retireeLimit;
} MetalDevice;

static void Retire(MetalDevice* device, uint64_t handle, bool pipeline)
{
    // One entry per object the device holds at most.
    MRHI_ASSERT(device->retireeCount < device->retireeLimit);
    device->retirees[device->retireeCount++] = (Retiree){.handle = handle, .pipeline = pipeline};
}

static void ReleaseRetirees(MetalDevice* device)
{
    for (uint32_t i = 0; i < device->retireeCount; ++i)
    {
        const Retiree* retiree = &device->retirees[i];
        if (retiree->pipeline)
        {
            mrhiMetalReleasePipeline(&device->pipelines, retiree->handle);
        }
        else
        {
            mrhiMetalRelease(retiree->handle);
        }
    }
    device->retireeCount = 0;
}

static void Destroy(void* self)
{
    MetalDevice* device = self;
    mrhiMetalCloseFrames(&device->frames);
    ReleaseRetirees(device);
    [device->queue release];
    [device->device release];
    mrhiAllocator allocator = device->allocator;
    mrhiRelease(&allocator, device, device->bytes, alignof(MetalDevice));
}

static mrhiResult CreateSampler(void* self, const mrhiSamplerDef* def, uint64_t* handleOut)
{
    const MetalDevice* device = self;
    return mrhiMetalCreateSampler(device->device, def, handleOut);
}

static mrhiResult CreateBuffer(void* self, const mrhiBufferDef* def, uint64_t* handleOut)
{
    const MetalDevice* device = self;
    return mrhiMetalCreateBuffer(device->device, def, handleOut);
}

static mrhiResult CreateTexture(void* self, const mrhiTextureDef* def, uint64_t* handleOut)
{
    const MetalDevice* device = self;
    return mrhiMetalCreateTexture(device->device, def, handleOut);
}

static mrhiResult CreateView(void* self, uint64_t texture, const mrhiViewDef* def,
                             uint64_t* handleOut)
{
    (void)self;
    return mrhiMetalCreateView(texture, def, handleOut);
}

static mrhiResult CreateQuerySet(void* self, const mrhiQuerySetDef* def, uint64_t* handleOut)
{
    const MetalDevice* device = self;
    return mrhiMetalCreateQuerySet(device->device, def, handleOut);
}

static void DestroyObject(void* self, uint64_t handle)
{
    Retire(self, handle, false);
}

// Heaps are never made, so never destroyed.
static void Never(void* self, uint64_t handle)
{
    (void)self;
    (void)handle;
    MRHI_ASSERT(false);
}

static mrhiResult ConfigureSurface(void* self, uint64_t surface, const mrhiSurfaceConfig* config,
                                   uint64_t oldSwapchain, uint64_t* swapchainOut)
{
    const MetalDevice* device = self;
    return mrhiMetalConfigure(&device->allocator, device->device, surface, config, oldSwapchain,
                              swapchainOut);
}

static void UnconfigureSurface(void* self, uint64_t swapchain)
{
    const MetalDevice* device = self;
    mrhiMetalUnconfigure(&device->allocator, swapchain);
}

static mrhiResult CreateShader(void* self, const mrhiShaderDef* def, const mrhiContainer* container,
                               uint64_t* handleOut)
{
    const MetalDevice* device = self;
    return mrhiMetalCreateShader(&device->pipelines, def, container, handleOut);
}

static void DestroyShader(void* self, uint64_t handle)
{
    const MetalDevice* device = self;
    mrhiMetalDestroyShader(&device->pipelines, handle);
}

static mrhiResult CreateComputePipeline(void* self, const mrhiDriverComputePipeline* pipeline,
                                        uint64_t tag, uint64_t* handleOut)
{
    MetalDevice* device = self;
    return mrhiMetalCreateCompute(&device->pipelines, pipeline, tag, handleOut);
}

static mrhiResult CreateGraphicsPipeline(void* self, const mrhiDriverGraphicsPipeline* pipeline,
                                         uint64_t tag, uint64_t* handleOut)
{
    MetalDevice* device = self;
    return mrhiMetalCreateGraphics(&device->pipelines, pipeline, tag, handleOut);
}

static void DestroyPipeline(void* self, uint64_t handle)
{
    MetalDevice* device = self;
    mrhiMetalForgetPipeline(&device->pipelines, handle);
    Retire(device, handle, true);
}

// Metal says only that a command buffer failed, with its error.
static void LossReport(void* self, mrhiDeviceLossReport* reportOut)
{
    const MetalDevice* device = self;
    *reportOut = (mrhiDeviceLossReport){
        .reason = mrhi_lossUnknown,
        .messageLength = device->frames.messageLength,
    };
    memcpy(reportOut->message, device->frames.message, device->frames.messageLength);
}

static mrhiResult AcquireImage(void* self, uint64_t swapchain, uint64_t* imageOut)
{
    (void)self;
    return mrhiMetalAcquire(swapchain, imageOut);
}

static void ReleaseImage(void* self, uint64_t swapchain, uint64_t image)
{
    (void)self;
    (void)swapchain;
    mrhiMetalReleaseImage(image);
}

// Timestamps are not granted yet, so the core never asks.
static double TimestampPeriod(void* self)
{
    (void)self;
    return 0.0;
}

// Metal keeps its own cache of compiled functions: the driver's is
// empty, so it takes an empty one and declines any other.
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
    const MetalDevice* device = self;
    mrhiMetalTextureMemory(device->device, def, bytesOut, alignmentOut);
}

static void BufferMemory(const void* self, const mrhiBufferDef* def, uint64_t* bytesOut,
                         uint64_t* alignmentOut)
{
    const MetalDevice* device = self;
    mrhiMetalBufferMemory(device->device, def, bytesOut, alignmentOut);
}

static mrhiResult SubmitFrame(void* self, const mrhiDriverFrame* frame, uint64_t tag)
{
    MetalDevice* device = self;
    mrhiResult status = mrhiMetalSubmitFrame(&device->frames, frame, tag);
    if (status == mrhi_success)
    {
        ReleaseRetirees(device);
    }
    return status;
}

static size_t Poll(void* self, mrhiDriverEvent* events, size_t capacity)
{
    MetalDevice* device = self;
    size_t moved = mrhiMetalPollPipelines(&device->pipelines, events, capacity);
    return moved + mrhiMetalPollFrames(&device->frames, events + moved, capacity - moved);
}

static bool WaitFrame(void* self, uint64_t tag, uint64_t timeoutNs)
{
    MetalDevice* device = self;
    return mrhiMetalWaitFrame(&device->frames, tag, timeoutNs);
}

static mrhiResult CreateHeap(void* self, const mrhiHeapDef* def, uint64_t* handleOut)
{
    (void)self;
    (void)def;
    *handleOut = 0;
    return mrhi_errorUnsupported;
}

static void WriteHeapEntry(void* self, uint64_t heap, uint32_t index,
                           const mrhiDriverHeapEntry* entry)
{
    (void)self;
    (void)heap;
    (void)index;
    (void)entry;
    MRHI_ASSERT(false);
}

static void WriteHeapSampler(void* self, uint64_t heap, uint32_t index, uint64_t sampler)
{
    (void)self;
    (void)heap;
    (void)index;
    (void)sampler;
    MRHI_ASSERT(false);
}

static const mrhiDeviceDriverVtable s_vtable = {
    .spiVersion = MRHI_SPI_VERSION,
    .size = sizeof(mrhiDeviceDriverVtable),
    .destroy = Destroy,
    .createSampler = CreateSampler,
    .destroySampler = DestroyObject,
    .createBuffer = CreateBuffer,
    .destroyBuffer = DestroyObject,
    .createTexture = CreateTexture,
    .destroyTexture = DestroyObject,
    .createView = CreateView,
    .destroyView = DestroyObject,
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
    .destroyQuerySet = DestroyObject,
    .timestampPeriod = TimestampPeriod,
    .importPipelineCache = ImportPipelineCache,
    .exportPipelineCache = ExportPipelineCache,
    .textureMemory = TextureMemory,
    .bufferMemory = BufferMemory,
    .submitFrame = SubmitFrame,
    .poll = Poll,
    .waitFrame = WaitFrame,
    .createHeap = CreateHeap,
    .destroyHeap = Never,
    .writeHeapEntry = WriteHeapEntry,
    .writeHeapSampler = WriteHeapSampler,
};

// The room a device takes: itself, the pipelines' answers, a frame's
// objects and encoding, and the retirees.
typedef struct Room
{
    mrhiLayout layout;
    size_t pending;
    size_t handles;
    size_t objects;
    size_t encoder;
    size_t retirees;
    uint32_t retireeLimit;
} Room;

static Room RoomOf(const mrhiDeviceLimits* limits)
{
    Room room = {.layout = {.size = sizeof(MetalDevice)}};
    mrhiLayout* layout = &room.layout;
    room.pending =
        mrhiLayoutAdd(layout, limits->pipelines, sizeof(mrhiDriverEvent), alignof(mrhiDriverEvent));
    room.handles = mrhiLayoutAdd(layout, limits->pipelines, sizeof(uint64_t), alignof(uint64_t));
    room.objects = mrhiLayoutAdd(layout, limits->frameResources, sizeof(id), alignof(id));
    room.encoder = mrhiLayoutAdd(layout, 1, sizeof(mrhiMetalEncoder), alignof(mrhiMetalEncoder));
    uint64_t retirees = (uint64_t)limits->buffers + limits->textures + limits->views +
                        limits->samplers + limits->querySets + limits->pipelines;
    room.retireeLimit = retirees < UINT32_MAX ? (uint32_t)retirees : UINT32_MAX;
    room.retirees = mrhiLayoutAdd(layout, room.retireeLimit, sizeof(Retiree), alignof(Retiree));
    return room;
}

// Lays out a device's parts in its block, around its Metal device and
// queue.
static void Lay(MetalDevice* made, const Room* room, const mrhiDeviceLimits* limits)
{
    unsigned char* block = (unsigned char*)made;
    made->pipelines = (mrhiMetalPipelines){
        .allocator = &made->allocator,
        .device = made->device,
        .pending = (mrhiDriverEvent*)(block + room->pending),
        .pendingHandles = (uint64_t*)(block + room->handles),
        .pendingLimit = limits->pipelines,
    };
    made->frames = (mrhiMetalFrames){
        .device = made->device,
        .queue = made->queue,
        .objects = (id*)(void*)(block + room->objects),
        .objectLimit = limits->frameResources,
        .encoder = (mrhiMetalEncoder*)(void*)(block + room->encoder),
    };
    made->retirees = (Retiree*)(void*)(block + room->retirees);
    made->retireeLimit = room->retireeLimit;
}

mrhiResult mrhiCreateMetalDevice(const mrhiAllocator* allocator, id<MTLDevice> device,
                                 const mrhiDeviceDef* def, mrhiDeviceDriver* deviceOut)
{
    Room room = RoomOf(&def->deviceLimits);
    MetalDevice* made = room.layout.overflow
                            ? nullptr
                            : mrhiAllocate(allocator, room.layout.size, alignof(MetalDevice));
    if (made == nullptr)
    {
        return mrhi_errorCapacity;
    }
    @autoreleasepool
    {
        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (queue == nil)
        {
            mrhiRelease(allocator, made, room.layout.size, alignof(MetalDevice));
            return mrhi_errorPlatform;
        }
        if (def->labelLength > 0)
        {
            queue.label = [[[NSString alloc] initWithBytes:def->label
                                                    length:def->labelLength
                                                  encoding:NSUTF8StringEncoding] autorelease];
        }
        *made = (MetalDevice){
            .allocator = *allocator,
            .bytes = room.layout.size,
            .device = [device retain],
            .queue = queue,
        };
    }
    Lay(made, &room, &def->deviceLimits);
    if (mrhiMetalOpenFrames(&made->frames) != mrhi_success)
    {
        Destroy(made);
        return mrhi_errorPlatform;
    }
    *deviceOut = (mrhiDeviceDriver){.vtable = &s_vtable, .self = made};
    return mrhi_success;
}
