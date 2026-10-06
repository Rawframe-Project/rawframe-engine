// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The validation layer (mrhi-0025): vtables around whichever driver an
// instance starts, which pass every call through and count what breaks
// the SPI's contract: a frame no driver could translate, and answers no
// core could trust. Counts a driver could make the core overrun are
// clamped; everything else passes through as the driver gave it.

// Built only with the layer; defined here too for tools that read the
// file alone, outside such a build.
#ifndef MAUL_RHI_VALIDATION
#define MAUL_RHI_VALIDATION
#endif

#include "validation.h"

#include "allocator.h"
#include "frame_walk.h"

#include <stdalign.h>

// Where breaches go: the instance's fault count and its diagnostic
// queue (mrhi-0027).
typedef struct Watch
{
    _Atomic uint64_t* faults;
    mrhiDiagnosticQueue* diagnostics;
} Watch;

// The instance driver's layer: the driver, the allocator the layer
// came from, where its breaches go, and the tags of the requests the
// driver has not answered yet, at most tagLimit.
typedef struct Layer
{
    mrhiInstanceDriver inner;
    mrhiAllocator allocator;
    Watch watch;
    uint32_t tagLimit;
    uint32_t tagCount;
    uint64_t* tags;
} Layer;

// A device driver's layer.
typedef struct DeviceLayer
{
    mrhiDeviceDriver inner;
    mrhiAllocator allocator;
    Watch watch;
} DeviceLayer;

// Counts and records a fault when a check fails.
static void Expect(const Watch* watch, bool held, mrhiDiagnosticCode code)
{
    if (!held)
    {
        atomic_fetch_add_explicit(watch->faults, 1, memory_order_relaxed);
        mrhiRecordDiagnostic(watch->diagnostics, code);
    }
}

// Records a request the driver must answer; one past the limit is a
// fault, since the core keeps no more pending.
static void Ask(Layer* layer, uint64_t tag)
{
    Expect(&layer->watch, tag != 0 && layer->tagCount < layer->tagLimit,
           mrhi_diagnosticDriverRequestTags);
    if (tag != 0 && layer->tagCount < layer->tagLimit)
    {
        layer->tags[layer->tagCount++] = tag;
    }
}

// Whether a tag was asked and not yet answered, forgetting it.
static bool Answer(Layer* layer, uint64_t tag)
{
    for (uint32_t i = 0; i < layer->tagCount; ++i)
    {
        if (layer->tags[i] == tag)
        {
            layer->tags[i] = layer->tags[--layer->tagCount];
            return true;
        }
    }
    return false;
}

static mrhiResult RequestAdapters(void* self, uint64_t tag)
{
    Layer* layer = self;
    mrhiResult status = layer->inner.vtable->requestAdapters(layer->inner.self, tag);
    if (status == mrhi_success)
    {
        Ask(layer, tag);
    }
    return status;
}

static size_t Poll(void* self, mrhiDriverEvent* events, size_t capacity)
{
    Layer* layer = self;
    size_t moved = layer->inner.vtable->poll(layer->inner.self, events, capacity);
    Expect(&layer->watch, moved <= capacity, mrhi_diagnosticDriverEventsOverrun);
    moved = moved <= capacity ? moved : capacity;
    for (size_t i = 0; i < moved; ++i)
    {
        Expect(&layer->watch, Answer(layer, events[i].tag), mrhi_diagnosticDriverUnaskedAnswer);
    }
    return moved;
}

static size_t GetAdapters(const void* self, mrhiDriverAdapter* adapters, size_t capacity)
{
    const Layer* layer = self;
    size_t total = layer->inner.vtable->getAdapters(layer->inner.self, adapters, capacity);
    size_t listed = total <= capacity ? total : capacity;
    for (size_t i = 0; i < listed; ++i)
    {
        const mrhiDriverAdapter* adapter = &adapters[i];
        bool distinct = adapter->handle != 0;
        for (size_t j = 0; j < i; ++j)
        {
            distinct = distinct && adapters[j].handle != adapter->handle;
        }
        Expect(&layer->watch,
               distinct && adapter->info.nameLength <= MRHI_ADAPTER_NAME_BYTES &&
                   adapter->info.kind <= mrhi_adapterSoftware,
               mrhi_diagnosticDriverAdapter);
    }
    return total;
}

