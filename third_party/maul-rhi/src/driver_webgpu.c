// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The WebGPU driver's instance (mrhi-0003): the browser's navigator.gpu,
// reached through EM_JS functions. Each instance has a state on the
// JavaScript side, where requests wait as promises and settle into a
// queue of request slots; a poll drains it. A search asks for the
// browser's adapter and lists it under one handle, so an adapter found
// again keeps its id; devices ask for a fresh adapter, since a WebGPU
// adapter makes one device. Features and limits follow the contract's
// WebGPU rows; an adapter without core features falls below the floor.
// A surface is a canvas named by selector, with its WebGPU context.

#include "driver_webgpu.h"

#include "allocator.h"
#include "format_caps.h"
#include "invariant.h"
#include "webgpu_device.h"
#include "webgpu_names.h"

#include "maul-rhi/surface.h"

#include <emscripten/em_js.h>
#include <stdalign.h>
#include <string.h>

// The handle of the browser's adapter.
#define ADAPTER_HANDLE 1

// The frames a device lets run at once, each a settled promise.
#define WEBGPU_FRAMES_IN_FLIGHT 3

typedef struct WebGpuDriver
{
    mrhiAllocator allocator;
    size_t bytes;
    // The instance's state on the JavaScript side.
    int state;
    // Each request slot's tag, 0 for a free slot, and whether it is a
    // search or a device's opening.
    uint64_t* tags;
    bool* searches;
    uint32_t slotCount;
    // What the last finished search found.
    bool found;
    mrhiDriverAdapter adapter;
} WebGpuDriver;

// clang-format off
// The driver's JavaScript side, one per module: the instances' and
// devices' states, every device's uncaptured errors, which the web test
// runner reads, a device state's objects under handles, and the makers
// of buffers and textures, whose usage flags are the browser's for the
// contract's bits, by name.
EM_JS(int, JsCreateState, (void), {
    const gpu = Module.mrhiGpu || (Module.mrhiGpu = {
        states: [null],
        errors: [],
        add(state) {
            this.states.push(state);
            return this.states.length - 1;
        },
        put(state, object) {
            const handle = state.free.length > 0 ? state.free.pop() : state.objects.length;
            state.objects[handle] = object;
            return handle;
        },
        take(state, handle) {
            const object = state.objects[handle];
            state.objects[handle] = null;
            state.free.push(handle);
            return object;
        },
        flags(bits, table, names) {
            let flags = 0;
            names.forEach((name, bit) => {
                flags |= (bits >> bit & 1) ? table[name] : 0;
            });
            return flags;
        },
        bufferUsages: ['VERTEX', 'INDEX', 'UNIFORM', 'STORAGE', 'INDIRECT', 'COPY_SRC',
                       'COPY_DST', 'QUERY_RESOLVE'],
        textureUsages: ['TEXTURE_BINDING', 'STORAGE_BINDING', 'RENDER_ATTACHMENT',
                        'TRANSIENT_ATTACHMENT', 'COPY_SRC', 'COPY_DST'],
        buffer(device, size, usage) {
            return device.createBuffer({size,
                                        usage: this.flags(usage, GPUBufferUsage,
                                                          this.bufferUsages)});
        },
        texture(device, volume, format, width, height, depth, mips, samples, usage, views) {
            return device.createTexture({
                size: [width, height, depth],
                dimension: volume ? '3d' : '2d',
                format,
                mipLevelCount: mips,
                sampleCount: samples,
                usage: this.flags(usage, GPUTextureUsage, this.textureUsages),
                viewFormats: views ? views.split(',') : [],
            });
        },
    });
    return gpu.add({settled: [], adapter: null});
});

EM_JS(void, JsDestroyState, (int state), {
    Module.mrhiGpu.states[state] = null;
});

// Asks for the browser's adapter; the slot settles either way.
EM_JS(void, JsRequestAdapter, (int state, uint32_t slot), {
    const self = Module.mrhiGpu.states[state];
    const settle = adapter => {
        if (Module.mrhiGpu.states[state] === self) {
            self.adapter = adapter || null;
            self.settled.push([slot, 0]);
        }
    };
    if (typeof navigator === 'undefined' || !navigator.gpu) {
        settle(null);
        return;
    }
    navigator.gpu.requestAdapter().then(settle, () => settle(null));
});

