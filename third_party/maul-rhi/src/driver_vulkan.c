// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Vulkan driver's instance (mrhi-0003): a Vulkan 1.3 instance on the
// loader opened at run time, with the surface extensions it offers;
// adapter searches answered at the next poll from the physical devices
// then present; and surfaces (vulkan_surface.c).

#include "driver_vulkan.h"

#include "allocator.h"
#include "invariant.h"
#include "vulkan_adapter.h"
#include "vulkan_device.h"
#include "vulkan_recipe.h"
#include "vulkan_surface.h"

#include "maul-rhi/vulkan.h"

#include <stdalign.h>
#include <string.h>

typedef struct VulkanDriver
{
    mrhiAllocator allocator;
    size_t bytes;
    mrhiVulkan vulkan;
    VkInstance instance;
    // The surface extensions the instance enabled, a bit each in
    // mrhiVulkanSurfaceExtensions's order.
    uint32_t surfaceExtensions;
    mrhiDriverEvent* pending;
    uint32_t pendingCount;
    uint32_t pendingLimit;
    // Room for the physical devices one search reads.
    VkPhysicalDevice* devices;
    uint32_t deviceLimit;
    // An instance made elsewhere, never destroyed here (mrhi-0018).
    bool adopted;
    // The last device description, its adapter and features, held until
    // the next or the end.
    mrhiVulkanRecipe recipe;
    uint64_t describedAdapter;
    mrhiFeatures describedFeatures;
    bool described;
} VulkanDriver;

static mrhiResult RequestAdapters(void* self, uint64_t tag)
{
    VulkanDriver* driver = self;
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
    VulkanDriver* driver = self;
    size_t moved = driver->pendingCount < capacity ? driver->pendingCount : capacity;
    memcpy(events, driver->pending, moved * sizeof(mrhiDriverEvent));
    memmove(driver->pending, driver->pending + moved,
            (driver->pendingCount - moved) * sizeof(mrhiDriverEvent));
    driver->pendingCount -= (uint32_t)moved;
    return moved;
}

// Lists the physical devices that meet the floor. A host with more
// physical devices than the room counts the ones not read, so that the
// core answers capacity.
static size_t GetAdapters(const void* self, mrhiDriverAdapter* adapters, size_t capacity)
{
    const VulkanDriver* driver = self;
    uint32_t present = 0;
    if (driver->vulkan.vkEnumeratePhysicalDevices(driver->instance, &present, nullptr) !=
        VK_SUCCESS)
    {
        return 0;
    }
    uint32_t read = driver->deviceLimit;
    VkResult status =
        driver->vulkan.vkEnumeratePhysicalDevices(driver->instance, &read, driver->devices);
    if (status != VK_SUCCESS && status != VK_INCOMPLETE)
    {
        return 0;
    }
    size_t found = 0;
    for (uint32_t i = 0; i < read; ++i)
    {
        mrhiDriverAdapter adapter;
        if (mrhiDescribeVulkanAdapter(&driver->vulkan, &driver->allocator, driver->devices[i],
                                      &adapter))
        {
            if (found < capacity)
            {
                adapters[found] = adapter;
            }
            ++found;
        }
    }
    return found + (present > read ? present - read : 0);
}

// The physical device an adapter handle holds.
static VkPhysicalDevice DeviceOf(uint64_t adapter)
{
    static_assert(sizeof(VkPhysicalDevice) == sizeof(uintptr_t), "a handle holds a device");
    uintptr_t bits = (uintptr_t)adapter;
    VkPhysicalDevice device;
    memcpy((void*)&device, &bits, sizeof(bits));
    return device;
}

static void GetFormatCaps(const void* self, uint64_t adapter, mrhiFormat format,
                          mrhiFormatCaps* capsOut)
{
    const VulkanDriver* driver = self;
    mrhiGetVulkanFormatCaps(&driver->vulkan, DeviceOf(adapter), format, capsOut);
}

// A surface's handle is the VkSurfaceKHR, never zero.
static VkSurfaceKHR SurfaceOf(uint64_t handle)
{
    static_assert(sizeof(VkSurfaceKHR) == sizeof(uint64_t), "a handle holds a surface");
    VkSurfaceKHR surface;
    memcpy((void*)&surface, &handle, sizeof(handle));
    return surface;
}