static void GetFormatCaps(const void* self, uint64_t adapter, mrhiFormat format,
                          mrhiFormatCaps* capsOut)
{
    const Layer* layer = self;
    layer->inner.vtable->getFormatCaps(layer->inner.self, adapter, format, capsOut);
    // One, two and four samples are all a cap can name.
    Expect(&layer->watch, (capsOut->sampleCounts & ~0x7u) == 0, mrhi_diagnosticDriverSampleCounts);
}

static mrhiResult CreateSurface(void* self, const mrhiChain* source, const mrhiSurfaceDef* def,
                                uint64_t* handleOut)
{
    Layer* layer = self;
    mrhiResult status =
        layer->inner.vtable->createSurface(layer->inner.self, source, def, handleOut);
    Expect(&layer->watch, status != mrhi_success || *handleOut != 0,
           mrhi_diagnosticDriverZeroHandle);
    return status;
}

static void DestroySurface(void* self, uint64_t handle)
{
    Layer* layer = self;
    layer->inner.vtable->destroySurface(layer->inner.self, handle);
}

static void GetSurfaceCaps(const void* self, uint64_t surface, uint64_t adapter,
                           mrhiSurfaceCaps* capsOut)
{
    const Layer* layer = self;
    layer->inner.vtable->getSurfaceCaps(layer->inner.self, surface, adapter, capsOut);
}

static const mrhiDeviceDriverVtable s_deviceVtable;

static mrhiResult CreateDevice(void* self, uint64_t adapter, const mrhiDeviceDef* def, uint64_t tag,
                               mrhiDeviceDriver* deviceOut)
{
    Layer* layer = self;
    mrhiResult status =
        layer->inner.vtable->createDevice(layer->inner.self, adapter, def, tag, deviceOut);
    if (status != mrhi_success)
    {
        return status;
    }
    // A device that fails the handshake goes to the core unwrapped, which
    // refuses it.
    if (mrhiCheckDeviceVtable(deviceOut->vtable) != mrhi_success)
    {
        Expect(&layer->watch, false, mrhi_diagnosticDriverDeviceHandshake);
        return status;
    }
    DeviceLayer* device =
        mrhiAllocate(&layer->allocator, sizeof(DeviceLayer), alignof(DeviceLayer));
    if (device == nullptr)
    {
        deviceOut->vtable->destroy(deviceOut->self);
        return mrhi_errorCapacity;
    }
    *device =
        (DeviceLayer){.inner = *deviceOut, .allocator = layer->allocator, .watch = layer->watch};
    *deviceOut = (mrhiDeviceDriver){.vtable = &s_deviceVtable, .self = device};
    Ask(layer, tag);
    return mrhi_success;
}

static void Destroy(void* self)
{
    Layer* layer = self;
    layer->inner.vtable->destroy(layer->inner.self);
    mrhiAllocator allocator = layer->allocator;
    mrhiRelease(&allocator, layer, sizeof(Layer) + layer->tagLimit * sizeof(uint64_t),
                alignof(Layer));
}

static const mrhiInstanceDriverVtable s_vtable = {
    .spiVersion = MRHI_SPI_VERSION,
    .size = sizeof(mrhiInstanceDriverVtable),
    .requestAdapters = RequestAdapters,
    .poll = Poll,
    .getAdapters = GetAdapters,
    .getFormatCaps = GetFormatCaps,
    .createSurface = CreateSurface,
    .destroySurface = DestroySurface,
    .getSurfaceCaps = GetSurfaceCaps,
    .createDevice = CreateDevice,
    .destroy = Destroy,
};

