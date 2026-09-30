// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A WebGPU device (mrhi-0003): opened from a fresh adapter with the
// granted features and limits, its errors kept where the web test runner
// reads them (a validation scope held for its life, read as it closes),
// and its loss reported by the next poll. Objects
// live on the JavaScript side under small handles. A destroyed object
// waits there until the next frame submitted after its destruction
// finishes, since frames are recorded at submission; frames are
// reported finished in order. WebGPU places memory itself,
// so a declared resource's bytes are an estimate for the frame's report.

#include "webgpu_device.h"

#include "allocator.h"
#include "capabilities_core.h"
#include "invariant.h"
#include "web_js.h"
#include "webgpu_frame.h"
#include "webgpu_names.h"
#include "webgpu_pipeline.h"

#include <stdalign.h>
#include <string.h>

// A pipeline waiting for the browser: its handle and its tag.
typedef struct Pending
{
    uint64_t handle;
    uint64_t tag;
} Pending;

typedef struct WebGpuDevice
{
    mrhiAllocator allocator;
    size_t bytes;
    // Its state on the JavaScript side.
    int state;
    bool lossTold;
    // The pipelines waiting, at most the device's pipelines.
    Pending* pending;
    uint32_t pendingCount;
    uint32_t pendingLimit;
    // Frames submitted and reported finished, and the tags of those
    // running by number, at most the frames in flight.
    uint64_t submitted;
    uint64_t finished;
    uint64_t* tags;
    uint32_t tagCount;
} WebGpuDevice;

// clang-format off
EM_JS(int, mrhiJsCreateDeviceState, (void), {
    return Module.mrhiGpu.add({device: null, objects: [null], free: [], retiring: [], lost: null,
                               features: [], limits: {}, pipelines: [], frame: null,
                               running: [], finished: 0, pool: [], readbacks: [],
                               staging: null});
});

EM_JS(void, mrhiJsWantFeature, (int state, const char* feature), {
    Module.mrhiGpu.states[state].features.push(UTF8ToString(feature));
});

EM_JS(void, mrhiJsWantLimit, (int state, const char* limit, double value), {
    Module.mrhiGpu.states[state].limits[UTF8ToString(limit)] = value;
});

// Asks a fresh adapter for the device; the instance's slot settles with
// success or the failure given.
EM_JS(void, mrhiJsOpenDevice, (int instance, int state, uint32_t slot, const char* label,
                           int labelLength, int failure), {
    const gpu = Module.mrhiGpu;
    const self = gpu.states[state];
    const owner = gpu.states[instance];
    const settle = outcome => {
        if (gpu.states[instance] === owner) {
            owner.settled.push([slot, outcome]);
        }
    };
    const descriptor = {
        label: UTF8ToString(label, labelLength),
        requiredFeatures: self.features,
        requiredLimits: self.limits,
    };
    navigator.gpu.requestAdapter()
        .then(adapter => adapter.requestDevice(descriptor))
        .then(device => {
            if (gpu.states[state] !== self) {
                device.destroy();
            } else {
                self.device = device;
                // Errors reach a scope held for the device's life, read as it
                // closes; any outside it are uncaptured.
                device.pushErrorScope("validation");
                device.onuncapturederror = event => gpu.errors.push(event.error.message);
                device.lost.then(info => {
                    if (gpu.states[state] === self) {
                        self.lost = info;
                    }
                });
            }
            settle(0);
        })
        .catch(() => settle(failure));
});

// Destroys the device and every object it holds, once its error scope
// is read: the closings still reading are counted.
EM_JS(void, mrhiJsCloseDevice, (int state), {
    const gpu = Module.mrhiGpu;
    const self = gpu.states[state];
    gpu.states[state] = null;
    const destroy = () => {
        for (const object of self.objects) {
            if (object && object.destroy) {
                object.destroy();
            }
        }
        self.pool.forEach(entry => entry.object.destroy());
        self.readbacks.forEach(buffer => buffer.destroy());
        if (self.staging) {
            self.staging.destroy();
        }
        if (self.device) {
            self.device.destroy();
        }
    };
    if (!self.device) {
        destroy();
        return;
    }
    gpu.closing = (gpu.closing || 0) + 1;
    self.device.popErrorScope()
        .then(error => {
            if (error) {
                gpu.errors.push(error.message);
            }
        }, () => {})
        .finally(() => {
            destroy();
            gpu.closing -= 1;
        });
});

