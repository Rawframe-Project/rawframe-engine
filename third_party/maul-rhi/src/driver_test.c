// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The test driver: a copy of the def's adapters, devices that hold
// nothing, and a queue of the requests the next poll answers.

#include "driver_test.h"

#include "allocator.h"
#include "capabilities_core.h"
#include "format_caps.h"
#include "frame_walk.h"
#include "invariant.h"

#include <stdalign.h>
#include <string.h>

// The surfaces a test driver holds at once; making more fails as the
// platform's capacity.
#define TEST_SURFACES 16

// A device's first object handle less one: handles pass 32 bits, so a
// handle the core cut to 32 bits names nothing.
#define HANDLE_BASE (UINT64_C(1) << 32)

// A test surface slot.
typedef struct TestSurface
{
    bool used;
    mrhiSurfaceCaps caps;
    uint32_t presentingAdapters;
} TestSurface;

typedef struct TestDriver
{
    mrhiAllocator allocator;
    size_t bytes;
    mrhiTestAdapter* adapters;
    uint32_t adapterCount;
    mrhiDriverEvent* pending;
    uint32_t pendingCount;
    uint32_t pendingLimit;
    // Surfaces not destroyed yet: the core destroys each before the
    // driver. A surface's handle is its slot's index plus one.
    TestSurface surfaces[TEST_SURFACES];
    uint32_t surfaceCount;
} TestDriver;

static mrhiResult CreateSurface(void* self, const mrhiChain* source, const mrhiSurfaceDef* def,
                                uint64_t* handleOut)
{
    (void)def;
    TestDriver* driver = self;
    if (source->type != mrhi_structSurfaceSourceTest)
    {
        return mrhi_errorUnsupported;
    }
    const mrhiSurfaceSourceTest* test = (const mrhiSurfaceSourceTest*)source;
    if (test->fail)
    {
        return mrhi_errorPlatform;
    }
    uint32_t slot = 0;
    while (slot < TEST_SURFACES && driver->surfaces[slot].used)
    {
        ++slot;
    }
    if (slot == TEST_SURFACES)
    {
        return mrhi_errorCapacity;
    }
    driver->surfaces[slot] = (TestSurface){
        .used = true,
        .caps = test->caps,
        .presentingAdapters = test->presentingAdapters,
    };
    ++driver->surfaceCount;
    *handleOut = slot + 1;
    return mrhi_success;
}

static void DestroySurface(void* self, uint64_t handle)
{
    TestDriver* driver = self;
    MRHI_ASSERT(handle != 0 && handle <= TEST_SURFACES && driver->surfaces[handle - 1].used);
    driver->surfaces[handle - 1].used = false;
    --driver->surfaceCount;
}

// The surface's caps with the floors added, on the adapters that
// present to it.
static void GetSurfaceCaps(const void* self, uint64_t surface, uint64_t adapter,
                           mrhiSurfaceCaps* capsOut)
{
    const TestDriver* driver = self;
    MRHI_ASSERT(surface != 0 && surface <= TEST_SURFACES && driver->surfaces[surface - 1].used);
    const TestSurface* described = &driver->surfaces[surface - 1];
    if (adapter > 32 || (described->presentingAdapters & (1u << (adapter - 1))) == 0)
    {
        *capsOut = (mrhiSurfaceCaps){0};
        return;
    }
    mrhiSurfaceCaps caps = described->caps;
    caps.presentable = true;
    if (caps.colorCount == 0 || caps.colorCount > MRHI_SURFACE_COLORS)
    {
        caps.colorCount = 1;
        caps.colors[0] = (mrhiSurfaceColor){.format = mrhi_formatBgra8Unorm};
    }
    caps.presentModes |= mrhi_presentFifo;
    caps.alphaModes |= mrhi_alphaOpaque;
    caps.usages |= mrhi_textureRenderTarget;
    // The sRGB floor: views of the twin unless the test gives images.
    caps.twinViews = caps.twinViews || !caps.twinImages;
    *capsOut = caps;
}

