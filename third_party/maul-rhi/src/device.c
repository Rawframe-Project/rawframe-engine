// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Devices (mrhi-0004): made at once and opened by their driver, with
// the features and limits asked for checked against the adapter and
// the floor.

#include "allocator.h"
#include "capabilities_core.h"
#include "chain.h"
#include "device_core.h"
#include "heap_core.h"
#include "instance_core.h"
#include "invariant.h"
#include "label.h"

#include <stdalign.h>
#include <stdatomic.h>
#include <stddef.h>

#define DEVICE_DEF_COOKIE 0x6D726476u

mrhiDeviceDef mrhiDefaultDeviceDef(void)
{
    mrhiDeviceDef def = {0};
    def.cookie = DEVICE_DEF_COOKIE;
    def.limits = mrhiDefaultLimits();
    def.deviceLimits.notifications = 256;
    def.deviceLimits.samplers = 256;
    def.deviceLimits.buffers = 4096;
    def.deviceLimits.textures = 4096;
    def.deviceLimits.views = 8192;
    def.deviceLimits.surfaces = 8;
    def.deviceLimits.frameResources = 1024;
    def.deviceLimits.framePasses = 256;
    def.deviceLimits.frameAccesses = 4096;
    def.deviceLimits.frameBarriers = 4096;
    def.deviceLimits.shaders = 256;
    def.deviceLimits.pipelines = 1024;
    def.deviceLimits.frameCommandBytes = 1u << 20;
    def.deviceLimits.frameUploadBytes = 1u << 20;
    def.deviceLimits.readbackBytes = 1u << 20;
    def.deviceLimits.readbacks = 64;
    def.deviceLimits.querySets = 16;
    def.deviceLimits.queries = 4096;
    def.deviceLimits.heaps = 4;
    return def;
}

// Checks a def against the instance, the adapter and the floor, and
// returns the adapter it names; NULL with the refusal in statusOut.
static const mrhiDriverAdapter* CheckDef(mrhiInstance* instance, const mrhiDeviceDef* def,
                                         mrhiResult* statusOut)
{
    mrhiLimits floor = mrhiDefaultLimits();
    mrhiResult chain = mrhiCheckChain(def->next, nullptr, 0, instance->limits.chainDepth);
    if (def->cookie != DEVICE_DEF_COOKIE || def->deviceLimits.notifications < 2 ||
        def->deviceLimits.samplers == 0 || def->deviceLimits.buffers == 0 ||
        def->deviceLimits.textures == 0 || def->deviceLimits.views == 0 ||
        def->deviceLimits.surfaces == 0 || def->deviceLimits.frameResources == 0 ||
        def->deviceLimits.framePasses == 0 || def->deviceLimits.frameAccesses == 0 ||
        def->deviceLimits.frameBarriers == 0 || def->deviceLimits.shaders == 0 ||
        def->deviceLimits.pipelines == 0 ||
        def->deviceLimits.frameCommandBytes < MRHI_CHUNK_BYTES ||
        def->deviceLimits.readbackBytes % 512 != 0 ||
        !mrhiIsLabelValid(def->label, def->labelLength) || !mrhiIsAllocatorValid(&def->allocator) ||
        (def->pipelineCache == nullptr && def->pipelineCacheBytes > 0) ||
        !mrhiLimitsWithin(&floor, &def->limits) || chain == mrhi_errorInvalid)
    {
        *statusOut = mrhiMisuse(instance);
        return nullptr;
    }
    const mrhiDriverAdapter* adapter = mrhiFindAdapter(instance, def->adapter);
    if (chain != mrhi_success)
    {
        *statusOut = chain;
    }
    else if (adapter == nullptr)
    {
        *statusOut = mrhi_errorStale;
    }
    else if (!mrhiFeaturesWithin(&def->features, &adapter->features) ||
             !mrhiLimitsWithin(&def->limits, &adapter->limits))
    {
        *statusOut = mrhi_errorUnsupported;
    }
    else if (!mrhiHasRoomForAnswer(instance))
    {
        *statusOut = mrhi_errorCapacity;
    }
    else
    {
        return adapter;
    }
    return nullptr;
}