mrhiResult mrhiWrapDriver(const mrhiAllocator* allocator, uint32_t tagLimit,
                          _Atomic uint64_t* faults, mrhiDiagnosticQueue* diagnostics,
                          mrhiInstanceDriver* driver)
{
    size_t bytes = sizeof(Layer) + tagLimit * sizeof(uint64_t);
    Layer* layer = mrhiAllocate(allocator, bytes, alignof(Layer));
    if (layer == nullptr)
    {
        return mrhi_errorCapacity;
    }
    *layer = (Layer){
        .inner = *driver,
        .allocator = *allocator,
        .watch = {.faults = faults, .diagnostics = diagnostics},
        .tagLimit = tagLimit,
        .tags = (uint64_t*)(layer + 1),
    };
    *driver = (mrhiInstanceDriver){.vtable = &s_vtable, .self = layer};
    return mrhi_success;
}

// The device layer: each function passes through, checking what the
// driver answers.

static void DestroyDevice(void* self)
{
    DeviceLayer* device = self;
    device->inner.vtable->destroy(device->inner.self);
    mrhiAllocator allocator = device->allocator;
    mrhiRelease(&allocator, device, sizeof(DeviceLayer), alignof(DeviceLayer));
}

// Checks a made object's handle, which is never zero.
static mrhiResult Made(const DeviceLayer* device, mrhiResult status, const uint64_t* handleOut)
{
    Expect(&device->watch, status != mrhi_success || *handleOut != 0,
           mrhi_diagnosticDriverZeroHandle);
    return status;
}

static mrhiResult CreateSampler(void* self, const mrhiSamplerDef* def, uint64_t* handleOut)
{
    DeviceLayer* device = self;
    return Made(device, device->inner.vtable->createSampler(device->inner.self, def, handleOut),
                handleOut);
}

static void DestroySampler(void* self, uint64_t handle)
{
    DeviceLayer* device = self;
    device->inner.vtable->destroySampler(device->inner.self, handle);
}

static mrhiResult CreateBuffer(void* self, const mrhiBufferDef* def, uint64_t* handleOut)
{
    DeviceLayer* device = self;
    return Made(device, device->inner.vtable->createBuffer(device->inner.self, def, handleOut),
                handleOut);
}

static void DestroyBuffer(void* self, uint64_t handle)
{
    DeviceLayer* device = self;
    device->inner.vtable->destroyBuffer(device->inner.self, handle);
}

static mrhiResult CreateTexture(void* self, const mrhiTextureDef* def, uint64_t* handleOut)
{
    DeviceLayer* device = self;
    return Made(device, device->inner.vtable->createTexture(device->inner.self, def, handleOut),
                handleOut);
}

static void DestroyTexture(void* self, uint64_t handle)
{
    DeviceLayer* device = self;
    device->inner.vtable->destroyTexture(device->inner.self, handle);
}

static mrhiResult CreateView(void* self, uint64_t texture, const mrhiViewDef* def,
                             uint64_t* handleOut)
{
    DeviceLayer* device = self;
    return Made(device,
                device->inner.vtable->createView(device->inner.self, texture, def, handleOut),
                handleOut);
}

static void DestroyView(void* self, uint64_t handle)
{
    DeviceLayer* device = self;
    device->inner.vtable->destroyView(device->inner.self, handle);
}

static mrhiResult ConfigureSurface(void* self, uint64_t surface, const mrhiSurfaceConfig* config,
                                   uint64_t oldSwapchain, uint64_t* swapchainOut)
{
    DeviceLayer* device = self;
    return Made(device,
                device->inner.vtable->configureSurface(device->inner.self, surface, config,
                                                       oldSwapchain, swapchainOut),
                swapchainOut);
}

static void UnconfigureSurface(void* self, uint64_t swapchain)
{
    DeviceLayer* device = self;
    device->inner.vtable->unconfigureSurface(device->inner.self, swapchain);
}