static mrhiResult RequestAdapters(void* self, uint64_t tag)
{
    TestDriver* driver = self;
    if (driver->pendingCount == driver->pendingLimit)
    {
        return mrhi_errorCapacity;
    }
    driver->pending[driver->pendingCount++] =
        (mrhiDriverEvent){.tag = tag, .outcome = mrhi_success};
    return mrhi_success;
}

static size_t Poll(void* self, mrhiDriverEvent* events, size_t capacity)
{
    TestDriver* driver = self;
    size_t moved = driver->pendingCount < capacity ? driver->pendingCount : capacity;
    memcpy(events, driver->pending, moved * sizeof(mrhiDriverEvent));
    memmove(driver->pending, driver->pending + moved,
            (driver->pendingCount - moved) * sizeof(mrhiDriverEvent));
    driver->pendingCount -= (uint32_t)moved;
    return moved;
}

static size_t GetAdapters(const void* self, mrhiDriverAdapter* adapters, size_t capacity)
{
    const TestDriver* driver = self;
    for (size_t i = 0; i < driver->adapterCount && i < capacity; ++i)
    {
        const mrhiTestAdapter* adapter = &driver->adapters[i];
        adapters[i] = (mrhiDriverAdapter){
            .handle = i + 1,
            .info = adapter->info,
            .features = adapter->features,
            .limits = adapter->limits,
        };
    }
    return driver->adapterCount;
}

// The views a test device holds at once; making more fails as the
// platform.
#define TEST_VIEWS 64

// The frames a test device runs at once.
#define TEST_FRAMES 16

// The pipeline creations a test device holds unanswered.
#define TEST_PIPELINES 64

// A frame the test device runs, and whether a wait finished it.
typedef struct TestFrame
{
    uint64_t tag;
    bool done;
} TestFrame;

// A pipeline creation the next poll answers.
typedef struct TestPipeline
{
    uint64_t tag;
    uint64_t handle;
} TestPipeline;

typedef struct TestView
{
    uint64_t handle;
    uint64_t texture;
} TestView;

typedef struct TestDevice
{
    mrhiAllocator allocator;
    uint64_t nextHandle;
    // Objects made so far, and the count after which making fails; 0
    // for no failure.
    uint32_t made;
    uint32_t madeBeforeFailure;
    uint32_t samplers;
    uint32_t buffers;
    uint64_t bufferBytes;
    uint32_t textures;
    uint32_t querySets;
    uint32_t heaps;
    // Live shaders and pipelines; the core destroys each before the device.
    uint32_t shaders;
    uint32_t pipelines;
    // Pipeline creations not answered yet, and what they are answered with.
    TestPipeline pending[TEST_PIPELINES];
    uint32_t pendingCount;
    mrhiResult pipelineOutcome;
    // Nanoseconds per timestamp tick.
    double timestampPeriod;
    // Where walked frames are reported, or NULL; what acquiring answers,
    // or NULL for success; and the images acquired and not yet presented
    // or given back.
    mrhiTestFrameLog* frameLog;
    // The objects made with a label.
    uint32_t named;
    const mrhiResult* acquireOutcome;
    uint32_t imagesOut;
    // The flag that loses the device, why, and whether it is lost and the
    // loss reported.
    const bool* loseDevice;
    mrhiDeviceLossReason lossReason;
    bool lost;
    bool lossReported;
    // Whether the core has been told, which asks for no more work.
    bool lossTold;
    // The first frame running when it was lost, its tag, or 0.
    uint64_t faultingTag;
    // Pipelines made, and those a pipeline cache said earlier devices made.
    uint64_t pipelinesMade;
    uint64_t pipelinesCached;
    // The last label an object was given, copied as a real driver would.
    char label[MRHI_LABEL_BYTES + 1];
    // Configured surfaces; the core ends each before the device.
    uint32_t swapchains;
    // Running frames; held ones finish only through a wait.
    bool holdFrames;
    mrhiResult frameOutcome;
    TestFrame frames[TEST_FRAMES];
    uint32_t frameCount;
    // The live views and their textures, so that a texture destroyed
    // before its views traps.
    TestView views[TEST_VIEWS];
    uint32_t viewCount;
} TestDevice;