// Where one table of the device's block starts: its pool's two arrays
// and its payload.
typedef struct TableParts
{
    size_t generations;
    size_t nextFree;
    size_t payload;
} TableParts;

static TableParts AddTable(mrhiLayout* layout, uint32_t count, size_t payloadSize,
                           size_t payloadAlignment)
{
    TableParts parts;
    parts.generations = mrhiLayoutAdd(layout, count, sizeof(uint32_t), alignof(uint32_t));
    parts.nextFree = mrhiLayoutAdd(layout, count, sizeof(uint32_t), alignof(uint32_t));
    parts.payload = mrhiLayoutAdd(layout, count, payloadSize, payloadAlignment);
    return parts;
}

// Starts a table's pool in the block and returns its payload.
static void* InitTable(unsigned char* block, TableParts parts, mrhiPool* pool, uint32_t count)
{
    mrhiPoolInit(pool, count, (uint32_t*)(block + parts.generations),
                 (uint32_t*)(block + parts.nextFree));
    return block + parts.payload;
}

// Where the tables of the device's objects sit in its block.
typedef struct ObjectParts
{
    TableParts samplers;
    TableParts buffers;
    TableParts textures;
    TableParts views;
    TableParts querySets;
    size_t marks;
    TableParts heaps;
    TableParts swapchains;
    TableParts shaders;
    TableParts pipelines;
    size_t vertex;
} ObjectParts;

static ObjectParts AddObjectParts(mrhiLayout* layout, const mrhiDeviceDef* def)
{
    const mrhiDeviceLimits* limits = &def->deviceLimits;
    ObjectParts parts;
    parts.samplers =
        AddTable(layout, limits->samplers, sizeof(mrhiSamplerSlot), alignof(mrhiSamplerSlot));
    parts.buffers =
        AddTable(layout, limits->buffers, sizeof(mrhiBufferSlot), alignof(mrhiBufferSlot));
    parts.textures =
        AddTable(layout, limits->textures, sizeof(mrhiTextureSlot), alignof(mrhiTextureSlot));
    parts.views = AddTable(layout, limits->views, sizeof(mrhiViewSlot), alignof(mrhiViewSlot));
    parts.querySets =
        AddTable(layout, limits->querySets, sizeof(mrhiQuerySetSlot), alignof(mrhiQuerySetSlot));
    parts.marks =
        mrhiLayoutAdd(layout, limits->queries, sizeof(_Atomic uint64_t), alignof(_Atomic uint64_t));
    parts.heaps = AddTable(layout, limits->heaps, sizeof(mrhiHeapSlot), alignof(mrhiHeapSlot));
    parts.swapchains =
        AddTable(layout, limits->surfaces, sizeof(mrhiSwapchainSlot), alignof(mrhiSwapchainSlot));
    parts.shaders =
        AddTable(layout, limits->shaders, sizeof(mrhiShaderSlot), alignof(mrhiShaderSlot));
    parts.pipelines =
        AddTable(layout, limits->pipelines, sizeof(mrhiPipelineSlot), alignof(mrhiPipelineSlot));
    parts.vertex = mrhiLayoutAdd(layout, (size_t)limits->pipelines * def->limits.vertexBuffers,
                                 sizeof(mrhiVertexFacts), alignof(mrhiVertexFacts));
    return parts;
}