EM_JS(bool, mrhiJsIsLost, (int state), {
    return Module.mrhiGpu.states[state].lost !== null;
});

// Writes the loss's message and returns its bytes.
EM_JS(int, mrhiJsLossMessage, (int state, char* out, int capacity), {
    const lost = Module.mrhiGpu.states[state].lost;
    return stringToUTF8(lost ? lost.message : "", out, capacity);
});

EM_JS(int, mrhiJsCreateBuffer, (int state, double size, uint32_t usage), {
    const gpu = Module.mrhiGpu;
    const self = gpu.states[state];
    return gpu.put(self, gpu.buffer(self.device, size, usage));
});

EM_JS(int, mrhiJsCreateTexture, (int state, bool volume, const char* format, uint32_t width,
                             uint32_t height, uint32_t depth, uint32_t mips, uint32_t samples,
                             uint32_t usage, const char* viewFormats), {
    const gpu = Module.mrhiGpu;
    const self = gpu.states[state];
    return gpu.put(self, gpu.texture(self.device, volume, UTF8ToString(format), width, height,
                                     depth, mips, samples, usage, UTF8ToString(viewFormats)));
});

EM_JS(int, mrhiJsCreateView, (int state, int texture, const char* format, const char* dimension,
                          const char* aspect, uint32_t baseMip, uint32_t mips, uint32_t baseLayer,
                          uint32_t layers), {
    const descriptor = {
        dimension: UTF8ToString(dimension),
        aspect: UTF8ToString(aspect),
        baseMipLevel: baseMip,
        mipLevelCount: mips,
        baseArrayLayer: baseLayer,
        arrayLayerCount: layers,
    };
    // A view of one aspect takes that aspect's format, which the browser
    // resolves.
    if (descriptor.aspect === "all") {
        descriptor.format = UTF8ToString(format);
    }
    const gpu = Module.mrhiGpu;
    const self = gpu.states[state];
    return gpu.put(self, self.objects[texture].createView(descriptor));
});

EM_JS(int, mrhiJsCreateSampler, (int state, const char* addressU, const char* addressV,
                             const char* addressW, const char* mag, const char* min,
                             const char* mip, float lodMin, float lodMax, const char* compare,
                             uint32_t anisotropy), {
    const descriptor = {
        addressModeU: UTF8ToString(addressU),
        addressModeV: UTF8ToString(addressV),
        addressModeW: UTF8ToString(addressW),
        magFilter: UTF8ToString(mag),
        minFilter: UTF8ToString(min),
        mipmapFilter: UTF8ToString(mip),
        lodMinClamp: lodMin,
        lodMaxClamp: lodMax,
        maxAnisotropy: anisotropy,
    };
    if (compare) {
        descriptor.compare = UTF8ToString(compare);
    }
    const gpu = Module.mrhiGpu;
    const self = gpu.states[state];
    return gpu.put(self, self.device.createSampler(descriptor));
});

EM_JS(int, mrhiJsCreateQuerySet, (int state, bool timestamps, uint32_t count), {
    const gpu = Module.mrhiGpu;
    const self = gpu.states[state];
    return gpu.put(self, self.device.createQuerySet({type: timestamps ? 'timestamp' : 'occlusion',
                                                     count}));
});

// Configures a canvas at a size, which becomes its drawing buffer's: its
// swapchain's handle.
EM_JS(int, mrhiJsConfigureCanvas, (int state, int surface, const char* format, uint32_t usage,
                               const char* viewFormats, bool displayP3, bool extended,
                               bool premultiplied, uint32_t width, uint32_t height), {
    const gpu = Module.mrhiGpu;
    const self = gpu.states[state];
    const canvas = gpu.states[surface];
    const views = UTF8ToString(viewFormats);
    canvas.canvas.width = width;
    canvas.canvas.height = height;
    canvas.context.configure({
        device: self.device,
        format: UTF8ToString(format),
        usage: gpu.flags(usage, GPUTextureUsage, gpu.textureUsages),
        viewFormats: views ? views.split(',') : [],
        colorSpace: displayP3 ? 'display-p3' : 'srgb',
        toneMapping: {mode: extended ? 'extended' : 'standard'},
        alphaMode: premultiplied ? 'premultiplied' : 'opaque',
    });
    return gpu.put(self, {canvas, width, height});
});