// Copies a label into the fixed, terminated buffer the core's checks
// allow.
static void Name(TestDevice* device, const char* label, size_t length)
{
    MRHI_ASSERT(length <= MRHI_LABEL_BYTES && (label != nullptr || length == 0));
    device->named += length > 0 ? 1 : 0;
    for (size_t i = 0; i < length; ++i)
    {
        MRHI_ASSERT(label[i] != '\0');
        device->label[i] = label[i];
    }
    device->label[length] = '\0';
}

// Whether the device is lost: once its adapter's flag is set, for good,
// the frame it was running then taken as the faulting one.
static bool IsLost(TestDevice* device)
{
    if (!device->lost && device->loseDevice != nullptr && *device->loseDevice)
    {
        device->lost = true;
        device->faultingTag = device->frameCount > 0 ? device->frames[0].tag : 0;
    }
    return device->lost;
}

// A new object's handle, mrhi_errorDeviceLost on a lost device, or
// mrhi_errorPlatform once the adapter's object budget is spent.
static mrhiResult MakeObject(TestDevice* device, uint64_t* handleOut)
{
    MRHI_ASSERT(!device->lossTold);
    if (IsLost(device))
    {
        device->lossTold = true;
        return mrhi_errorDeviceLost;
    }
    if (device->madeBeforeFailure != 0 && device->made == device->madeBeforeFailure)
    {
        return mrhi_errorPlatform;
    }
    ++device->made;
    *handleOut = ++device->nextHandle;
    return mrhi_success;
}

static mrhiResult CreateSampler(void* self, const mrhiSamplerDef* def, uint64_t* handleOut)
{
    TestDevice* device = self;
    Name(device, def->label, def->labelLength);
    mrhiResult status = MakeObject(device, handleOut);
    device->samplers += status == mrhi_success ? 1 : 0;
    return status;
}

static mrhiResult CreateBuffer(void* self, const mrhiBufferDef* def, uint64_t* handleOut)
{
    TestDevice* device = self;
    Name(device, def->label, def->labelLength);
    mrhiResult status = MakeObject(device, handleOut);
    if (status == mrhi_success)
    {
        ++device->buffers;
        device->bufferBytes += def->size;
    }
    return status;
}

static void DestroyBuffer(void* self, uint64_t handle)
{
    TestDevice* device = self;
    MRHI_ASSERT(handle != 0 && handle <= device->nextHandle && device->buffers > 0);
    --device->buffers;
}

static mrhiResult CreateTexture(void* self, const mrhiTextureDef* def, uint64_t* handleOut)
{
    TestDevice* device = self;
    Name(device, def->label, def->labelLength);
    mrhiResult status = MakeObject(device, handleOut);
    device->textures += status == mrhi_success ? 1 : 0;
    return status;
}

static void DestroyTexture(void* self, uint64_t handle)
{
    TestDevice* device = self;
    MRHI_ASSERT(handle != 0 && handle <= device->nextHandle && device->textures > 0);
    for (uint32_t i = 0; i < device->viewCount; ++i)
    {
        MRHI_ASSERT(device->views[i].texture != handle);
    }
    --device->textures;
}

static mrhiResult CreateView(void* self, uint64_t texture, const mrhiViewDef* def,
                             uint64_t* handleOut)
{
    TestDevice* device = self;
    Name(device, def->label, def->labelLength);
    MRHI_ASSERT(texture != 0 && texture <= device->nextHandle);
    if (device->viewCount == TEST_VIEWS)
    {
        return mrhi_errorPlatform;
    }
    mrhiResult status = MakeObject(device, handleOut);
    if (status == mrhi_success)
    {
        device->views[device->viewCount++] = (TestView){*handleOut, texture};
    }
    return status;
}

static void DestroyView(void* self, uint64_t handle)
{
    TestDevice* device = self;
    uint32_t i = 0;
    while (i < device->viewCount && device->views[i].handle != handle)
    {
        ++i;
    }
    MRHI_ASSERT(handle != 0 && i < device->viewCount);
    device->views[i] = device->views[--device->viewCount];
}

