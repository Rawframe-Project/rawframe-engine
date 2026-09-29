// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Opens the Vulkan loader by its platform names (mrhi-0003) and reads the
// driver's functions from it.

#include "vulkan_api.h"

#include <stddef.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// The loader's names on this platform, in the order they are tried.
#if defined(_WIN32)
static const char* const s_names[] = {"vulkan-1.dll"};
#elif defined(__APPLE__)
static const char* const s_names[] = {"libvulkan.1.dylib", "libvulkan.dylib"};
#else
static const char* const s_names[] = {"libvulkan.so.1", "libvulkan.so"};
#endif

static void* OpenLibrary(const char* name)
{
#ifdef _WIN32
    return (void*)LoadLibraryA(name);
#else
    return dlopen(name, RTLD_NOW | RTLD_LOCAL);
#endif
}

static void CloseLibrary(void* library)
{
#ifdef _WIN32
    FreeLibrary((HMODULE)library);
#else
    dlclose(library);
#endif
}

static PFN_vkGetInstanceProcAddr FindEntry(void* library)
{
    PFN_vkGetInstanceProcAddr entry = nullptr;
#ifdef _WIN32
    FARPROC address = GetProcAddress((HMODULE)library, "vkGetInstanceProcAddr");
#else
    void* address = dlsym(library, "vkGetInstanceProcAddr");
#endif
    // A function pointer read from the platform's untyped symbol.
    static_assert(sizeof(address) == sizeof(entry), "symbols are function pointers");
    memcpy((void*)&entry, (const void*)&address, sizeof(entry));
    return entry;
}

// A function the driver reads: its name, and where its pointer lies in
// its table.
typedef struct Function
{
    const char* name;
    size_t offset;
} Function;

#define MRHI_VULKAN_LOADER_ENTRY(name) {#name, offsetof(mrhiVulkan, name)},
#define MRHI_VULKAN_DEVICE_ENTRY(name) {#name, offsetof(mrhiVulkanDevice, name)},

static const Function s_global[] = {MRHI_VULKAN_GLOBAL(MRHI_VULKAN_LOADER_ENTRY)};
static const Function s_instance[] = {MRHI_VULKAN_INSTANCE(MRHI_VULKAN_LOADER_ENTRY)};
static const Function s_surface[] = {MRHI_VULKAN_SURFACE(MRHI_VULKAN_LOADER_ENTRY)};
static const Function s_debug[] = {MRHI_VULKAN_DEBUG(MRHI_VULKAN_LOADER_ENTRY)};
static const Function s_device[] = {MRHI_VULKAN_DEVICE(MRHI_VULKAN_DEVICE_ENTRY)};
static const Function s_swapchain[] = {MRHI_VULKAN_SWAPCHAIN(MRHI_VULKAN_DEVICE_ENTRY)};

// Stores a function in its table's typed field: every Vulkan function
// pointer has the one representation.
static void Store(void* table, size_t offset, PFN_vkVoidFunction function)
{
    static_assert(sizeof(PFN_vkVoidFunction) == sizeof(PFN_vkCreateInstance), "one size");
    memcpy((unsigned char*)table + offset, (const void*)&function, sizeof(function));
}

// Reads functions through vkGetInstanceProcAddr: false when one is
// missing.
static bool ReadInstance(mrhiVulkan* vulkan, VkInstance instance, const Function* functions,
                         size_t count)
{
    bool found = true;
    for (size_t i = 0; i < count; ++i)
    {
        PFN_vkVoidFunction function = vulkan->vkGetInstanceProcAddr(instance, functions[i].name);
        found = found && function != nullptr;
        Store(vulkan, functions[i].offset, function);
    }
    return found;
}

bool mrhiOpenVulkan(mrhiVulkan* vulkan)
{
    *vulkan = (mrhiVulkan){0};
    for (size_t i = 0; i < sizeof(s_names) / sizeof(s_names[0]) && vulkan->library == nullptr; ++i)
    {
        vulkan->library = OpenLibrary(s_names[i]);
    }
    if (vulkan->library == nullptr)
    {
        return false;
    }
    vulkan->vkGetInstanceProcAddr = FindEntry(vulkan->library);
    bool found =
        vulkan->vkGetInstanceProcAddr != nullptr &&
        ReadInstance(vulkan, VK_NULL_HANDLE, s_global, sizeof(s_global) / sizeof(s_global[0]));
    if (!found)
    {
        mrhiCloseVulkan(vulkan);
    }
    return found;
}

bool mrhiLoadVulkanInstance(mrhiVulkan* vulkan, VkInstance instance)
{
    return ReadInstance(vulkan, instance, s_instance, sizeof(s_instance) / sizeof(s_instance[0]));
}

bool mrhiLoadVulkanSurface(mrhiVulkan* vulkan, VkInstance instance)
{
    return ReadInstance(vulkan, instance, s_surface, sizeof(s_surface) / sizeof(s_surface[0]));
}

bool mrhiLoadVulkanDebug(mrhiVulkan* vulkan, VkInstance instance)
{
    if (ReadInstance(vulkan, instance, s_debug, sizeof(s_debug) / sizeof(s_debug[0])))
    {
        return true;
    }
    for (size_t i = 0; i < sizeof(s_debug) / sizeof(s_debug[0]); ++i)
    {
        Store(vulkan, s_debug[i].offset, nullptr);
    }
    return false;
}

// Reads functions through vkGetDeviceProcAddr: false when one is
// missing.
static bool ReadDevice(const mrhiVulkan* vulkan, VkDevice device, const Function* functions,
                       size_t count, mrhiVulkanDevice* table)
{
    bool found = true;
    for (size_t i = 0; i < count; ++i)
    {
        PFN_vkVoidFunction function = vulkan->vkGetDeviceProcAddr(device, functions[i].name);
        found = found && function != nullptr;
        Store(table, functions[i].offset, function);
    }
    return found;
}

bool mrhiLoadVulkanDevice(const mrhiVulkan* vulkan, VkDevice device, bool swapchain,
                          mrhiVulkanDevice* functions)
{
    *functions = (mrhiVulkanDevice){
#define MRHI_VULKAN_COPY(name) .name = vulkan->name,
        MRHI_VULKAN_DEBUG(MRHI_VULKAN_COPY)
#undef MRHI_VULKAN_COPY
    };
    return ReadDevice(vulkan, device, s_device, sizeof(s_device) / sizeof(s_device[0]),
                      functions) &&
           (!swapchain || ReadDevice(vulkan, device, s_swapchain,
                                     sizeof(s_swapchain) / sizeof(s_swapchain[0]), functions));
}

void mrhiCloseVulkan(mrhiVulkan* vulkan)
{
    if (vulkan->library != nullptr)
    {
        CloseLibrary(vulkan->library);
    }
    *vulkan = (mrhiVulkan){0};
}
