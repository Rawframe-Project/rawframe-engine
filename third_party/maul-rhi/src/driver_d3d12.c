// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's instance (mrhi-0003): every DXGI adapter that opens
// a feature level 12_0 device with shader model 6.0, each under its
// LUID, so an adapter found again keeps its id; WARP, DXGI's software
// adapter, is one of them. A search opens a device on each adapter to
// read it, which D3D12 keeps as the adapter's one device while it is
// held. Searches and device openings are answered at the next poll.
// Limits are WebGPU's floor, raised where every device at the floor
// goes further. Feature level 12_0 grants timestamps on the direct
// queue, BC textures, filtering of 32-bit floats, rg11b10ufloat
// targets, dual-source blending, unclipped depth and first instances in
// indirect draws on every device (the format tables of feature level
// 11_0 require the formats' parts); 64-bit integers and wave operations
// are the device's to report. Resource binding tier 3 grants bindless
// heaps of every kind (mrhi-0015): 65536 entries, the portable ceiling,
// and 256 samplers, since the heaps and the frames' rings share D3D12's
// 2048 shader-visible samplers. Surfaces are Win32 windows
// (d3d12_surface.c).

#include "driver_d3d12.h"

#include "allocator.h"
#include "d3d12_device.h"
#include "d3d12_surface.h"
#include "format_caps.h"
#include "invariant.h"

#include <stdalign.h>
#include <string.h>

// The root block's bytes: 32 of the root signature's 64 DWORDs, the
// others left for the constants, the vertex information and the tables.
#define D3D12_ROOT_BLOCK_BYTES 128

// A heap's resource and sampler entries at most.
#define D3D12_HEAP_SIZE         65536
#define D3D12_SAMPLER_HEAP_SIZE 256

typedef struct D3d12Driver
{
    mrhiAllocator allocator;
    size_t bytes;
    mrhiD3d12Api api;
    IDXGIFactory4* factory;
    mrhiDriverEvent* pending;
    uint32_t pendingCount;
    uint32_t pendingLimit;
} D3d12Driver;

// An adapter's handle: its LUID, never zero.
static uint64_t HandleOf(const DXGI_ADAPTER_DESC1* desc, UINT index)
{
    uint64_t luid =
        (uint64_t)(uint32_t)desc->AdapterLuid.HighPart << 32 | desc->AdapterLuid.LowPart;
    return luid != 0 ? luid : (uint64_t)index + 1;
}

// Opens a device on an adapter at the floor: nullptr below it.
static ID3D12Device* OpenDevice(const D3d12Driver* driver, IDXGIAdapter1* adapter)
{
    ID3D12Device* device = nullptr;
    if (FAILED(driver->api.createDevice((IUnknown*)adapter, D3D_FEATURE_LEVEL_12_0,
                                        &IID_ID3D12Device, (void**)&device)))
    {
        return nullptr;
    }
    D3D12_FEATURE_DATA_SHADER_MODEL model = {.HighestShaderModel = D3D_SHADER_MODEL_6_0};
    if (FAILED(ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_SHADER_MODEL, &model,
                                                sizeof(model))) ||
        model.HighestShaderModel < D3D_SHADER_MODEL_6_0)
    {
        ID3D12Device_Release(device);
        return nullptr;
    }
    return device;
}