static mrhiResult CreateQuerySet(void* self, const mrhiQuerySetDef* def, uint64_t* handleOut)
{
    TestDevice* device = self;
    Name(device, def->label, def->labelLength);
    mrhiResult status = MakeObject(device, handleOut);
    device->querySets += status == mrhi_success ? 1 : 0;
    return status;
}

static void DestroyQuerySet(void* self, uint64_t handle)
{
    TestDevice* device = self;
    MRHI_ASSERT(handle != 0 && handle <= device->nextHandle && device->querySets > 0);
    --device->querySets;
}

static mrhiResult CreateHeap(void* self, const mrhiHeapDef* def, uint64_t* handleOut)
{
    TestDevice* device = self;
    Name(device, def->label, def->labelLength);
    mrhiResult status = MakeObject(device, handleOut);
    device->heaps += status == mrhi_success ? 1 : 0;
    return status;
}

static void DestroyHeap(void* self, uint64_t handle)
{
    TestDevice* device = self;
    MRHI_ASSERT(handle != 0 && handle <= device->nextHandle && device->heaps > 0);
    --device->heaps;
}

// Whether a handle is one the device made.
static bool IsMade(const TestDevice* device, uint64_t handle)
{
    return handle > HANDLE_BASE && handle <= device->nextHandle;
}

static void WriteHeapEntry(void* self, uint64_t heap, uint32_t index,
                           const mrhiDriverHeapEntry* entry)
{
    (void)index;
    const TestDevice* device = self;
    MRHI_ASSERT(IsMade(device, heap) && IsMade(device, entry->handle) &&
                entry->kind <= mrhi_heapStorageBuffer &&
                (entry->kind == mrhi_heapStorageBuffer ? entry->size > 0
                                                       : entry->size == 0 && entry->offset == 0));
}

static void WriteHeapSampler(void* self, uint64_t heap, uint32_t index, uint64_t sampler)
{
    (void)index;
    const TestDevice* device = self;
    MRHI_ASSERT(IsMade(device, heap) && IsMade(device, sampler));
}

static double TimestampPeriod(void* self)
{
    const TestDevice* device = self;
    return device->timestampPeriod;
}

static void DestroySampler(void* self, uint64_t handle)
{
    TestDevice* device = self;
    MRHI_ASSERT(handle != 0 && handle <= device->nextHandle && device->samplers > 0);
    --device->samplers;
}

// A shader, from a container whose code sections the core has checked.
static mrhiResult CreateShader(void* self, const mrhiShaderDef* def, const mrhiContainer* container,
                               uint64_t* handleOut)
{
    TestDevice* device = self;
    Name(device, def->label, def->labelLength);
    // A container has WGSL exactly when no entry uses a heap or the view
    // index and its code no 64-bit integers.
    MRHI_ASSERT(container->entryCount > 0 && container->spirvBytes >= 20 &&
                (container->wgslBytes > 0) ==
                    (container->heapUses == 0 &&
                     (container->builtins & mrhi_builtinViewIndex) == 0 && !container->int64));
    mrhiResult status = MakeObject(device, handleOut);
    device->shaders += status == mrhi_success ? 1 : 0;
    return status;
}

static void DestroyShader(void* self, uint64_t handle)
{
    TestDevice* device = self;
    MRHI_ASSERT(handle != 0 && handle <= device->nextHandle && device->shaders > 0);
    --device->shaders;
}

// Holds a pipeline's creation for the next poll: its handle, or
// mrhi_errorPlatform once the device holds 64 or its budget is spent.
static mrhiResult HoldPipeline(TestDevice* device, uint64_t tag, uint64_t* handleOut)
{
    if (device->pendingCount == TEST_PIPELINES)
    {
        return mrhi_errorPlatform;
    }
    mrhiResult status = MakeObject(device, handleOut);
    if (status == mrhi_success)
    {
        device->pending[device->pendingCount++] = (TestPipeline){tag, *handleOut};
        ++device->pipelines;
        ++device->pipelinesMade;
    }
    return status;
}