EM_JS(void, mrhiJsUnconfigureCanvas, (int state, int swapchain), {
    const gpu = Module.mrhiGpu;
    gpu.take(gpu.states[state], swapchain).canvas.context.unconfigure();
});

// The canvas's current texture, or 0 when the page has resized its
// drawing buffer since it was configured.
EM_JS(int, mrhiJsAcquireCanvas, (int state, int swapchain), {
    const gpu = Module.mrhiGpu;
    const self = gpu.states[state];
    const chain = self.objects[swapchain];
    const canvas = chain.canvas.canvas;
    if (canvas.width !== chain.width || canvas.height !== chain.height) {
        return 0;
    }
    return gpu.put(self, chain.canvas.context.getCurrentTexture());
});

// Frees a handle whose object the device does not destroy: a swapchain
// reconfigured, or a canvas's texture, which the browser presents.
EM_JS(void, mrhiJsForget, (int state, int handle), {
    const gpu = Module.mrhiGpu;
    gpu.take(gpu.states[state], handle);
});

// Retires an object once the frame numbered serial has finished.
EM_JS(void, mrhiJsRetire, (int state, int handle, double serial), {
    Module.mrhiGpu.states[state].retiring.push([serial, handle]);
});

// Destroys the retiring objects whose frames have finished.
EM_JS(void, mrhiJsSweep, (int state, double finished), {
    const gpu = Module.mrhiGpu;
    const self = gpu.states[state];
    self.retiring = self.retiring.filter(([serial, handle]) => {
        if (serial > finished) {
            return true;
        }
        const object = gpu.take(self, handle);
        if (object.destroy) {
            object.destroy();
        }
        return false;
    });
});
// clang-format on

EM_JS_DEPS(mrhi_webgpu_device, "$UTF8ToString,$stringToUTF8");

// Retires an object once the next frame submitted has finished, since
// the frame recording may name it.
static void Retire(WebGpuDevice* device, uint64_t handle)
{
    mrhiJsRetire(device->state, (int)handle, (double)(device->submitted + 1));
}

static void Destroy(void* self)
{
    WebGpuDevice* device = self;
    mrhiJsCloseDevice(device->state);
    mrhiAllocator allocator = device->allocator;
    mrhiRelease(&allocator, device, device->bytes, alignof(WebGpuDevice));
}

static const char* AddressOf(mrhiAddressMode mode)
{
    static const char* const s_modes[] = {"clamp-to-edge", "repeat", "mirror-repeat"};
    MRHI_ASSERT(mode <= mrhi_addressMirrorRepeat);
    return s_modes[mode];
}

static const char* FilterOf(mrhiFilter filter)
{
    return filter == mrhi_filterLinear ? "linear" : "nearest";
}

// A comparison's name, NULL for none.
static const char* CompareOf(mrhiCompareFunction compare)
{
    static const char* const s_compares[] = {
        nullptr,   "never",     "less",          "equal",  "less-equal",
        "greater", "not-equal", "greater-equal", "always",
    };
    MRHI_ASSERT(compare <= mrhi_compareAlways);
    return s_compares[compare];
}

static mrhiResult CreateSampler(void* self, const mrhiSamplerDef* def, uint64_t* handleOut)
{
    const WebGpuDevice* device = self;
    *handleOut = (uint64_t)mrhiJsCreateSampler(
        device->state, AddressOf(def->addressU), AddressOf(def->addressV), AddressOf(def->addressW),
        FilterOf(def->magFilter), FilterOf(def->minFilter), FilterOf(def->mipFilter), def->lodMin,
        def->lodMax, CompareOf(def->compare), def->maxAnisotropy);
    return mrhi_success;
}

static void DestroyObject(void* self, uint64_t handle)
{
    Retire(self, handle);
}

static mrhiResult CreateBuffer(void* self, const mrhiBufferDef* def, uint64_t* handleOut)
{
    const WebGpuDevice* device = self;
    *handleOut = (uint64_t)mrhiJsCreateBuffer(device->state, (double)def->size, def->usage);
    return mrhi_success;
}

static mrhiResult CreateTexture(void* self, const mrhiTextureDef* def, uint64_t* handleOut)
{
    const WebGpuDevice* device = self;
    char viewFormats[MRHI_WEBGPU_VIEW_FORMAT_BYTES];
    mrhiWebGpuViewFormats(def, viewFormats);
    *handleOut = (uint64_t)mrhiJsCreateTexture(
        device->state, def->kind == mrhi_texture3d, mrhiWebGpuFormat(def->format), def->width,
        def->height, def->depthOrLayers, def->mipLevels, def->sampleCount, def->usage, viewFormats);
    return mrhi_success;
}