static mrhiResult CreateSurface(void* self, const mrhiChain* source, const mrhiSurfaceDef* def,
                                uint64_t* handleOut)
{
    (void)def;
    VulkanDriver* driver = self;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    mrhiResult status = mrhiVulkanCreateSurface(&driver->vulkan, driver->instance,
                                                driver->surfaceExtensions, source, &surface);
    if (status == mrhi_success)
    {
        memcpy(handleOut, (const void*)&surface, sizeof(*handleOut));
    }
    return status;
}

static void DestroySurface(void* self, uint64_t handle)
{
    VulkanDriver* driver = self;
    driver->vulkan.vkDestroySurfaceKHR(driver->instance, SurfaceOf(handle), nullptr);
}

static void GetSurfaceCaps(const void* self, uint64_t surface, uint64_t adapter,
                           mrhiSurfaceCaps* capsOut)
{
    const VulkanDriver* driver = self;
    const char* const wanted[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                                  VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME};
    VkPhysicalDevice device = DeviceOf(adapter);
    uint32_t offered = mrhiVulkanExtensions(&driver->vulkan, &driver->allocator, device, wanted, 2);
    mrhiVulkanSurfaceCaps(&driver->vulkan, device, (offered & 1u) != 0, offered == 3u,
                          SurfaceOf(surface), capsOut);
}

// Whether a def that adopts a device asks for the device the last
// description made: the same adapter, features and own extensions.
static bool IsDescribed(const VulkanDriver* driver, uint64_t adapter, const mrhiDeviceDef* def)
{
    bool adopts = false;
    for (const mrhiChain* node = def->next; node != nullptr; node = node->next)
    {
        adopts = adopts || node->type == mrhi_structDeviceVulkanAdopt;
    }
    const char* names = nullptr;
    size_t bytes = 0;
    bool listed = mrhiVulkanDefExtensions(def, &names, &bytes);
    return !adopts ||
           (driver->described && listed && adapter == driver->describedAdapter &&
            memcmp(&def->features, &driver->describedFeatures, sizeof(mrhiFeatures)) == 0 &&
            bytes == driver->recipe.ownBytes &&
            (bytes == 0 || memcmp(names, driver->recipe.ownNames, bytes) == 0));
}

// Opens the device at once and answers the opening at the next poll.
static mrhiResult CreateDevice(void* self, uint64_t adapter, const mrhiDeviceDef* def, uint64_t tag,
                               mrhiDeviceDriver* deviceOut)
{
    VulkanDriver* driver = self;
    if (driver->pendingCount == driver->pendingLimit)
    {
        return mrhi_errorCapacity;
    }
    if (!IsDescribed(driver, adapter, def))
    {
        return mrhi_errorInvalid;
    }
    if (!mrhiVulkanDefFeaturesKnown(def))
    {
        return mrhi_errorUnsupported;
    }
    mrhiResult status =
        mrhiCreateVulkanDevice(&def->allocator, &driver->vulkan, DeviceOf(adapter), def, deviceOut);
    if (status == mrhi_success)
    {
        driver->pending[driver->pendingCount++] =
            (mrhiDriverEvent){.tag = tag, .outcome = mrhi_success};
    }
    return status;
}