// Starts the device's object tables in its block, zeroing the slots
// whose fields are read while free.
static void PlaceObjectParts(mrhiDevice* device, unsigned char* block, const ObjectParts* parts,
                             const mrhiDeviceLimits* limits)
{
    device->samplerSlots = InitTable(block, parts->samplers, &device->samplers, limits->samplers);
    device->bufferSlots = InitTable(block, parts->buffers, &device->buffers, limits->buffers);
    device->textureSlots = InitTable(block, parts->textures, &device->textures, limits->textures);
    device->viewSlots = InitTable(block, parts->views, &device->views, limits->views);
    device->querySetSlots =
        InitTable(block, parts->querySets, &device->querySets, limits->querySets);
    for (uint32_t i = 0; i < limits->querySets; ++i)
    {
        device->querySetSlots[i] = (mrhiQuerySetSlot){0};
    }
    device->queryMarks = (_Atomic uint64_t*)(block + parts->marks);
    device->heapSlots = InitTable(block, parts->heaps, &device->heaps, limits->heaps);
    for (uint32_t i = 0; i < limits->heaps; ++i)
    {
        device->heapSlots[i] = (mrhiHeapSlot){0};
    }
    device->swapchainSlots =
        InitTable(block, parts->swapchains, &device->swapchains, limits->surfaces);
    device->shaderSlots = InitTable(block, parts->shaders, &device->shaders, limits->shaders);
    for (uint32_t i = 0; i < limits->shaders; ++i)
    {
        device->shaderSlots[i] = (mrhiShaderSlot){0};
    }
    device->pipelineSlots =
        InitTable(block, parts->pipelines, &device->pipelines, limits->pipelines);
    for (uint32_t i = 0; i < limits->pipelines; ++i)
    {
        device->pipelineSlots[i] = (mrhiPipelineSlot){0};
    }
    device->pipelineVertex = (mrhiVertexFacts*)(block + parts->vertex);
}

// Where a frame's tables sit in the device's block.
typedef struct FrameParts
{
    size_t resources;
    size_t passes;
    size_t uses;
    size_t barriers;
    size_t scratch;
    size_t counts;
    size_t boxes;
    size_t boxLimit;
    size_t order;
    size_t vertexBytes;
    size_t chunks;
    uint32_t chunkCount;
    size_t labels;
    size_t driverPasses;
    size_t driverResources;
    size_t driverAccesses;
} FrameParts;

// Lays out the tables of the open frame, sized by the device's limits.
static FrameParts AddFrameParts(mrhiLayout* layout, const mrhiDeviceDef* def)
{
    const mrhiDeviceLimits* limits = &def->deviceLimits;
    FrameParts parts = {
        .boxLimit = (size_t)limits->frameAccesses * 4 + 16,
        .chunkCount = limits->frameCommandBytes / MRHI_CHUNK_BYTES,
    };
    parts.resources = mrhiLayoutAdd(layout, limits->frameResources, sizeof(mrhiFrameResource),
                                    alignof(mrhiFrameResource));
    parts.passes =
        mrhiLayoutAdd(layout, limits->framePasses, sizeof(mrhiFramePass), alignof(mrhiFramePass));
    parts.uses =
        mrhiLayoutAdd(layout, limits->frameAccesses, sizeof(mrhiFrameUse), alignof(mrhiFrameUse));
    parts.barriers =
        mrhiLayoutAdd(layout, limits->frameBarriers, sizeof(mrhiBarrier), alignof(mrhiBarrier));
    parts.scratch =
        mrhiLayoutAdd(layout, limits->frameBarriers, sizeof(mrhiBarrier), alignof(mrhiBarrier));
    parts.counts =
        mrhiLayoutAdd(layout, (size_t)limits->framePasses + 2, sizeof(uint32_t), alignof(uint32_t));
    parts.boxes = mrhiLayoutAdd(layout, parts.boxLimit, sizeof(mrhiBox), alignof(mrhiBox));
    parts.order =
        mrhiLayoutAdd(layout, limits->frameResources, sizeof(uint32_t), alignof(uint32_t));
    parts.vertexBytes =
        mrhiLayoutAdd(layout, (size_t)limits->framePasses * def->limits.vertexBuffers,
                      sizeof(uint64_t), alignof(uint64_t));
    parts.chunks = mrhiLayoutAdd(layout, parts.chunkCount, sizeof(mrhiCommandChunk),
                                 alignof(mrhiCommandChunk));
    parts.labels = mrhiLayoutAdd(layout, (size_t)limits->framePasses * MRHI_LABEL_BYTES, 1, 1);
    parts.driverPasses =
        mrhiLayoutAdd(layout, limits->framePasses, sizeof(mrhiDriverPass), alignof(mrhiDriverPass));
    parts.driverResources = mrhiLayoutAdd(layout, limits->frameResources,
                                          sizeof(mrhiDriverResource), alignof(mrhiDriverResource));
    parts.driverAccesses = mrhiLayoutAdd(layout, limits->frameAccesses, sizeof(mrhiDriverAccess),
                                         alignof(mrhiDriverAccess));
    return parts;
}