static mrhiResult CreateView(void* self, uint64_t texture, const mrhiViewDef* def,
                             uint64_t* handleOut)
{
    const WebGpuDevice* device = self;
    static const char* const s_dimensions[] = {"2d", "2d-array", "cube", "cube-array", "3d"};
    static const char* const s_aspects[] = {"all", "depth-only", "stencil-only"};
    MRHI_ASSERT(def->kind <= mrhi_texture3d && def->aspect <= mrhi_aspectStencilOnly);
    *handleOut = (uint64_t)mrhiJsCreateView(
        device->state, (int)texture, mrhiWebGpuFormat(def->format), s_dimensions[def->kind],
        s_aspects[def->aspect], def->baseMip, def->mipCount, def->baseLayer, def->layerCount);
    return mrhi_success;
}

static mrhiResult CreateQuerySet(void* self, const mrhiQuerySetDef* def, uint64_t* handleOut)
{
    const WebGpuDevice* device = self;
    *handleOut =
        (uint64_t)mrhiJsCreateQuerySet(device->state, def->type == mrhi_queryTimestamp, def->count);
    return mrhi_success;
}

static void LossReport(void* self, mrhiDeviceLossReport* reportOut)
{
    const WebGpuDevice* device = self;
    // Room for the terminating NUL stringToUTF8 writes.
    char message[MRHI_LOSS_MESSAGE_BYTES + 1];
    int length = mrhiJsLossMessage(device->state, message, (int)sizeof(message));
    *reportOut =
        (mrhiDeviceLossReport){.reason = mrhi_lossUnknown, .messageLength = (uint32_t)length};
    memcpy(reportOut->message, message, (size_t)length);
}

// WebGPU timestamps count nanoseconds.
static double TimestampPeriod(void* self)
{
    (void)self;
    return 1.0;
}

// The browser keeps its own pipeline cache: the driver's is empty, so it
// takes an empty one, its own, and declines any other.
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

// A declared texture's texels over its mips; none for one the GPU keeps
// on chip.
static void TextureMemory(const void* self, const mrhiTextureDef* def, uint64_t* bytesOut,
                          uint64_t* alignmentOut)
{
    (void)self;
    *alignmentOut = 256;
    *bytesOut = 0;
    if ((def->usage & mrhi_textureTransient) != 0)
    {
        return;
    }
    mrhiFormatBlock block = mrhiGetFormatBlock(def->format);
    uint32_t texelBytes = mrhiGetFormatCopy(def->format, mrhi_aspectAll).bytes;
    // A depth and stencil format copies by aspect: its texels as four
    // bytes.
    texelBytes = texelBytes > 0 ? texelBytes : 4;
    bool volume = def->kind == mrhi_texture3d;
    for (uint32_t mip = 0; mip < def->mipLevels; ++mip)
    {
        uint64_t width = def->width >> mip > 0 ? def->width >> mip : 1;
        uint64_t height = def->height >> mip > 0 ? def->height >> mip : 1;
        uint64_t depth = volume ? (def->depthOrLayers >> mip > 0 ? def->depthOrLayers >> mip : 1)
                                : def->depthOrLayers;
        uint64_t blocks = ((width + block.width - 1) / block.width) *
                          ((height + block.height - 1) / block.height);
        *bytesOut += blocks * depth * texelBytes * def->sampleCount;
    }
}

static void BufferMemory(const void* self, const mrhiBufferDef* def, uint64_t* bytesOut,
                         uint64_t* alignmentOut)
{
    (void)self;
    *bytesOut = (def->size + 255) & ~(uint64_t)255;
    *alignmentOut = 256;
}

// Reports finished frames in order, destroying the objects that waited
// for them.
static size_t PollFrames(WebGpuDevice* device, mrhiDriverEvent* events, size_t capacity)
{
    uint64_t finished = mrhiWebGpuFinishedFrames(device->state);
    size_t moved = 0;
    while (moved < capacity && device->finished < finished)
    {
        device->finished += 1;
        events[moved++] = (mrhiDriverEvent){
            .tag = device->tags[device->finished % device->tagCount],
            .outcome = mrhi_success,
        };
    }
    if (moved > 0)
    {
        mrhiJsSweep(device->state, (double)device->finished);
    }
    return moved;
}