static void Destroy(void* self)
{
    VulkanDriver* driver = self;
    mrhiVulkanEndRecipe(&driver->allocator, &driver->recipe);
    if (!driver->adopted)
    {
        driver->vulkan.vkDestroyInstance(driver->instance, nullptr);
    }
    mrhiCloseVulkan(&driver->vulkan);
    mrhiAllocator allocator = driver->allocator;
    mrhiRelease(&allocator, driver, driver->bytes, alignof(VulkanDriver));
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

// Makes a Vulkan 1.3 instance with its functions read and the surface
// extensions and VK_EXT_debug_utils the loader offers enabled (the
// surface extensions' bits in enabledOut):
// VK_NULL_HANDLE where the loader is older or refuses. Vulkan's own
// allocations stay the platform's: its callbacks free without a size,
// which the program's allocator needs.
static VkInstance CreateInstance(mrhiVulkan* vulkan, const mrhiAllocator* allocator,
                                 const mrhiInstanceVulkanExtensions* extra, uint32_t* enabledOut)
{
    uint32_t version = 0;
    if (vulkan->vkEnumerateInstanceVersion(&version) != VK_SUCCESS || version < VK_API_VERSION_1_3)
    {
        return VK_NULL_HANDLE;
    }
    const VkApplicationInfo application = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pEngineName = "Maul RHI",
        .engineVersion =
            VK_MAKE_API_VERSION(0, MRHI_VERSION_MAJOR, MRHI_VERSION_MINOR, MRHI_VERSION_PATCH),
        .apiVersion = VK_API_VERSION_1_3,
    };
    const char* const* wanted = nullptr;
    uint32_t wantedCount = (uint32_t)mrhiVulkanSurfaceExtensions(&wanted);
    uint32_t offered = mrhiVulkanExtensions(vulkan, allocator, VK_NULL_HANDLE, wanted, wantedCount);
    const char* const debug = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    bool debugUtils = mrhiVulkanExtensions(vulkan, allocator, VK_NULL_HANDLE, &debug, 1) != 0;
    const char* extraNames = extra != nullptr ? extra->extensions : nullptr;
    size_t extraBytes = extra != nullptr ? extra->extensionsLength : 0;
    uint32_t extraCount = 0;
    for (size_t at = 0; at < extraBytes; ++at)
    {
        extraCount += extraNames[at] == 0 ? 1u : 0u;
    }
    uint32_t capacity = wantedCount + 1 + extraCount;
    const char** names =
        (const char**)mrhiAllocate(allocator, capacity * sizeof(const char*), alignof(const char*));
    if (names == nullptr)
    {
        return VK_NULL_HANDLE;
    }
    uint32_t count = 0;
    for (uint32_t i = 0; i < wantedCount; ++i)
    {
        if ((offered >> i & 1u) != 0)
        {
            names[count++] = wanted[i];
        }
    }
    if (debugUtils)
    {
        names[count++] = debug;
    }
    for (size_t at = 0; at < extraBytes; at += strlen(extraNames + at) + 1)
    {
        names[count++] = extraNames + at;
    }
    const VkInstanceCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application,
        .enabledExtensionCount = count,
        .ppEnabledExtensionNames = names,
    };
    VkInstance instance = VK_NULL_HANDLE;
    VkResult made = vulkan->vkCreateInstance(&info, nullptr, &instance);
    mrhiRelease(allocator, (void*)names, capacity * sizeof(const char*), alignof(const char*));
    if (made != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }
    vulkan->surfaces = (offered & 1u) != 0;
    if (!mrhiLoadVulkanInstance(vulkan, instance) ||
        (vulkan->surfaces && !mrhiLoadVulkanSurface(vulkan, instance)))
    {
        if (vulkan->vkDestroyInstance != nullptr)
        {
            vulkan->vkDestroyInstance(instance, nullptr);
        }
        return VK_NULL_HANDLE;
    }
    // Labels only help tools: an instance without them works the same.
    if (debugUtils)
    {
        (void)mrhiLoadVulkanDebug(vulkan, instance);
    }
    *enabledOut = offered;
    return instance;
}

// Whether a NUL-ended list of names has a name.
static bool Names(const char* names, size_t bytes, const char* name)
{
    for (size_t at = 0; at < bytes; at += strlen(names + at) + 1)
    {
        if (strcmp(names + at, name) == 0)
        {
            return true;
        }
    }
    return false;
}