// Points the device at its frame's tables in its block.
static void PlaceFrameParts(mrhiDevice* device, unsigned char* block, const FrameParts* parts)
{
    device->frameResources = (mrhiFrameResource*)(block + parts->resources);
    device->framePasses = (mrhiFramePass*)(block + parts->passes);
    device->frameUses = (mrhiFrameUse*)(block + parts->uses);
    device->frameBarriers = (mrhiBarrier*)(block + parts->barriers);
    device->frameBarrierScratch = (mrhiBarrier*)(block + parts->scratch);
    device->frameCounts = (uint32_t*)(block + parts->counts);
    device->frameBoxes = (mrhiBox*)(block + parts->boxes);
    device->frameBoxLimit = (uint32_t)parts->boxLimit;
    device->frameOrder = (uint32_t*)(block + parts->order);
    device->frameVertexBytes = (uint64_t*)(block + parts->vertexBytes);
    device->frameChunks = (mrhiCommandChunk*)(block + parts->chunks);
    device->frameChunkCount = parts->chunkCount;
    device->frameLabels = (char*)(block + parts->labels);
    device->driverPasses = (mrhiDriverPass*)(block + parts->driverPasses);
    device->driverResources = (mrhiDriverResource*)(block + parts->driverResources);
    device->driverAccesses = (mrhiDriverAccess*)(block + parts->driverAccesses);
}

// The device's block: the struct, then its tables, sized by its limits.
static mrhiDevice* Allocate(const mrhiDeviceDef* def)
{
    const mrhiDeviceLimits* limits = &def->deviceLimits;
    mrhiLayout layout = {.size = sizeof(mrhiDevice)};
    ObjectParts objects = AddObjectParts(&layout, def);
    size_t runningAt =
        mrhiLayoutAdd(&layout, def->limits.framesInFlight, sizeof(uint32_t), alignof(uint32_t));
    size_t regionsAt =
        mrhiLayoutAdd(&layout, def->limits.framesInFlight, sizeof(uint32_t), alignof(uint32_t));
    size_t readbacksAt =
        mrhiLayoutAdd(&layout, limits->readbacks, sizeof(mrhiReadback), alignof(mrhiReadback));
    size_t ringAt = mrhiLayoutAdd(&layout, limits->readbackBytes, 1, alignof(max_align_t));
    size_t firstsAt =
        mrhiLayoutAdd(&layout, def->limits.framesInFlight, sizeof(uint32_t), alignof(uint32_t));
    size_t countsInFlightAt =
        mrhiLayoutAdd(&layout, def->limits.framesInFlight, sizeof(uint32_t), alignof(uint32_t));
    size_t stagingAt =
        mrhiLayoutAdd(&layout, (size_t)def->limits.framesInFlight * limits->frameUploadBytes, 1,
                      alignof(max_align_t));
    FrameParts frame = AddFrameParts(&layout, def);
    size_t queueAt = mrhiLayoutAdd(&layout, limits->notifications, sizeof(mrhiDeviceNotification),
                                   alignof(mrhiDeviceNotification));
    unsigned char* block =
        layout.overflow ? nullptr : mrhiAllocate(&def->allocator, layout.size, alignof(mrhiDevice));
    if (block == nullptr)
    {
        return nullptr;
    }
    mrhiDevice* device = (mrhiDevice*)block;
    *device = (mrhiDevice){.bytes = layout.size};
    PlaceObjectParts(device, block, &objects, limits);
    device->running = (uint32_t*)(block + runningAt);
    device->runningRegions = (uint32_t*)(block + regionsAt);
    device->frameStaging = block + stagingAt;
    device->readbacks = (mrhiReadback*)(block + readbacksAt);
    device->readbackRing = block + ringAt;
    device->runningReadbackFirst = (uint32_t*)(block + firstsAt);
    device->runningReadbackCount = (uint32_t*)(block + countsInFlightAt);
    atomic_flag_clear(&device->readbackLock);
    for (uint32_t i = 0; i < limits->readbacks; ++i)
    {
        device->readbacks[i] = (mrhiReadback){0};
    }
    device->queue = (mrhiDeviceNotification*)(block + queueAt);
    PlaceFrameParts(device, block, &frame);
    return device;
}