static mrhiResult CreateShader(void* self, const mrhiShaderDef* def, const mrhiContainer* container,
                               uint64_t* handleOut)
{
    DeviceLayer* device = self;
    return Made(device,
                device->inner.vtable->createShader(device->inner.self, def, container, handleOut),
                handleOut);
}

static void DestroyShader(void* self, uint64_t handle)
{
    DeviceLayer* device = self;
    device->inner.vtable->destroyShader(device->inner.self, handle);
}

static mrhiResult CreateComputePipeline(void* self, const mrhiDriverComputePipeline* pipeline,
                                        uint64_t tag, uint64_t* handleOut)
{
    DeviceLayer* device = self;
    return Made(
        device,
        device->inner.vtable->createComputePipeline(device->inner.self, pipeline, tag, handleOut),
        handleOut);
}

static mrhiResult CreateGraphicsPipeline(void* self, const mrhiDriverGraphicsPipeline* pipeline,
                                         uint64_t tag, uint64_t* handleOut)
{
    DeviceLayer* device = self;
    return Made(
        device,
        device->inner.vtable->createGraphicsPipeline(device->inner.self, pipeline, tag, handleOut),
        handleOut);
}

static void DestroyPipeline(void* self, uint64_t handle)
{
    DeviceLayer* device = self;
    device->inner.vtable->destroyPipeline(device->inner.self, handle);
}

static void LossReport(void* self, mrhiDeviceLossReport* reportOut)
{
    DeviceLayer* device = self;
    device->inner.vtable->lossReport(device->inner.self, reportOut);
}

static mrhiResult AcquireImage(void* self, uint64_t swapchain, uint64_t* imageOut)
{
    DeviceLayer* device = self;
    mrhiResult status = device->inner.vtable->acquireImage(device->inner.self, swapchain, imageOut);
    // An image comes with every acquire that gives one.
    bool given = status == mrhi_success || status == mrhi_suboptimal;
    Expect(&device->watch, !given || *imageOut != 0, mrhi_diagnosticDriverZeroHandle);
    return status;
}

static void ReleaseImage(void* self, uint64_t swapchain, uint64_t image)
{
    DeviceLayer* device = self;
    device->inner.vtable->releaseImage(device->inner.self, swapchain, image);
}

static mrhiResult CreateQuerySet(void* self, const mrhiQuerySetDef* def, uint64_t* handleOut)
{
    DeviceLayer* device = self;
    return Made(device, device->inner.vtable->createQuerySet(device->inner.self, def, handleOut),
                handleOut);
}

static void DestroyQuerySet(void* self, uint64_t handle)
{
    DeviceLayer* device = self;
    device->inner.vtable->destroyQuerySet(device->inner.self, handle);
}

static double TimestampPeriod(void* self)
{
    DeviceLayer* device = self;
    double period = device->inner.vtable->timestampPeriod(device->inner.self);
    // Zero for a driver that does not say; never negative or NaN.
    Expect(&device->watch, period >= 0.0, mrhi_diagnosticDriverTimestampPeriod);
    return period;
}

static bool ImportPipelineCache(void* self, const void* bytes, size_t size)
{
    DeviceLayer* device = self;
    return device->inner.vtable->importPipelineCache(device->inner.self, bytes, size);
}

static size_t ExportPipelineCache(void* self, void* bytes, size_t capacity)
{
    DeviceLayer* device = self;
    return device->inner.vtable->exportPipelineCache(device->inner.self, bytes, capacity);
}

static void TextureMemory(const void* self, const mrhiTextureDef* def, uint64_t* bytesOut,
                          uint64_t* alignmentOut)
{
    const DeviceLayer* device = self;
    device->inner.vtable->textureMemory(device->inner.self, def, bytesOut, alignmentOut);
    Expect(&device->watch, *alignmentOut != 0 && (*alignmentOut & (*alignmentOut - 1)) == 0,
           mrhi_diagnosticDriverMemoryAlignment);
}