static mrhiAdapterKind KindOf(const DXGI_ADAPTER_DESC1* desc, ID3D12Device* device)
{
    if ((desc->Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
    {
        return mrhi_adapterSoftware;
    }
    D3D12_FEATURE_DATA_ARCHITECTURE architecture = {0};
    if (FAILED(ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_ARCHITECTURE, &architecture,
                                                sizeof(architecture))))
    {
        return mrhi_adapterUnknown;
    }
    return architecture.UMA ? mrhi_adapterIntegrated : mrhi_adapterDiscrete;
}

// DXGI's name in UTF-8, cut at a character's boundary.
static mrhiAdapterInfo InfoOf(const DXGI_ADAPTER_DESC1* desc, ID3D12Device* device)
{
    mrhiAdapterInfo info = {
        .driver = mrhi_driverD3d12,
        .kind = KindOf(desc, device),
        .vendorId = desc->VendorId,
        .deviceId = desc->DeviceId,
    };
    // Each UTF-16 unit of the name takes at most 3 bytes of UTF-8.
    char name[3 * sizeof(desc->Description) / sizeof(desc->Description[0])];
    int written = WideCharToMultiByte(CP_UTF8, 0, desc->Description, -1, name, (int)sizeof(name),
                                      nullptr, nullptr);
    size_t length = written > 1 ? (size_t)written - 1 : 0;
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

// The optional features every device at the floor grants, which hold
// all that formats' caps depend on.
static const mrhiFeatures s_floorFeatures = {
    .timestampQuery = true,
    .pipelineStatisticsQuery = true,
    .textureCompressionBc = true,
    .float32Filterable = true,
    .rg11b10Renderable = true,
    .dualSourceBlending = true,
    .unclippedDepth = true,
    .indirectFirstInstance = true,
    .multiDrawIndirectCount = true,
};

// The optional features a device grants.
static mrhiFeatures FeaturesOf(ID3D12Device* device)
{
    mrhiFeatures features = s_floorFeatures;
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {0};
    bool heaps = SUCCEEDED(ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS,
                                                            &options, sizeof(options))) &&
                 options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3;
    features.bindlessSampling = heaps;
    features.bindlessHeterogeneous = heaps;
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1 = {0};
    if (SUCCEEDED(ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS1, &options1,
                                                   sizeof(options1))))
    {
        features.shaderInt64 = options1.Int64ShaderOps;
        features.subgroups = options1.WaveOps;
    }
    // Multiview through view instancing, its shaders reading SV_ViewID
    // of shader model 6.1 (mrhi-0020).
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 options3 = {0};
    D3D12_FEATURE_DATA_SHADER_MODEL model = {.HighestShaderModel = D3D_SHADER_MODEL_6_1};
    features.multiview = SUCCEEDED(ID3D12Device_CheckFeatureSupport(
                             device, D3D12_FEATURE_D3D12_OPTIONS3, &options3, sizeof(options3))) &&
                         options3.ViewInstancingTier >= D3D12_VIEW_INSTANCING_TIER_1 &&
                         SUCCEEDED(ID3D12Device_CheckFeatureSupport(
                             device, D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))) &&
                         model.HighestShaderModel >= D3D_SHADER_MODEL_6_1;
    return features;
}

// WebGPU's floor, raised to what every feature level 12_0 device takes,
// with heaps where the features grant them.
static mrhiLimits LimitsOf(const mrhiFeatures* features)
{
    mrhiLimits limits = mrhiDefaultLimits();
    limits.textureDimension2d = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    limits.textureArrayLayers = D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;
    limits.rootBlockBytes = D3D12_ROOT_BLOCK_BYTES;
    limits.framesInFlight = MRHI_D3D12_FRAMES;
    if (features->bindlessSampling)
    {
        limits.heapSize = D3D12_HEAP_SIZE;
        limits.samplerHeapSize = D3D12_SAMPLER_HEAP_SIZE;
    }
    limits.multiviewViews = features->multiview ? D3D12_MAX_VIEW_INSTANCE_COUNT : 1;
    return limits;
}

static mrhiResult RequestAdapters(void* self, uint64_t tag)
{
    D3d12Driver* driver = self;
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
    D3d12Driver* driver = self;
    size_t moved = driver->pendingCount < capacity ? driver->pendingCount : capacity;
    memcpy(events, driver->pending, moved * sizeof(mrhiDriverEvent));
    memmove(driver->pending, driver->pending + moved,
            (driver->pendingCount - moved) * sizeof(mrhiDriverEvent));
    driver->pendingCount -= (uint32_t)moved;
    return moved;
}

// Lists every adapter at the floor, in DXGI's order.
static size_t GetAdapters(const void* self, mrhiDriverAdapter* adapters, size_t capacity)
{
    const D3d12Driver* driver = self;
    size_t found = 0;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; IDXGIFactory4_EnumAdapters1(driver->factory, i, &adapter) == S_OK; ++i)
    {
        DXGI_ADAPTER_DESC1 desc;
        ID3D12Device* device = SUCCEEDED(IDXGIAdapter1_GetDesc1(adapter, &desc))
                                   ? OpenDevice(driver, adapter)
                                   : nullptr;
        if (device != nullptr)
        {
            if (found < capacity)
            {
                mrhiFeatures features = FeaturesOf(device);
                adapters[found] = (mrhiDriverAdapter){
                    .handle = HandleOf(&desc, i),
                    .info = InfoOf(&desc, device),
                    .features = features,
                    .limits = LimitsOf(&features),
                };
            }
            ++found;
            ID3D12Device_Release(device);
        }
        IDXGIAdapter1_Release(adapter);
    }
    return found;
}