// Reports settled pipelines, finished frames, then the device's loss
// once. A pipeline destroyed while it waited is not reported.
static size_t Poll(void* self, mrhiDriverEvent* events, size_t capacity)
{
    WebGpuDevice* device = self;
    size_t moved = 0;
    uint64_t handle = 0;
    mrhiResult outcome = mrhi_success;
    while (moved < capacity && mrhiWebGpuTakePipeline(device->state, &handle, &outcome))
    {
        for (uint32_t i = 0; i < device->pendingCount; ++i)
        {
            if (device->pending[i].handle == handle)
            {
                events[moved++] =
                    (mrhiDriverEvent){.tag = device->pending[i].tag, .outcome = outcome};
                device->pending[i] = device->pending[--device->pendingCount];
                break;
            }
        }
    }
    moved += PollFrames(device, events + moved, capacity - moved);
    if (moved < capacity && !device->lossTold && mrhiJsIsLost(device->state))
    {
        device->lossTold = true;
        events[moved++] = (mrhiDriverEvent){.tag = 0, .outcome = mrhi_errorDeviceLost};
    }
    return moved;
}

// A browser never blocks: a frame not yet finished is not waited for.
static bool WaitFrame(void* self, uint64_t tag, uint64_t timeoutNs)
{
    (void)timeoutNs;
    const WebGpuDevice* device = self;
    uint64_t finished = mrhiWebGpuFinishedFrames(device->state);
    for (uint64_t serial = device->finished + 1; serial <= device->submitted; ++serial)
    {
        if (device->tags[serial % device->tagCount] == tag)
        {
            return serial <= finished;
        }
    }
    return true;
}

static mrhiResult CreateShader(void* self, const mrhiShaderDef* def, const mrhiContainer* container,
                               uint64_t* handleOut)
{
    const WebGpuDevice* device = self;
    *handleOut = mrhiWebGpuCreateShader(device->state, def, container);
    return mrhi_success;
}

// Keeps a started pipeline's tag for its answer.
static void Wait(WebGpuDevice* device, uint64_t handle, uint64_t tag)
{
    MRHI_ASSERT(device->pendingCount < device->pendingLimit);
    device->pending[device->pendingCount++] = (Pending){handle, tag};
}

static mrhiResult CreateComputePipeline(void* self, const mrhiDriverComputePipeline* pipeline,
                                        uint64_t tag, uint64_t* handleOut)
{
    WebGpuDevice* device = self;
    *handleOut = mrhiWebGpuStartComputePipeline(device->state, pipeline);
    Wait(device, *handleOut, tag);
    return mrhi_success;
}

static mrhiResult CreateGraphicsPipeline(void* self, const mrhiDriverGraphicsPipeline* pipeline,
                                         uint64_t tag, uint64_t* handleOut)
{
    WebGpuDevice* device = self;
    *handleOut = mrhiWebGpuStartGraphicsPipeline(device->state, pipeline);
    Wait(device, *handleOut, tag);
    return mrhi_success;
}

static void DestroyPipeline(void* self, uint64_t handle)
{
    WebGpuDevice* device = self;
    for (uint32_t i = 0; i < device->pendingCount; ++i)
    {
        if (device->pending[i].handle == handle)
        {
            device->pending[i] = device->pending[--device->pendingCount];
            break;
        }
    }
    Retire(device, handle);
}

static void Never(void* self, uint64_t handle)
{
    (void)self;
    (void)handle;
    MRHI_ASSERT(false);
}

// Configures a canvas, whose context takes the new configuration in
// place of the old swapchain's.
static mrhiResult ConfigureSurface(void* self, uint64_t surface, const mrhiSurfaceConfig* config,
                                   uint64_t oldSwapchain, uint64_t* swapchainOut)
{
    const WebGpuDevice* device = self;
    if (oldSwapchain != 0)
    {
        mrhiJsForget(device->state, (int)oldSwapchain);
    }
    mrhiTextureDef def = mrhiDefaultTextureDef();
    memcpy(def.viewFormats, config->viewFormats, sizeof(def.viewFormats));
    char viewFormats[MRHI_WEBGPU_VIEW_FORMAT_BYTES];
    mrhiWebGpuViewFormats(&def, viewFormats);
    *swapchainOut = (uint64_t)mrhiJsConfigureCanvas(
        device->state, (int)surface, mrhiWebGpuFormat(config->color.format), config->usage,
        viewFormats, config->color.primaries == mrhi_primariesDisplayP3,
        config->color.range == mrhi_rangeExtended, config->alphaMode == mrhi_alphaPremultiplied,
        config->width, config->height);
    return mrhi_success;
}