static void BufferMemory(const void* self, const mrhiBufferDef* def, uint64_t* bytesOut,
                         uint64_t* alignmentOut)
{
    const DeviceLayer* device = self;
    device->inner.vtable->bufferMemory(device->inner.self, def, bytesOut, alignmentOut);
    Expect(&device->watch, *alignmentOut != 0 && (*alignmentOut & (*alignmentOut - 1)) == 0,
           mrhi_diagnosticDriverMemoryAlignment);
}

// Handles a frame names are never zero; the layer does not track which
// are live.
static bool IsHandle(const void* handles, uint64_t handle)
{
    (void)handles;
    return handle != 0;
}

static mrhiResult SubmitFrame(void* self, const mrhiDriverFrame* frame, uint64_t tag)
{
    DeviceLayer* device = self;
    Expect(&device->watch, mrhiWalkFrame(frame, IsHandle, nullptr, nullptr) == 0,
           mrhi_diagnosticDriverFrameWalk);
    return device->inner.vtable->submitFrame(device->inner.self, frame, tag);
}

static size_t PollDevice(void* self, mrhiDriverEvent* events, size_t capacity)
{
    DeviceLayer* device = self;
    size_t moved = device->inner.vtable->poll(device->inner.self, events, capacity);
    Expect(&device->watch, moved <= capacity, mrhi_diagnosticDriverEventsOverrun);
    moved = moved <= capacity ? moved : capacity;
    for (size_t i = 0; i < moved; ++i)
    {
        // Tag 0 is the device's loss, and only that.
        Expect(&device->watch, events[i].tag != 0 || events[i].outcome == mrhi_errorDeviceLost,
               mrhi_diagnosticDriverEventTag);
    }
    return moved;
}

static bool WaitFrame(void* self, uint64_t tag, uint64_t timeoutNs)
{
    DeviceLayer* device = self;
    return device->inner.vtable->waitFrame(device->inner.self, tag, timeoutNs);
}

static mrhiResult CreateHeap(void* self, const mrhiHeapDef* def, uint64_t* handleOut)
{
    DeviceLayer* device = self;
    return Made(device, device->inner.vtable->createHeap(device->inner.self, def, handleOut),
                handleOut);
}

static void DestroyHeap(void* self, uint64_t handle)
{
    DeviceLayer* device = self;
    device->inner.vtable->destroyHeap(device->inner.self, handle);
}

static void WriteHeapEntry(void* self, uint64_t heap, uint32_t index,
                           const mrhiDriverHeapEntry* entry)
{
    DeviceLayer* device = self;
    device->inner.vtable->writeHeapEntry(device->inner.self, heap, index, entry);
}

static void WriteHeapSampler(void* self, uint64_t heap, uint32_t index, uint64_t sampler)
{
    DeviceLayer* device = self;
    device->inner.vtable->writeHeapSampler(device->inner.self, heap, index, sampler);
}

static const mrhiDeviceDriverVtable s_deviceVtable = {
    .spiVersion = MRHI_SPI_VERSION,
    .size = sizeof(mrhiDeviceDriverVtable),
    .destroy = DestroyDevice,
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
    .poll = PollDevice,
    .waitFrame = WaitFrame,
    .createHeap = CreateHeap,
    .destroyHeap = DestroyHeap,
    .writeHeapEntry = WriteHeapEntry,
    .writeHeapSampler = WriteHeapSampler,
};

const mrhiInstanceDriver* mrhiInnerDriver(const mrhiInstanceDriver* driver)
{
    return driver->vtable == &s_vtable ? &((const Layer*)driver->self)->inner : driver;
}

const mrhiDeviceDriver* mrhiInnerDevice(const mrhiDeviceDriver* device)
{
    return device->vtable == &s_deviceVtable ? &((const DeviceLayer*)device->self)->inner : device;
}