// A test pipeline cache: a magic, then the pipelines made by the devices
// before, as a real cache holds what they compiled.
static const char TEST_CACHE_MAGIC[8] = {'M', 'R', 'H', 'I', 'T', 'E', 'S', 'T'};

static bool ImportPipelineCache(void* self, const void* bytes, size_t size)
{
    TestDevice* device = self;
    const unsigned char* data = bytes;
    if (size != 16 || memcmp(data, TEST_CACHE_MAGIC, sizeof(TEST_CACHE_MAGIC)) != 0)
    {
        return false;
    }
    uint64_t count = 0;
    for (int i = 7; i >= 0; --i)
    {
        count = count << 8 | data[8 + i];
    }
    device->pipelinesCached = count;
    return true;
}

static size_t ExportPipelineCache(void* self, void* bytes, size_t capacity)
{
    TestDevice* device = self;
    if (capacity >= 16)
    {
        unsigned char* data = bytes;
        memcpy(data, TEST_CACHE_MAGIC, sizeof(TEST_CACHE_MAGIC));
        uint64_t count = device->pipelinesCached + device->pipelinesMade;
        for (int i = 0; i < 8; ++i)
        {
            data[8 + i] = (unsigned char)(count >> (8 * i));
        }
    }
    return 16;
}

static mrhiResult CreateComputePipeline(void* self, const mrhiDriverComputePipeline* pipeline,
                                        uint64_t tag, uint64_t* handleOut)
{
    TestDevice* device = self;
    Name(device, pipeline->label, pipeline->labelLength);
    MRHI_ASSERT(pipeline->shader != 0 && tag > UINT32_MAX &&
                pipeline->entry < pipeline->reflection->entryCount &&
                pipeline->reflection->entries[pipeline->entry].stage == mrhi_stageCompute);
    return HoldPipeline(device, tag, handleOut);
}

static mrhiResult CreateGraphicsPipeline(void* self, const mrhiDriverGraphicsPipeline* pipeline,
                                         uint64_t tag, uint64_t* handleOut)
{
    TestDevice* device = self;
    Name(device, pipeline->def->label, pipeline->def->labelLength);
    MRHI_ASSERT(
        pipeline->shader != 0 && tag > UINT32_MAX &&
        pipeline->vertexEntry < pipeline->reflection->entryCount &&
        pipeline->reflection->entries[pipeline->vertexEntry].stage == mrhi_stageVertex &&
        (pipeline->fragmentEntry == pipeline->reflection->entryCount ||
         pipeline->reflection->entries[pipeline->fragmentEntry].stage == mrhi_stageFragment));
    return HoldPipeline(device, tag, handleOut);
}

// Destroys a pipeline, dropping its answer if it is still pending.
static void DestroyPipeline(void* self, uint64_t handle)
{
    TestDevice* device = self;
    MRHI_ASSERT(handle != 0 && handle <= device->nextHandle && device->pipelines > 0);
    for (uint32_t i = 0; i < device->pendingCount; ++i)
    {
        if (device->pending[i].handle == handle)
        {
            device->pending[i] = device->pending[--device->pendingCount];
            break;
        }
    }
    --device->pipelines;
}

static mrhiResult ConfigureSurface(void* self, uint64_t surface, const mrhiSurfaceConfig* config,
                                   uint64_t oldSwapchain, uint64_t* swapchainOut)
{
    (void)config;
    TestDevice* device = self;
    MRHI_ASSERT(surface != 0);
    if (oldSwapchain != 0)
    {
        MRHI_ASSERT(device->swapchains > 0);
        --device->swapchains;
    }
    mrhiResult status = MakeObject(device, swapchainOut);
    device->swapchains += status == mrhi_success ? 1 : 0;
    return status;
}

