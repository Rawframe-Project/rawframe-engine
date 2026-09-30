// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Metal driver's instance (mrhi-0003): every Metal device the system
// lists (the one default device outside macOS), each an adapter under
// its registry id, so an adapter found again keeps its id. Searches and
// device openings are answered at the next poll. Limits are WebGPU's
// floor, raised where every Metal device of the family goes further.
// Features are what the device reports among those the driver runs:
// timestamps, present timing, multiview, counted multi-draws, pipeline
// statistics and heaps are not granted yet. Every entry
// point that touches Objective-C objects drains its own autorelease
// pool, since the calling thread may have none. A surface is a
// CAMetalLayer the driver retains, which every adapter presents to.

#include "driver_metal.h"

#include "allocator.h"
#include "format_caps.h"
#include "invariant.h"
#include "metal_device.h"
#include "metal_frame.h"
#include "metal_surface.h"

#import <QuartzCore/CAMetalLayer.h>
#import <TargetConditionals.h>
#include <stdalign.h>
#include <string.h>

// The root block's bytes: setBytes takes up to 4 KiB, the container 256.
#define METAL_ROOT_BLOCK_BYTES 256

typedef struct MetalDriver
{
    mrhiAllocator allocator;
    size_t bytes;
    mrhiDriverEvent* pending;
    uint32_t pendingCount;
    uint32_t pendingLimit;
} MetalDriver;

// The system's Metal devices, retained by the array the caller releases.
static NSArray<id<MTLDevice>>* CopyDevices(void)
{
#if TARGET_OS_OSX
    return MTLCopyAllDevices();
#else
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    NSArray<id<MTLDevice>>* devices =
        device == nil ? [[NSArray alloc] init] : [[NSArray alloc] initWithObjects:device, nil];
    [device release];
    return devices;
#endif
}

// A device's adapter handle: its registry id, never zero.
static uint64_t HandleOf(id<MTLDevice> device, NSUInteger index)
{
    uint64_t registry = device.registryID;
    return registry != 0 ? registry : (uint64_t)index + 1;
}

static mrhiAdapterKind KindOf(id<MTLDevice> device)
{
    // The virtual machines of hosted macOS runners show Apple's
    // paravirtual device, which says so only in its name.
    if ([device.name rangeOfString:@"Paravirtual"].location != NSNotFound)
    {
        return mrhi_adapterVirtual;
    }
    return device.hasUnifiedMemory ? mrhi_adapterIntegrated : mrhi_adapterDiscrete;
}

// Metal hides PCI ids; the name is cut at a character's boundary.
static mrhiAdapterInfo InfoOf(id<MTLDevice> device)
{
    mrhiAdapterInfo info = {.driver = mrhi_driverMetal, .kind = KindOf(device)};
    const char* name = device.name.UTF8String;
    size_t length = name != nullptr ? strlen(name) : 0;
    if (length > MRHI_ADAPTER_NAME_BYTES)
    {
        length = MRHI_ADAPTER_NAME_BYTES;
        while (length > 0 && ((unsigned char)name[length] & 0xC0) == 0x80)
        {
            --length;
        }
    }
    if (length > 0)
    {
        memcpy(info.name, name, length);
    }
    info.nameLength = (uint32_t)length;
    return info;
}

static uint32_t Clamp32(uint64_t value)
{
    return value < UINT32_MAX ? (uint32_t)value : UINT32_MAX;
}

// WebGPU's floor, raised where Metal's feature set tables promise more
// for the device's family and where the device reports its own.
static mrhiLimits LimitsOf(id<MTLDevice> device)
{
    mrhiLimits limits = mrhiDefaultLimits();
    bool large =
        [device supportsFamily:MTLGPUFamilyApple3] || [device supportsFamily:MTLGPUFamilyMac2];
    limits.textureDimension2d = large ? 16384 : 8192;
    limits.textureArrayLayers = 2048;
    limits.bufferBytes = device.maxBufferLength;
    limits.storageBindingBytes = device.maxBufferLength;
    limits.workgroupStorageBytes = Clamp32(device.maxThreadgroupMemoryLength);
    limits.rootBlockBytes = METAL_ROOT_BLOCK_BYTES;
    limits.framesInFlight = MRHI_METAL_FRAMES;
    return limits;
}

static mrhiFeatures FeaturesOf(id<MTLDevice> device)
{
    bool apple7 = [device supportsFamily:MTLGPUFamilyApple7];
    bool mac2 = [device supportsFamily:MTLGPUFamilyMac2];
    return (mrhiFeatures){
        .textureCompressionBc = device.supportsBCTextureCompression,
        .textureCompressionEtc2 = [device supportsFamily:MTLGPUFamilyApple2],
        .textureCompressionAstc = [device supportsFamily:MTLGPUFamilyApple2],
        .float32Filterable = device.supports32BitFloatFiltering,
        .rg11b10Renderable = true,
        .dualSourceBlending = true,
        .unclippedDepth = true,
        .shaderF16 = true,
        .subgroups = apple7 || mac2,
        .shaderInt64 = apple7 || mac2,
        .indirectFirstInstance = true,
    };
}

// The device an adapter handle names, from a list of the system's.
static id<MTLDevice> DeviceOf(NSArray<id<MTLDevice>>* devices, uint64_t adapter)
{
    for (NSUInteger i = 0; i < devices.count; ++i)
    {
        if (HandleOf(devices[i], i) == adapter)
        {
            return devices[i];
        }
    }
    return nil;
}