mrhiResult mrhiCreateDevice(mrhiInstance* instance, const mrhiDeviceDef* def,
                            mrhiDevice** deviceOut, mrhiRequestId* requestOut)
{
    if (instance == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (deviceOut == nullptr || requestOut == nullptr || def == nullptr)
    {
        if (deviceOut != nullptr)
        {
            *deviceOut = nullptr;
        }
        return mrhiMisuse(instance);
    }
    *deviceOut = nullptr;
    mrhiResult status = mrhi_success;
    const mrhiDriverAdapter* adapter = CheckDef(instance, def, &status);
    if (adapter == nullptr)
    {
        return status;
    }
    mrhiDevice* device = Allocate(def);
    if (device == nullptr)
    {
        return mrhi_errorCapacity;
    }
    device->instance = instance;
    device->adapter = adapter->handle;
    device->adapterInfo = adapter->info;
    device->allocator = def->allocator;
    device->features = def->features;
    device->limits = def->limits;
    device->deviceLimits = def->deviceLimits;
    device->state = mrhi_deviceOpening;
    device->request = mrhiNextRequest(instance);
    for (uint32_t i = 0; i < MRHI_KNOWN_FORMATS; ++i)
    {
        mrhiFormat format = mrhiKnownFormats[i];
        device->formatCaps[i] = mrhiFormatFamilyGranted(format, &def->features)
                                    ? mrhiAdapterFormatCaps(instance, adapter, format)
                                    : (mrhiFormatCaps){0};
    }
    // An adapter was found, so the instance has a driver.
    MRHI_ASSERT(instance->driver.vtable != nullptr);
    status = instance->driver.vtable->createDevice(instance->driver.self, adapter->handle, def,
                                                   device->request, &device->driver);
    if (status != mrhi_success)
    {
        mrhiRelease(&device->allocator, device, device->bytes, alignof(mrhiDevice));
        return status;
    }
    device->cacheOutcome =
        mrhiImportPipelineCache(device, def->pipelineCache, def->pipelineCacheBytes);
    ++instance->deviceCount;
    mrhiAddPending(instance, (mrhiPending){
                                 .request = device->request,
                                 .kind = mrhiPendingDevice,
                                 .device = device,
                             });
    *deviceOut = device;
    *requestOut = (mrhiRequestId){device->request, 1};
    return mrhi_success;
}

mrhiResult mrhiFinishOpening(mrhiDevice* device, mrhiResult outcome)
{
    device->state = outcome == mrhi_success ? mrhi_deviceReady : mrhi_deviceFailed;
    return outcome;
}

void mrhiDestroyDevice(mrhiDevice* device)
{
    if (device == nullptr)
    {
        return;
    }
    mrhiInstance* instance = device->instance;
    if (device->state == mrhi_deviceOpening)
    {
        mrhiAnswerNow(instance, device->request, mrhi_errorStale);
    }
    if (device->driver.vtable != nullptr)
    {
        // An open frame is dropped with the device: its images go back.
        if (device->frameOpen)
        {
            mrhiReleaseImages(device);
        }
        mrhiEndConfigurations(device);
        mrhiDestroyHeaps(device);
        mrhiDestroyPipelines(device);
        mrhiDestroyShaders(device);
        device->driver.vtable->destroy(device->driver.self);
    }
    --instance->deviceCount;
    mrhiAllocator allocator = device->allocator;
    mrhiRelease(&allocator, device, device->bytes, alignof(mrhiDevice));
}

mrhiDeviceState mrhiGetDeviceState(mrhiDevice* device)
{
    return device == nullptr ? mrhi_deviceFailed : device->state;
}

mrhiResult mrhiDeviceMisuse(mrhiDevice* device)
{
    atomic_fetch_add_explicit(&device->misuse, 1, memory_order_relaxed);
    return mrhi_errorInvalid;
}

mrhiResult mrhiDeviceUsable(const mrhiDevice* device)
{
    if (device->state == mrhi_deviceLost)
    {
        return mrhi_errorDeviceLost;
    }
    return device->state == mrhi_deviceReady ? mrhi_success : mrhi_errorState;
}

mrhiResult mrhiGetDeviceFeatures(mrhiDevice* device, mrhiFeatures* featuresOut)
{
    if (device == nullptr || featuresOut == nullptr)
    {
        return device == nullptr ? mrhi_errorInvalid : mrhiDeviceMisuse(device);
    }
    *featuresOut = device->features;
    return mrhi_success;
}

mrhiResult mrhiGetDeviceLimits(mrhiDevice* device, mrhiLimits* limitsOut)
{
    if (device == nullptr || limitsOut == nullptr)
    {
        return device == nullptr ? mrhi_errorInvalid : mrhiDeviceMisuse(device);
    }
    *limitsOut = device->limits;
    return mrhi_success;
}

mrhiResult mrhiGetDeviceTimestampPeriod(mrhiDevice* device, double* periodOut)
{
    if (device == nullptr || periodOut == nullptr)
    {
        return device == nullptr ? mrhi_errorInvalid : mrhiDeviceMisuse(device);
    }
    mrhiResult status = mrhiDeviceUsable(device);
    if (status != mrhi_success)
    {
        return status;
    }
    if (!device->features.timestampQuery)
    {
        return mrhi_errorUnsupported;
    }
    *periodOut = device->driver.vtable->timestampPeriod(device->driver.self);
    return mrhi_success;
}

mrhiResult mrhiGetDeviceLossReport(mrhiDevice* device, mrhiDeviceLossReport* reportOut)
{
    if (device == nullptr || reportOut == nullptr)
    {
        return device == nullptr ? mrhi_errorInvalid : mrhiDeviceMisuse(device);
    }
    if (device->state != mrhi_deviceLost)
    {
        return mrhi_errorState;
    }
    *reportOut = device->lossReport;
    return mrhi_success;
}

uint64_t mrhiGetDeviceMisuse(mrhiDevice* device)
{
    return device == nullptr ? 0 : atomic_load_explicit(&device->misuse, memory_order_relaxed);
}

mrhiResult mrhiCheckObjectDef(mrhiDevice* device, mrhiDefHead head, uint32_t expected)
{
    mrhiResult chain = mrhiCheckChain(head.next, nullptr, 0, device->instance->limits.chainDepth);
    if (head.cookie != expected || chain == mrhi_errorInvalid ||
        !mrhiIsLabelValid(head.label, head.labelLength))
    {
        return mrhiDeviceMisuse(device);
    }
    return chain;
}