// The next settled slot and its outcome, or -1.
EM_JS(int, JsTakeSettled, (int state, int32_t* outcomeOut), {
    const next = Module.mrhiGpu.states[state].settled.shift();
    if (!next) {
        return -1;
    }
    HEAP32[outcomeOut >> 2] = next[1];
    return next[0];
});

EM_JS(bool, JsHasAdapter, (int state), {
    return Module.mrhiGpu.states[state].adapter !== null;
});

EM_JS(bool, JsAdapterHas, (int state, const char* feature), {
    return Module.mrhiGpu.states[state].adapter.features.has(UTF8ToString(feature));
});

// A limit, 0 for one the browser does not report.
EM_JS(double, JsAdapterLimit, (int state, const char* limit), {
    const value = Module.mrhiGpu.states[state].adapter.limits[UTF8ToString(limit)];
    return typeof value === 'number' ? value : 0;
});

EM_JS(bool, JsAdapterFallback, (int state), {
    const info = Module.mrhiGpu.states[state].adapter.info;
    return !!(info && info.isFallbackAdapter);
});

// Writes the adapter's name, its description or its vendor and
// architecture, and returns its bytes.
EM_JS(int, JsAdapterName, (int state, char* out, int capacity), {
    const info = Module.mrhiGpu.states[state].adapter.info || {};
    const parts = [info.vendor, info.architecture].filter(part => part);
    const name = info.description || parts.join(' ') || 'WebGPU';
    return stringToUTF8(name, out, capacity);
});

// A canvas the selector names and its WebGPU context, kept as a state of
// its own so that any device reaches it by handle: the handle, or 0 for a
// selector naming no canvas, or a canvas without a WebGPU context.
EM_JS(int, JsCreateCanvas, (const char* selector, int selectorLength), {
    let canvas = null;
    try {
        canvas = typeof document === 'undefined' ? null :
            document.querySelector(UTF8ToString(selector, selectorLength));
    } catch (error) {
        canvas = null;
    }
    const context = canvas && canvas.getContext ? canvas.getContext('webgpu') : null;
    return context ? Module.mrhiGpu.add({canvas, context}) : 0;
});

EM_JS(void, JsDestroyCanvas, (int surface), {
    Module.mrhiGpu.states[surface] = null;
});

EM_JS(bool, JsPrefersBgra, (void), {
    return navigator.gpu.getPreferredCanvasFormat() === 'bgra8unorm';
});
// clang-format on

EM_JS_DEPS(mrhi_webgpu_driver, "$UTF8ToString,$stringToUTF8");

// Reads the adapter the last search found, when there is one with
// WebGPU's core features: false otherwise.
static bool Describe(const WebGpuDriver* driver, mrhiDriverAdapter* adapterOut)
{
    if (!JsHasAdapter(driver->state) || !JsAdapterHas(driver->state, "core-features-and-limits"))
    {
        return false;
    }
    *adapterOut = (mrhiDriverAdapter){.handle = ADAPTER_HANDLE};
    mrhiAdapterInfo* info = &adapterOut->info;
    info->driver = mrhi_driverWebGpu;
    info->kind = JsAdapterFallback(driver->state) ? mrhi_adapterSoftware : mrhi_adapterUnknown;
    // Room for the terminating NUL stringToUTF8 writes.
    char name[MRHI_ADAPTER_NAME_BYTES + 1];
    int length = JsAdapterName(driver->state, name, (int)sizeof(name));
    memcpy(info->name, name, (size_t)length);
    info->nameLength = (uint32_t)length;
    for (size_t i = 0; i < mrhiWebGpuFeatureCount; ++i)
    {
        bool has = JsAdapterHas(driver->state, mrhiWebGpuFeatures[i].name);
        memcpy((unsigned char*)&adapterOut->features + mrhiWebGpuFeatures[i].offset, &has,
               sizeof(has));
    }
    for (size_t i = 0; i < mrhiWebGpuLimitCount; ++i)
    {
        mrhiSetWebGpuLimit(&adapterOut->limits, &mrhiWebGpuLimits[i],
                           JsAdapterLimit(driver->state, mrhiWebGpuLimits[i].name));
    }
    adapterOut->limits.framesInFlight = WEBGPU_FRAMES_IN_FLIGHT;
    return true;
}