static mrhiResult RequestAdapters(void* self, uint64_t tag)
{
    MetalDriver* driver = self;
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
    MetalDriver* driver = self;
    size_t moved = driver->pendingCount < capacity ? driver->pendingCount : capacity;
    memcpy(events, driver->pending, moved * sizeof(mrhiDriverEvent));
    memmove(driver->pending, driver->pending + moved,
            (driver->pendingCount - moved) * sizeof(mrhiDriverEvent));
    driver->pendingCount -= (uint32_t)moved;
    return moved;
}

// Lists every device; the core leaves out those below the floor.
static size_t GetAdapters(const void* self, mrhiDriverAdapter* adapters, size_t capacity)
{
    (void)self;
    size_t found = 0;
    @autoreleasepool
    {
        NSArray<id<MTLDevice>>* devices = CopyDevices();
        found = devices.count;
        for (NSUInteger i = 0; i < devices.count && i < capacity; ++i)
        {
            id<MTLDevice> device = devices[i];
            adapters[i] = (mrhiDriverAdapter){
                .handle = HandleOf(device, i),
                .info = InfoOf(device),
                .features = FeaturesOf(device),
                .limits = LimitsOf(device),
            };
        }
        [devices release];
    }
    return found;
}

// What the floor promises, and what the adapter's features add.
static void GetFormatCaps(const void* self, uint64_t adapter, mrhiFormat format,
                          mrhiFormatCaps* capsOut)
{
    (void)self;
    mrhiFeatures features = {0};
    @autoreleasepool
    {
        NSArray<id<MTLDevice>>* devices = CopyDevices();
        id<MTLDevice> device = DeviceOf(devices, adapter);
        features = device != nil ? FeaturesOf(device) : features;
        [devices release];
    }
    *capsOut = mrhiGrantedFormatCaps(format, &features);
}

// A CAMetalLayer is the only source Metal presents to; the pointer must
// be one.
static mrhiResult CreateSurface(void* self, const mrhiChain* source, const mrhiSurfaceDef* def,
                                uint64_t* handleOut)
{
    (void)self;
    (void)def;
    *handleOut = 0;
    if (source->type != mrhi_structSurfaceSourceMetalLayer)
    {
        return mrhi_errorUnsupported;
    }
    id layer = (id)((const mrhiSurfaceSourceMetalLayer*)source)->layer;
    bool metal = false;
    @autoreleasepool
    {
        metal = layer != nil && [layer isKindOfClass:[CAMetalLayer class]];
    }
    if (!metal)
    {
        return mrhi_errorUnsupported;
    }
    *handleOut = (uint64_t)(uintptr_t)(void*)[layer retain];
    return mrhi_success;
}

static void DestroySurface(void* self, uint64_t handle)
{
    (void)self;
    [(id)(void*)(uintptr_t)handle release];
}

// Every Metal device presents to a layer, in the same colors; its
// images render, are sampled and copied, since configurations that ask
// for more than rendering turn framebufferOnly off.
static void GetSurfaceCaps(const void* self, uint64_t surface, uint64_t adapter,
                           mrhiSurfaceCaps* capsOut)
{
    (void)self;
    (void)surface;
    (void)adapter;
    *capsOut = (mrhiSurfaceCaps){
        .presentable = true,
        .presentModes = mrhi_presentFifo,
        .alphaModes = mrhi_alphaOpaque | mrhi_alphaPremultiplied,
        .usages = mrhi_textureRenderTarget | mrhi_textureSampled | mrhi_textureCopySource |
                  mrhi_textureCopyDestination,
    };
#if TARGET_OS_OSX
    capsOut->presentModes |= mrhi_presentImmediate;
#endif
    capsOut->colorCount = mrhiMetalSurfaceColors(capsOut->colors);
}

// Opens the device at once and answers the opening at the next poll.
static mrhiResult CreateDevice(void* self, uint64_t adapter, const mrhiDeviceDef* def, uint64_t tag,
                               mrhiDeviceDriver* deviceOut)
{
    MetalDriver* driver = self;
    if (driver->pendingCount == driver->pendingLimit)
    {
        return mrhi_errorCapacity;
    }
    mrhiResult status = mrhi_errorPlatform;
    @autoreleasepool
    {
        NSArray<id<MTLDevice>>* devices = CopyDevices();
        id<MTLDevice> device = DeviceOf(devices, adapter);
        if (device != nil)
        {
            status = mrhiCreateMetalDevice(&driver->allocator, device, def, deviceOut);
        }
        [devices release];
    }
    if (status == mrhi_success)
    {
        driver->pending[driver->pendingCount++] =
            (mrhiDriverEvent){.tag = tag, .outcome = mrhi_success};
    }
    return status;
}

static void Destroy(void* self)
{
    MetalDriver* driver = self;
    mrhiAllocator allocator = driver->allocator;
    mrhiRelease(&allocator, driver, driver->bytes, alignof(MetalDriver));
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

mrhiResult mrhiCreateMetalDriver(const mrhiAllocator* allocator, uint32_t pendingLimit,
                                 mrhiInstanceDriver* driverOut)
{
    *driverOut = (mrhiInstanceDriver){0};
    mrhiLayout layout = {.size = sizeof(MetalDriver)};
    size_t pendingAt =
        mrhiLayoutAdd(&layout, pendingLimit, sizeof(mrhiDriverEvent), alignof(mrhiDriverEvent));
    MetalDriver* driver =
        layout.overflow ? nullptr : mrhiAllocate(allocator, layout.size, alignof(MetalDriver));
    if (driver == nullptr)
    {
        return mrhi_errorCapacity;
    }
    *driver = (MetalDriver){
        .allocator = *allocator,
        .bytes = layout.size,
        .pending = (mrhiDriverEvent*)((unsigned char*)driver + pendingAt),
        .pendingLimit = pendingLimit,
    };
    *driverOut = (mrhiInstanceDriver){.vtable = &s_vtable, .self = driver};
    return mrhi_success;
}