static void UnconfigureSurface(void* self, uint64_t swapchain)
{
    const WebGpuDevice* device = self;
    mrhiJsUnconfigureCanvas(device->state, (int)swapchain);
}

// The canvas's current texture: out of date once the page resizes it.
static mrhiResult AcquireImage(void* self, uint64_t swapchain, uint64_t* imageOut)
{
    const WebGpuDevice* device = self;
    int image = mrhiJsAcquireCanvas(device->state, (int)swapchain);
    *imageOut = (uint64_t)image;
    return image != 0 ? mrhi_success : mrhi_errorOutOfDate;
}

// The browser presents a canvas's texture itself, so one taken back is
// only forgotten.
static void ReleaseImage(void* self, uint64_t swapchain, uint64_t image)
{
    const WebGpuDevice* device = self;
    (void)swapchain;
    mrhiJsForget(device->state, (int)image);
}

static mrhiResult SubmitFrame(void* self, const mrhiDriverFrame* frame, uint64_t tag)
{
    WebGpuDevice* device = self;
    device->submitted += 1;
    MRHI_ASSERT(device->submitted - device->finished <= device->tagCount);
    device->tags[device->submitted % device->tagCount] = tag;
    mrhiWebGpuSubmitFrame(device->state, frame, device->submitted);
    return mrhi_success;
}

// WebGPU has no heaps: the core never grants their features here.
static mrhiResult CreateHeap(void* self, const mrhiHeapDef* def, uint64_t* handleOut)
{
    (void)self;
    (void)def;
    (void)handleOut;
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
    .destroyShader = DestroyObject,
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

mrhiResult mrhiCreateWebGpuDevice(const mrhiAllocator* allocator, int instanceState, uint32_t slot,
                                  const mrhiDeviceDef* def, mrhiDeviceDriver* deviceOut)
{
    uint32_t pipelines = def->deviceLimits.pipelines;
    mrhiLayout layout = {.size = sizeof(WebGpuDevice)};
    size_t pendingAt = mrhiLayoutAdd(&layout, pipelines, sizeof(Pending), alignof(Pending));
    uint32_t frames = def->limits.framesInFlight;
    size_t tagsAt = mrhiLayoutAdd(&layout, frames, sizeof(uint64_t), alignof(uint64_t));
    WebGpuDevice* device =
        layout.overflow ? nullptr : mrhiAllocate(allocator, layout.size, alignof(WebGpuDevice));
    if (device == nullptr)
    {
        return mrhi_errorCapacity;
    }
    *device = (WebGpuDevice){
        .allocator = *allocator,
        .bytes = layout.size,
        .state = mrhiJsCreateDeviceState(),
        .pending = (Pending*)((unsigned char*)device + pendingAt),
        .pendingLimit = pipelines,
        .tags = (uint64_t*)((unsigned char*)device + tagsAt),
        .tagCount = frames,
    };
    mrhiWebGpuDefineNames();
    for (size_t i = 0; i < mrhiWebGpuFeatureCount; ++i)
    {
        bool wanted = false;
        memcpy(&wanted, (const unsigned char*)&def->features + mrhiWebGpuFeatures[i].offset,
               sizeof(wanted));
        if (wanted)
        {
            mrhiJsWantFeature(device->state, mrhiWebGpuFeatures[i].name);
        }
    }
    // A limit below WebGPU's default is raised to it by the browser.
    for (size_t i = 0; i < mrhiWebGpuLimitCount; ++i)
    {
        mrhiJsWantLimit(device->state, mrhiWebGpuLimits[i].name,
                        mrhiWebGpuLimitValue(&def->limits, &mrhiWebGpuLimits[i]));
    }
    mrhiJsOpenDevice(instanceState, device->state, slot, def->label, (int)def->labelLength,
                     mrhi_errorPlatform);
    *deviceOut = (mrhiDeviceDriver){.vtable = &s_vtable, .self = device};
    return mrhi_success;
}