// Acquires an image as the adapter's test says: a new handle each time,
// or none.
static mrhiResult AcquireImage(void* self, uint64_t swapchain, uint64_t* imageOut)
{
    TestDevice* device = self;
    MRHI_ASSERT(swapchain > HANDLE_BASE && swapchain <= device->nextHandle);
    MRHI_ASSERT(!device->lossTold);
    mrhiResult outcome = device->acquireOutcome == nullptr ? mrhi_success : *device->acquireOutcome;
    if (IsLost(device))
    {
        device->lossTold = true;
        outcome = mrhi_errorDeviceLost;
    }
    if (outcome != mrhi_success && outcome != mrhi_suboptimal)
    {
        return outcome;
    }
    mrhiResult status = MakeObject(device, imageOut);
    device->imagesOut += status == mrhi_success ? 1 : 0;
    return status == mrhi_success ? outcome : status;
}

static void ReleaseImage(void* self, uint64_t swapchain, uint64_t image)
{
    TestDevice* device = self;
    MRHI_ASSERT(swapchain > HANDLE_BASE && image > HANDLE_BASE && device->imagesOut > 0);
    --device->imagesOut;
}

static void UnconfigureSurface(void* self, uint64_t swapchain)
{
    TestDevice* device = self;
    MRHI_ASSERT(swapchain != 0 && swapchain <= device->nextHandle && device->swapchains > 0);
    --device->swapchains;
}

// A texture's bytes: 4 per texel and sample, doubled for mips; none for
// a transient texture, as a tile GPU keeps it on chip.
static void TextureMemory(const void* self, const mrhiTextureDef* def, uint64_t* bytesOut,
                          uint64_t* alignmentOut)
{
    (void)self;
    uint64_t texels = (uint64_t)def->width * def->height * def->depthOrLayers * def->sampleCount;
    bool transient = (def->usage & mrhi_textureTransient) != 0;
    *bytesOut = transient ? 0 : texels * 4 * (def->mipLevels > 1 ? 2 : 1);
    *alignmentOut = 256;
}

// A buffer's bytes, rounded up to its alignment of 256.
static void BufferMemory(const void* self, const mrhiBufferDef* def, uint64_t* bytesOut,
                         uint64_t* alignmentOut)
{
    (void)self;
    *bytesOut = (def->size + 255) & ~(uint64_t)255;
    *alignmentOut = 256;
}

// IsMade, as a frame walk checks handles.
static bool IsHandleOf(const void* self, uint64_t handle)
{
    return IsMade(self, handle);
}

// Walks the frame as a driver would translate it, then runs it at once.
static mrhiResult SubmitFrame(void* self, const mrhiDriverFrame* frame, uint64_t tag)
{
    TestDevice* device = self;
    MRHI_ASSERT(tag != 0 && device->frameCount < TEST_FRAMES && !device->lossTold);
    if (IsLost(device))
    {
        device->lossTold = true;
        return mrhi_errorDeviceLost;
    }
    // A fault means the core recorded what no driver could translate:
    // the walk is kept in every build the test driver is part of, since
    // a program's own tests rely on it whatever NDEBUG says.
    mrhiTestFrameLog log = {0};
    if (mrhiWalkFrame(frame, IsHandleOf, device, &log) != 0)
    {
        __builtin_trap();
    }
    // Every image the frame acquired is presented.
    MRHI_ASSERT(log.presented <= device->imagesOut);
    device->imagesOut -= log.presented;
    if (device->frameLog != nullptr)
    {
        log.frames = device->frameLog->frames + 1;
        log.named = device->named;
        *device->frameLog = log;
    }
    uint64_t handle = 0;
    mrhiResult status = MakeObject(device, &handle);
    if (status == mrhi_success)
    {
        device->frames[device->frameCount++] = (TestFrame){.tag = tag};
    }
    return status;
}