// Takes a free request slot for a tag: its index, or slotCount when all
// are waiting.
static uint32_t TakeSlot(WebGpuDriver* driver, uint64_t tag, bool search)
{
    MRHI_ASSERT(tag != 0);
    for (uint32_t i = 0; i < driver->slotCount; ++i)
    {
        if (driver->tags[i] == 0)
        {
            driver->tags[i] = tag;
            driver->searches[i] = search;
            return i;
        }
    }
    return driver->slotCount;
}

static mrhiResult RequestAdapters(void* self, uint64_t tag)
{
    WebGpuDriver* driver = self;
    uint32_t slot = TakeSlot(driver, tag, true);
    if (slot == driver->slotCount)
    {
        return mrhi_errorCapacity;
    }
    JsRequestAdapter(driver->state, slot);
    return mrhi_success;
}

static size_t Poll(void* self, mrhiDriverEvent* events, size_t capacity)
{
    WebGpuDriver* driver = self;
    size_t moved = 0;
    int32_t outcome = 0;
    int slot = moved < capacity ? JsTakeSettled(driver->state, &outcome) : -1;
    while (slot >= 0)
    {
        MRHI_ASSERT((uint32_t)slot < driver->slotCount && driver->tags[slot] != 0);
        // A search reads what it found as it settles.
        if (driver->searches[slot])
        {
            driver->found = Describe(driver, &driver->adapter);
        }
        events[moved++] = (mrhiDriverEvent){.tag = driver->tags[slot], .outcome = outcome};
        driver->tags[slot] = 0;
        slot = moved < capacity ? JsTakeSettled(driver->state, &outcome) : -1;
    }
    return moved;
}

static size_t GetAdapters(const void* self, mrhiDriverAdapter* adapters, size_t capacity)
{
    const WebGpuDriver* driver = self;
    if (driver->found && capacity > 0)
    {
        adapters[0] = driver->adapter;
    }
    return driver->found ? 1 : 0;
}

static void GetFormatCaps(const void* self, uint64_t adapter, mrhiFormat format,
                          mrhiFormatCaps* capsOut)
{
    const WebGpuDriver* driver = self;
    MRHI_ASSERT(adapter == ADAPTER_HANDLE && driver->found);
    *capsOut = mrhiGrantedFormatCaps(format, &driver->adapter.features);
}

// A canvas is the only source a browser has; one the selector does not
// name, or without a WebGPU context, cannot be used.
static mrhiResult CreateSurface(void* self, const mrhiChain* source, const mrhiSurfaceDef* def,
                                uint64_t* handleOut)
{
    (void)self;
    (void)def;
    if (source->type != mrhi_structSurfaceSourceCanvas)
    {
        return mrhi_errorUnsupported;
    }
    const mrhiSurfaceSourceCanvas* canvas = (const mrhiSurfaceSourceCanvas*)source;
    int handle = JsCreateCanvas(canvas->selector, (int)canvas->selectorLength);
    *handleOut = (uint64_t)handle;
    return handle != 0 ? mrhi_success : mrhi_errorUnsupported;
}

static void DestroySurface(void* self, uint64_t handle)
{
    (void)self;
    JsDestroyCanvas((int)handle);
}