// Takes an instance made elsewhere (mrhi-0018): its functions read through
// the program's vkGetInstanceProcAddr, surfaces and labels used where
// its extensions name them; VK_NULL_HANDLE where it is older than 1.3
// or a function is missing.
static VkInstance AdoptInstance(mrhiVulkan* vulkan, const mrhiInstanceVulkanAdopt* adopt,
                                uint32_t* enabledOut)
{
    PFN_vkGetInstanceProcAddr entry = nullptr;
    static_assert(sizeof(entry) == sizeof(adopt->getInstanceProcAddr), "function pointers");
    memcpy((void*)&entry, (const void*)&adopt->getInstanceProcAddr, sizeof(entry));
    VkInstance instance = (VkInstance)adopt->instance;
    if (adopt->apiVersion < VK_API_VERSION_1_3 || !mrhiAdoptVulkan(vulkan, entry))
    {
        return VK_NULL_HANDLE;
    }
    const char* const* wanted = nullptr;
    uint32_t wantedCount = (uint32_t)mrhiVulkanSurfaceExtensions(&wanted);
    uint32_t named = 0;
    for (uint32_t i = 0; i < wantedCount; ++i)
    {
        named |= Names(adopt->extensions, adopt->extensionsLength, wanted[i]) ? 1u << i : 0u;
    }
    vulkan->surfaces = (named & 1u) != 0;
    if (!mrhiLoadVulkanInstance(vulkan, instance) ||
        (vulkan->surfaces && !mrhiLoadVulkanSurface(vulkan, instance)))
    {
        return VK_NULL_HANDLE;
    }
    if (Names(adopt->extensions, adopt->extensionsLength, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
    {
        (void)mrhiLoadVulkanDebug(vulkan, instance);
    }
    *enabledOut = named;
    return instance;
}

// The chained struct of a type, or NULL.
static const mrhiChain* Find(const mrhiChain* chain, mrhiStructType type)
{
    for (const mrhiChain* node = chain; node != nullptr; node = node->next)
    {
        if (node->type == type)
        {
            return node;
        }
    }
    return nullptr;
}

mrhiResult mrhiCreateVulkanDriver(const mrhiAllocator* allocator, const mrhiChain* chain,
                                  uint32_t pendingLimit, uint32_t adapterLimit,
                                  mrhiInstanceDriver* driverOut)
{
    *driverOut = (mrhiInstanceDriver){0};
    const mrhiInstanceVulkanAdopt* adopt =
        (const mrhiInstanceVulkanAdopt*)Find(chain, mrhi_structInstanceVulkanAdopt);
    const mrhiInstanceVulkanExtensions* extra =
        (const mrhiInstanceVulkanExtensions*)Find(chain, mrhi_structInstanceVulkanExtensions);
    // Nothing is open until the loader opens or the instance is adopted.
    mrhiVulkan vulkan = {0};
    if (adopt == nullptr && !mrhiOpenVulkan(&vulkan))
    {
        return mrhi_success;
    }
    mrhiLayout layout = {.size = sizeof(VulkanDriver)};
    size_t pendingAt =
        mrhiLayoutAdd(&layout, pendingLimit, sizeof(mrhiDriverEvent), alignof(mrhiDriverEvent));
    size_t devicesAt =
        mrhiLayoutAdd(&layout, adapterLimit, sizeof(VkPhysicalDevice), alignof(VkPhysicalDevice));
    VulkanDriver* driver =
        layout.overflow ? nullptr : mrhiAllocate(allocator, layout.size, alignof(VulkanDriver));
    if (driver == nullptr)
    {
        mrhiCloseVulkan(&vulkan);
        return mrhi_errorCapacity;
    }
    uint32_t surfaceExtensions = 0;
    VkInstance instance = adopt != nullptr
                              ? AdoptInstance(&vulkan, adopt, &surfaceExtensions)
                              : CreateInstance(&vulkan, allocator, extra, &surfaceExtensions);
    if (instance == VK_NULL_HANDLE)
    {
        mrhiRelease(allocator, driver, layout.size, alignof(VulkanDriver));
        mrhiCloseVulkan(&vulkan);
        // The program asked for this instance: no driver would hide it.
        return adopt != nullptr || extra != nullptr ? mrhi_errorUnsupported : mrhi_success;
    }
    unsigned char* block = (unsigned char*)driver;
    *driver = (VulkanDriver){
        .allocator = *allocator,
        .bytes = layout.size,
        .vulkan = vulkan,
        .instance = instance,
        .surfaceExtensions = surfaceExtensions,
        .pending = (mrhiDriverEvent*)(block + pendingAt),
        .pendingLimit = pendingLimit,
        .devices = (VkPhysicalDevice*)(block + devicesAt),
        .deviceLimit = adapterLimit,
        .adopted = adopt != nullptr,
    };
    *driverOut = (mrhiInstanceDriver){.vtable = &s_vtable, .self = driver};
    return mrhi_success;
}

bool mrhiIsVulkanDriver(const mrhiInstanceDriver* driver)
{
    return driver->vtable == &s_vtable;
}

void* mrhiVulkanPhysicalDevice(uint64_t adapter)
{
    return (void*)DeviceOf(adapter);
}

void mrhiForgetVulkanDevice(const mrhiInstanceDriver* driver)
{
    VulkanDriver* vulkan = driver->self;
    mrhiVulkanEndRecipe(&vulkan->allocator, &vulkan->recipe);
    vulkan->described = false;
}

mrhiResult mrhiDescribeVulkanDriverDevice(const mrhiInstanceDriver* driver, uint64_t adapter,
                                          const mrhiDeviceDef* def, void** infoOut)
{
    VulkanDriver* vulkan = driver->self;
    mrhiForgetVulkanDevice(driver);
    mrhiResult status = mrhiVulkanComposeDevice(&vulkan->vulkan, &vulkan->allocator,
                                                DeviceOf(adapter), def, &vulkan->recipe);
    if (status != mrhi_success)
    {
        return status;
    }
    vulkan->described = true;
    vulkan->describedAdapter = adapter;
    vulkan->describedFeatures = def->features;
    *infoOut = &vulkan->recipe.info;
    return mrhi_success;
}