// Reports every pending pipeline, then the frames that finished: all of
// them, or only those a wait finished when frames are held.
static size_t PollDevice(void* self, mrhiDriverEvent* events, size_t capacity)
{
    TestDevice* device = self;
    size_t moved = 0;
    // A lost device reports its loss once, then what it had finished, as a
    // driver's queue may still hold it; the core has answered those.
    if (IsLost(device))
    {
        if (device->lossReported || capacity == 0)
        {
            return 0;
        }
        device->lossReported = true;
        device->lossTold = true;
        events[moved++] = (mrhiDriverEvent){.tag = 0, .outcome = mrhi_errorDeviceLost};
        while (device->pendingCount > 0 && moved < capacity)
        {
            events[moved++] = (mrhiDriverEvent){.tag = device->pending[--device->pendingCount].tag,
                                                .outcome = mrhi_success};
        }
        while (device->frameCount > 0 && moved < capacity)
        {
            events[moved++] = (mrhiDriverEvent){.tag = device->frames[--device->frameCount].tag,
                                                .outcome = mrhi_success};
        }
        return moved;
    }
    while (device->pendingCount > 0 && moved < capacity)
    {
        events[moved++] = (mrhiDriverEvent){.tag = device->pending[--device->pendingCount].tag,
                                            .outcome = device->pipelineOutcome};
    }
    uint32_t i = 0;
    while (i < device->frameCount && moved < capacity)
    {
        if (device->holdFrames && !device->frames[i].done)
        {
            ++i;
            continue;
        }
        events[moved++] =
            (mrhiDriverEvent){.tag = device->frames[i].tag, .outcome = device->frameOutcome};
        device->frames[i] = device->frames[--device->frameCount];
    }
    return moved;
}

// A frame that is not held has finished; a held one finishes when waited
// on with a nonzero timeout, and a zero timeout only checks.
static bool WaitFrame(void* self, uint64_t tag, uint64_t timeoutNs)
{
    TestDevice* device = self;
    for (uint32_t i = 0; i < device->frameCount; ++i)
    {
        if (device->frames[i].tag == tag)
        {
            device->frames[i].done = device->frames[i].done || !device->holdFrames || timeoutNs > 0;
            return device->frames[i].done;
        }
    }
    // The core asks only about frames it has not seen finish.
    MRHI_ASSERT(false);
    return true;
}

// The reason the adapter gives, the frame running when it was lost as the
// one that faulted, and a message.
static void LossReport(void* self, mrhiDeviceLossReport* reportOut)
{
    TestDevice* device = self;
    static const char message[] = "the test driver lost its device";
    *reportOut = (mrhiDeviceLossReport){
        .reason = device->lossReason,
        .messageLength = sizeof(message) - 1,
    };
    memcpy(reportOut->message, message, sizeof(message) - 1);
    if (device->faultingTag != 0)
    {
        reportOut->faultingFrame = (mrhiRequestId){(uint32_t)device->faultingTag, 1};
        reportOut->faultingPass = 1;
    }
}

static void DestroyDevice(void* self)
{
    TestDevice* device = self;
    MRHI_ASSERT(device->swapchains == 0 && device->shaders == 0 && device->pipelines == 0 &&
                device->imagesOut == 0);
    mrhiAllocator allocator = device->allocator;
    mrhiRelease(&allocator, device, sizeof(TestDevice), alignof(TestDevice));
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
    .createQuerySet = CreateQuerySet,
    .destroyQuerySet = DestroyQuerySet,
    .createHeap = CreateHeap,
    .destroyHeap = DestroyHeap,
    .writeHeapEntry = WriteHeapEntry,
    .writeHeapSampler = WriteHeapSampler,
    .lossReport = LossReport,
    .acquireImage = AcquireImage,
    .releaseImage = ReleaseImage,
    .timestampPeriod = TimestampPeriod,
    .importPipelineCache = ImportPipelineCache,
    .exportPipelineCache = ExportPipelineCache,
    .textureMemory = TextureMemory,
    .bufferMemory = BufferMemory,
    .submitFrame = SubmitFrame,
    .poll = PollDevice,
    .waitFrame = WaitFrame,
};