// What a canvas shows, the browser's preferred format first: 8-bit
// formats with the sRGB curve, and half floats in linear values, each in
// Rec. 709 and Display P3, the floats also past 1.0 through extended
// tone mapping. Browsers present in order only.
static void GetSurfaceCaps(const void* self, uint64_t surface, uint64_t adapter,
                           mrhiSurfaceCaps* capsOut)
{
    const WebGpuDriver* driver = self;
    (void)surface;
    MRHI_ASSERT(adapter == ADAPTER_HANDLE && driver->found);
    bool bgra = JsPrefersBgra();
    const mrhiFormat formats[2] = {bgra ? mrhi_formatBgra8Unorm : mrhi_formatRgba8Unorm,
                                   bgra ? mrhi_formatRgba8Unorm : mrhi_formatBgra8Unorm};
    const mrhiColorPrimaries primaries[2] = {mrhi_primariesBt709, mrhi_primariesDisplayP3};
    *capsOut = (mrhiSurfaceCaps){
        .presentable = true,
        .presentModes = mrhi_presentFifo,
        .alphaModes = mrhi_alphaOpaque | mrhi_alphaPremultiplied,
        .usages = mrhi_textureRenderTarget | mrhi_textureSampled | mrhi_textureStorage |
                  mrhi_textureCopySource | mrhi_textureCopyDestination,
    };
    for (uint32_t p = 0; p < 2; ++p)
    {
        for (uint32_t f = 0; f < 2; ++f)
        {
            capsOut->colors[capsOut->colorCount++] =
                (mrhiSurfaceColor){formats[f], primaries[p], mrhi_transferSrgb, mrhi_rangeStandard};
        }
    }
    for (uint32_t p = 0; p < 2; ++p)
    {
        const mrhiColorRange ranges[2] = {mrhi_rangeStandard, mrhi_rangeExtended};
        for (uint32_t r = 0; r < 2; ++r)
        {
            capsOut->colors[capsOut->colorCount++] = (mrhiSurfaceColor){
                mrhi_formatRgba16Float, primaries[p], mrhi_transferLinear, ranges[r]};
        }
    }
}

// Makes the device at once and answers its opening when the browser has.
static mrhiResult CreateDevice(void* self, uint64_t adapter, const mrhiDeviceDef* def, uint64_t tag,
                               mrhiDeviceDriver* deviceOut)
{
    WebGpuDriver* driver = self;
    MRHI_ASSERT(adapter == ADAPTER_HANDLE);
    uint32_t slot = TakeSlot(driver, tag, false);
    if (slot == driver->slotCount)
    {
        return mrhi_errorCapacity;
    }
    mrhiResult status =
        mrhiCreateWebGpuDevice(&driver->allocator, driver->state, slot, def, deviceOut);
    if (status != mrhi_success)
    {
        driver->tags[slot] = 0;
    }
    return status;
}

static void Destroy(void* self)
{
    WebGpuDriver* driver = self;
    JsDestroyState(driver->state);
    mrhiAllocator allocator = driver->allocator;
    mrhiRelease(&allocator, driver, driver->bytes, alignof(WebGpuDriver));
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

mrhiResult mrhiCreateWebGpuDriver(const mrhiAllocator* allocator, uint32_t pendingLimit,
                                  mrhiInstanceDriver* driverOut)
{
    *driverOut = (mrhiInstanceDriver){0};
    mrhiLayout layout = {.size = sizeof(WebGpuDriver)};
    size_t tagsAt = mrhiLayoutAdd(&layout, pendingLimit, sizeof(uint64_t), alignof(uint64_t));
    size_t searchesAt = mrhiLayoutAdd(&layout, pendingLimit, sizeof(bool), alignof(bool));
    WebGpuDriver* driver =
        layout.overflow ? nullptr : mrhiAllocate(allocator, layout.size, alignof(WebGpuDriver));
    if (driver == nullptr)
    {
        return mrhi_errorCapacity;
    }
    unsigned char* block = (unsigned char*)driver;
    *driver = (WebGpuDriver){
        .allocator = *allocator,
        .bytes = layout.size,
        .state = JsCreateState(),
        .tags = (uint64_t*)(block + tagsAt),
        .searches = (bool*)(block + searchesAt),
        .slotCount = pendingLimit,
    };
    memset(driver->tags, 0, (size_t)pendingLimit * sizeof(uint64_t));
    *driverOut = (mrhiInstanceDriver){.vtable = &s_vtable, .self = driver};
    return mrhi_success;
}