// What the floor promises, with the features every device grants.
static void GetFormatCaps(const void* self, uint64_t adapter, mrhiFormat format,
                          mrhiFormatCaps* capsOut)
{
    (void)self;
    (void)adapter;
    *capsOut = mrhiGrantedFormatCaps(format, &s_floorFeatures);
}

static mrhiResult CreateSurface(void* self, const mrhiChain* source, const mrhiSurfaceDef* def,
                                uint64_t* handleOut)
{
    (void)self;
    (void)def;
    return mrhiD3d12CreateSurface(source, handleOut);
}

// A surface holds only its window, which the program keeps.
static void DestroySurface(void* self, uint64_t handle)
{
    (void)self;
    (void)handle;
}

// Every adapter presents to a window, through DXGI's compositor.
static void GetSurfaceCaps(const void* self, uint64_t surface, uint64_t adapter,
                           mrhiSurfaceCaps* capsOut)
{
    const D3d12Driver* driver = self;
    (void)adapter;
    mrhiD3d12SurfaceCaps(driver->factory, surface, capsOut);
}

// The adapter an adapter handle names: nullptr when it is gone.
static IDXGIAdapter1* AdapterOf(const D3d12Driver* driver, uint64_t handle)
{
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; IDXGIFactory4_EnumAdapters1(driver->factory, i, &adapter) == S_OK; ++i)
    {
        DXGI_ADAPTER_DESC1 desc;
        if (SUCCEEDED(IDXGIAdapter1_GetDesc1(adapter, &desc)) && HandleOf(&desc, i) == handle)
        {
            return adapter;
        }
        IDXGIAdapter1_Release(adapter);
    }
    return nullptr;
}

// Opens the device at once and answers the opening at the next poll.
static mrhiResult CreateDevice(void* self, uint64_t adapter, const mrhiDeviceDef* def, uint64_t tag,
                               mrhiDeviceDriver* deviceOut)
{
    D3d12Driver* driver = self;
    if (driver->pendingCount == driver->pendingLimit)
    {
        return mrhi_errorCapacity;
    }
    IDXGIAdapter1* found = AdapterOf(driver, adapter);
    ID3D12Device* device = found != nullptr ? OpenDevice(driver, found) : nullptr;
    if (found != nullptr)
    {
        IDXGIAdapter1_Release(found);
    }
    if (device == nullptr)
    {
        return mrhi_errorPlatform;
    }
    mrhiResult status = mrhiCreateD3d12Device(&def->allocator, &driver->api, driver->factory,
                                              device, def, deviceOut);
    if (status == mrhi_success)
    {
        driver->pending[driver->pendingCount++] =
            (mrhiDriverEvent){.tag = tag, .outcome = mrhi_success};
    }
    return status;
}

static void Destroy(void* self)
{
    D3d12Driver* driver = self;
    IDXGIFactory4_Release(driver->factory);
    mrhiCloseD3d12(&driver->api);
    mrhiAllocator allocator = driver->allocator;
    mrhiRelease(&allocator, driver, driver->bytes, alignof(D3d12Driver));
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

mrhiResult mrhiCreateD3d12Driver(const mrhiAllocator* allocator, uint32_t pendingLimit,
                                 mrhiInstanceDriver* driverOut)
{
    *driverOut = (mrhiInstanceDriver){0};
    mrhiD3d12Api api;
    if (!mrhiOpenD3d12(&api))
    {
        return mrhi_success;
    }
    IDXGIFactory4* factory = nullptr;
    if (FAILED(api.createFactory(0, &IID_IDXGIFactory4, (void**)&factory)))
    {
        mrhiCloseD3d12(&api);
        return mrhi_success;
    }
    mrhiLayout layout = {.size = sizeof(D3d12Driver)};
    size_t pendingAt =
        mrhiLayoutAdd(&layout, pendingLimit, sizeof(mrhiDriverEvent), alignof(mrhiDriverEvent));
    D3d12Driver* driver =
        layout.overflow ? nullptr : mrhiAllocate(allocator, layout.size, alignof(D3d12Driver));
    if (driver == nullptr)
    {
        IDXGIFactory4_Release(factory);
        mrhiCloseD3d12(&api);
        return mrhi_errorCapacity;
    }
    *driver = (D3d12Driver){
        .allocator = *allocator,
        .bytes = layout.size,
        .api = api,
        .factory = factory,
        .pending = (mrhiDriverEvent*)((unsigned char*)driver + pendingAt),
        .pendingLimit = pendingLimit,
    };
    *driverOut = (mrhiInstanceDriver){.vtable = &s_vtable, .self = driver};
    return mrhi_success;
}