static mrhiResult CreateDevice(void* self, uint64_t adapter, const mrhiDeviceDef* def, uint64_t tag,
                               mrhiDeviceDriver* deviceOut)
{
    TestDriver* driver = self;
    if (driver->pendingCount == driver->pendingLimit)
    {
        return mrhi_errorCapacity;
    }
    TestDevice* device = mrhiAllocate(&def->allocator, sizeof(TestDevice), alignof(TestDevice));
    if (device == nullptr)
    {
        return mrhi_errorCapacity;
    }
    *device = (TestDevice){
        .allocator = def->allocator,
        .nextHandle = HANDLE_BASE,
        .madeBeforeFailure = driver->adapters[adapter - 1].objectsBeforeFailure,
        .holdFrames = driver->adapters[adapter - 1].holdFrames,
        .frameOutcome = driver->adapters[adapter - 1].frameOutcome,
        .pipelineOutcome = driver->adapters[adapter - 1].pipelineOutcome,
        .frameLog = driver->adapters[adapter - 1].frameLog,
        .acquireOutcome = driver->adapters[adapter - 1].acquireOutcome,
        .loseDevice = driver->adapters[adapter - 1].loseDevice,
        .lossReason = driver->adapters[adapter - 1].lossReason,
        .timestampPeriod = driver->adapters[adapter - 1].timestampPeriod > 0.0
                               ? driver->adapters[adapter - 1].timestampPeriod
                               : 1.0,
    };
    Name(device, def->label, def->labelLength);
    driver->pending[driver->pendingCount++] = (mrhiDriverEvent){
        .tag = tag,
        .outcome = driver->adapters[adapter - 1].openOutcome,
    };
    *deviceOut = (mrhiDeviceDriver){.vtable = &s_deviceVtable, .self = device};
    return mrhi_success;
}

// The floor, and what the adapter's features add: filtering of 32-bit
// floats, rendering to rg11b10, and sampling of a granted compressed
// family.
static void GetFormatCaps(const void* self, uint64_t adapter, mrhiFormat format,
                          mrhiFormatCaps* capsOut)
{
    const TestDriver* driver = self;
    const mrhiTestAdapter* described = &driver->adapters[adapter - 1];
    if (format == described->limitedFormat)
    {
        *capsOut = described->limitedCaps;
        return;
    }
    *capsOut = mrhiGrantedFormatCaps(format, &described->features);
}

static void Destroy(void* self)
{
    TestDriver* driver = self;
    MRHI_ASSERT(driver->surfaceCount == 0);
    mrhiAllocator allocator = driver->allocator;
    mrhiRelease(&allocator, driver, driver->bytes, alignof(TestDriver));
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

mrhiResult mrhiCreateTestDriver(const mrhiAllocator* allocator, const mrhiTestDriverDef* def,
                                uint32_t pendingLimit, mrhiInstanceDriver* driverOut)
{
    if (def->adapterCount > 0 && def->adapters == nullptr)
    {
        return mrhi_errorInvalid;
    }
    mrhiLayout layout = {.size = sizeof(TestDriver)};
    size_t adaptersAt = mrhiLayoutAdd(&layout, def->adapterCount, sizeof(mrhiTestAdapter),
                                      alignof(mrhiTestAdapter));
    size_t pendingAt =
        mrhiLayoutAdd(&layout, pendingLimit, sizeof(mrhiDriverEvent), alignof(mrhiDriverEvent));
    TestDriver* driver =
        layout.overflow ? nullptr : mrhiAllocate(allocator, layout.size, alignof(TestDriver));
    if (driver == nullptr)
    {
        return mrhi_errorCapacity;
    }
    unsigned char* block = (unsigned char*)driver;
    *driver = (TestDriver){
        .allocator = *allocator,
        .bytes = layout.size,
        .adapters = (mrhiTestAdapter*)(block + adaptersAt),
        .adapterCount = def->adapterCount,
        .pending = (mrhiDriverEvent*)(block + pendingAt),
        .pendingLimit = pendingLimit,
    };
    size_t adapterBytes = def->adapterCount * sizeof(mrhiTestAdapter);
    if (adapterBytes > 0)
    {
        memcpy(driver->adapters, def->adapters, adapterBytes);
    }
    *driverOut = (mrhiInstanceDriver){.vtable = &s_vtable, .self = driver};
    return mrhi_success;
}
