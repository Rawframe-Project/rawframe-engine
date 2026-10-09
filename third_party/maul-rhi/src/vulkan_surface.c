// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Surfaces on Vulkan (vulkan_surface.h). Each platform header of the
// kept Khronos headers is compiled on its platform only: XCB and Wayland
// on Linux and the BSDs, Win32, Android and Metal on theirs. XCB's three
// types are declared here, so that no window library's header is
// needed to build; Win32's come from the system's windows.h.

#include "vulkan_surface.h"

#include "capabilities_core.h"
#include "vulkan_adapter.h"

#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// The Win32 header needs the types windows.h declares.
#include <vulkan/vulkan_win32.h>
#define MRHI_VULKAN_WIN32 1
#elif defined(__ANDROID__)
#include <vulkan/vulkan_android.h>
#define MRHI_VULKAN_ANDROID 1
#elif defined(__APPLE__)
#include <vulkan/vulkan_metal.h>
#define MRHI_VULKAN_METAL 1
#elif defined(__unix__)
typedef struct xcb_connection_t xcb_connection_t;
typedef uint32_t xcb_window_t;
typedef uint32_t xcb_visualid_t;
#include <vulkan/vulkan_wayland.h>
#include <vulkan/vulkan_xcb.h>
#define MRHI_VULKAN_XCB     1
#define MRHI_VULKAN_WAYLAND 1
#endif

// A platform surface extension and the source it takes.
typedef struct Platform
{
    const char* extension;
    mrhiStructType source;
} Platform;

// The platforms of this build, in the order of their extension bits
// after VK_KHR_surface and VK_EXT_swapchain_colorspace.
static const Platform s_platforms[] = {
#ifdef MRHI_VULKAN_WIN32
    {VK_KHR_WIN32_SURFACE_EXTENSION_NAME, mrhi_structSurfaceSourceWin32},
#endif
#ifdef MRHI_VULKAN_ANDROID
    {VK_KHR_ANDROID_SURFACE_EXTENSION_NAME, mrhi_structSurfaceSourceAndroid},
#endif
#ifdef MRHI_VULKAN_METAL
    {VK_EXT_METAL_SURFACE_EXTENSION_NAME, mrhi_structSurfaceSourceMetalLayer},
#endif
#ifdef MRHI_VULKAN_XCB
    {VK_KHR_XCB_SURFACE_EXTENSION_NAME, mrhi_structSurfaceSourceXcb},
#endif
#ifdef MRHI_VULKAN_WAYLAND
    {VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME, mrhi_structSurfaceSourceWayland},
#endif
    {nullptr, 0},
};

#define PLATFORMS (sizeof(s_platforms) / sizeof(s_platforms[0]) - 1)

static const char* const s_extensions[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,         VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME,
#ifdef MRHI_VULKAN_WIN32
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
#ifdef MRHI_VULKAN_ANDROID
    VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
#endif
#ifdef MRHI_VULKAN_METAL
    VK_EXT_METAL_SURFACE_EXTENSION_NAME,
#endif
#ifdef MRHI_VULKAN_XCB
    VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#endif
#ifdef MRHI_VULKAN_WAYLAND
    VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
#endif
};

static_assert(sizeof(s_extensions) / sizeof(s_extensions[0]) == PLATFORMS + 2,
              "an extension per platform");

size_t mrhiVulkanSurfaceExtensions(const char* const** namesOut)
{
    *namesOut = s_extensions;
    return sizeof(s_extensions) / sizeof(s_extensions[0]);
}

// An instance function read by name into its typed pointer.
static void Read(const mrhiVulkan* vulkan, VkInstance instance, const char* name, void* functionOut)
{
    PFN_vkVoidFunction function = vulkan->vkGetInstanceProcAddr(instance, name);
    memcpy(functionOut, (const void*)&function, sizeof(function));
}

static VkResult Create(const mrhiVulkan* vulkan, VkInstance instance, const mrhiChain* source,
                       VkSurfaceKHR* surfaceOut)
{
    switch (source->type)
    {
#ifdef MRHI_VULKAN_WIN32
    case mrhi_structSurfaceSourceWin32:
    {
        const mrhiSurfaceSourceWin32* win32 = (const mrhiSurfaceSourceWin32*)source;
        const VkWin32SurfaceCreateInfoKHR info = {
            .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
            .hinstance = (HINSTANCE)win32->hinstance,
            .hwnd = (HWND)win32->hwnd,
        };
        PFN_vkCreateWin32SurfaceKHR create = nullptr;
        Read(vulkan, instance, "vkCreateWin32SurfaceKHR", (void*)&create);
        return create(instance, &info, nullptr, surfaceOut);
    }
#endif
#ifdef MRHI_VULKAN_ANDROID
    case mrhi_structSurfaceSourceAndroid:
    {
        const mrhiSurfaceSourceAndroid* android = (const mrhiSurfaceSourceAndroid*)source;
        const VkAndroidSurfaceCreateInfoKHR info = {
            .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
            .window = (struct ANativeWindow*)android->window,
        };
        PFN_vkCreateAndroidSurfaceKHR create = nullptr;
        Read(vulkan, instance, "vkCreateAndroidSurfaceKHR", (void*)&create);
        return create(instance, &info, nullptr, surfaceOut);
    }
#endif
#ifdef MRHI_VULKAN_METAL
    case mrhi_structSurfaceSourceMetalLayer:
    {
        const mrhiSurfaceSourceMetalLayer* metal = (const mrhiSurfaceSourceMetalLayer*)source;
        const VkMetalSurfaceCreateInfoEXT info = {
            .sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT,
            .pLayer = metal->layer,
        };
        PFN_vkCreateMetalSurfaceEXT create = nullptr;
        Read(vulkan, instance, "vkCreateMetalSurfaceEXT", (void*)&create);
        return create(instance, &info, nullptr, surfaceOut);
    }
#endif
#ifdef MRHI_VULKAN_XCB
    case mrhi_structSurfaceSourceXcb:
    {
        const mrhiSurfaceSourceXcb* xcb = (const mrhiSurfaceSourceXcb*)source;
        const VkXcbSurfaceCreateInfoKHR info = {
            .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
            .connection = (xcb_connection_t*)xcb->connection,
            .window = xcb->window,
        };
        PFN_vkCreateXcbSurfaceKHR create = nullptr;
        Read(vulkan, instance, "vkCreateXcbSurfaceKHR", (void*)&create);
        return create(instance, &info, nullptr, surfaceOut);
    }
#endif
#ifdef MRHI_VULKAN_WAYLAND
    case mrhi_structSurfaceSourceWayland:
    {
        const mrhiSurfaceSourceWayland* wayland = (const mrhiSurfaceSourceWayland*)source;
        const VkWaylandSurfaceCreateInfoKHR info = {
            .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
            .display = (struct wl_display*)wayland->display,
            .surface = (struct wl_surface*)wayland->surface,
        };
        PFN_vkCreateWaylandSurfaceKHR create = nullptr;
        Read(vulkan, instance, "vkCreateWaylandSurfaceKHR", (void*)&create);
        return create(instance, &info, nullptr, surfaceOut);
    }
#endif
    default:
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

mrhiResult mrhiVulkanCreateSurface(const mrhiVulkan* vulkan, VkInstance instance, uint32_t enabled,
                                   const mrhiChain* source, VkSurfaceKHR* surfaceOut)
{
    bool usable = false;
    for (size_t i = 0; i < PLATFORMS; ++i)
    {
        usable = usable || (s_platforms[i].source == source->type && (enabled & 1u) != 0 &&
                            (enabled >> (i + 2) & 1u) != 0);
    }
    if (!usable)
    {
        return mrhi_errorUnsupported;
    }
    VkResult result = Create(vulkan, instance, source, surfaceOut);
    return result == VK_SUCCESS ? mrhi_success : mrhi_errorPlatform;
}

// A surface format the contract has, as the unorm or float format the
// surface's images are made in.
typedef struct Format
{
    VkFormat vulkan;
    mrhiFormat format;
} Format;

static const Format s_formats[] = {
    {VK_FORMAT_B8G8R8A8_UNORM, mrhi_formatBgra8Unorm},
    {VK_FORMAT_B8G8R8A8_SRGB, mrhi_formatBgra8Unorm},
    {VK_FORMAT_R8G8B8A8_UNORM, mrhi_formatRgba8Unorm},
    {VK_FORMAT_R8G8B8A8_SRGB, mrhi_formatRgba8Unorm},
    {VK_FORMAT_A2B10G10R10_UNORM_PACK32, mrhi_formatRgb10a2Unorm},
    {VK_FORMAT_R16G16B16A16_SFLOAT, mrhi_formatRgba16Float},
};

// A color space the contract's rows map, and its color.
typedef struct Space
{
    VkColorSpaceKHR space;
    mrhiColorPrimaries primaries;
    mrhiTransfer transfer;
    mrhiColorRange range;
} Space;

static const Space s_spaces[] = {
    {VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, mrhi_primariesBt709, mrhi_transferSrgb, mrhi_rangeStandard},
    {VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT, mrhi_primariesBt709, mrhi_transferSrgb,
     mrhi_rangeExtended},
    {VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT, mrhi_primariesBt709, mrhi_transferLinear,
     mrhi_rangeExtended},
    {VK_COLOR_SPACE_BT709_LINEAR_EXT, mrhi_primariesBt709, mrhi_transferLinear, mrhi_rangeStandard},
    {VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT, mrhi_primariesDisplayP3, mrhi_transferSrgb,
     mrhi_rangeStandard},
    {VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT, mrhi_primariesDisplayP3, mrhi_transferLinear,
     mrhi_rangeStandard},
    {VK_COLOR_SPACE_HDR10_ST2084_EXT, mrhi_primariesBt2020, mrhi_transferPq, mrhi_rangeStandard},
    {VK_COLOR_SPACE_BT2020_LINEAR_EXT, mrhi_primariesBt2020, mrhi_transferLinear,
     mrhi_rangeStandard},
};

// The contract's color for a surface format: false for one it has not.
static bool ColorOf(VkSurfaceFormatKHR surface, mrhiSurfaceColor* colorOut)
{
    const Format* format = nullptr;
    for (size_t i = 0; i < sizeof(s_formats) / sizeof(s_formats[0]); ++i)
    {
        format = s_formats[i].vulkan == surface.format ? &s_formats[i] : format;
    }
    const Space* space = nullptr;
    for (size_t i = 0; i < sizeof(s_spaces) / sizeof(s_spaces[0]); ++i)
    {
        space = s_spaces[i].space == surface.colorSpace ? &s_spaces[i] : space;
    }
    if (format == nullptr || space == nullptr)
    {
        return false;
    }
    *colorOut = (mrhiSurfaceColor){
        .format = format->format,
        .primaries = space->primaries,
        .transfer = space->transfer,
        .range = space->range,
    };
    return true;
}

static bool IsSameColor(mrhiSurfaceColor a, mrhiSurfaceColor b)
{
    return a.format == b.format && a.primaries == b.primaries && a.transfer == b.transfer &&
           a.range == b.range;
}

// The surface's formats, up to capacity: how many were read, 0 on a
// failure.
static uint32_t ReadFormats(const mrhiVulkan* vulkan, VkPhysicalDevice device, VkSurfaceKHR surface,
                            VkSurfaceFormatKHR* formats, uint32_t capacity)
{
    uint32_t count = capacity;
    VkResult result =
        vulkan->vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, formats);
    return result == VK_SUCCESS || result == VK_INCOMPLETE ? count : 0;
}

static void AddColors(const VkSurfaceFormatKHR* formats, uint32_t count, mrhiSurfaceCaps* caps)
{
    for (uint32_t i = 0; i < count && caps->colorCount < MRHI_SURFACE_COLORS; ++i)
    {
        mrhiSurfaceColor color;
        bool known = ColorOf(formats[i], &color);
        for (uint32_t j = 0; j < caps->colorCount && known; ++j)
        {
            known = !IsSameColor(caps->colors[j], color);
        }
        if (known)
        {
            caps->colors[caps->colorCount++] = color;
        }
    }
}

// The surface's present modes, up to capacity: how many were read, 0 on
// a failure.
static uint32_t ReadModes(const mrhiVulkan* vulkan, VkPhysicalDevice device, VkSurfaceKHR surface,
                          VkPresentModeKHR* modes, uint32_t capacity)
{
    uint32_t count = capacity;
    VkResult result =
        vulkan->vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, modes);
    return result == VK_SUCCESS || result == VK_INCOMPLETE ? count : 0;
}

static mrhiPresentModes ModesOf(const VkPresentModeKHR* modes, uint32_t count)
{
    mrhiPresentModes found = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        found |= modes[i] == VK_PRESENT_MODE_FIFO_KHR        ? mrhi_presentFifo
                 : modes[i] == VK_PRESENT_MODE_MAILBOX_KHR   ? mrhi_presentMailbox
                 : modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR ? mrhi_presentImmediate
                                                             : 0u;
    }
    return found;
}

static mrhiTextureUsage UsagesOf(VkImageUsageFlags flags)
{
    mrhiTextureUsage usages = 0;
    usages |= (flags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0 ? mrhi_textureRenderTarget : 0u;
    usages |= (flags & VK_IMAGE_USAGE_SAMPLED_BIT) != 0 ? mrhi_textureSampled : 0u;
    usages |= (flags & VK_IMAGE_USAGE_STORAGE_BIT) != 0 ? mrhi_textureStorage : 0u;
    usages |= (flags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0 ? mrhi_textureCopySource : 0u;
    usages |= (flags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0 ? mrhi_textureCopyDestination : 0u;
    return usages;
}

// Whether a surface format is 8-bit sRGB, which a configuration may take
// as its images' own format.
static bool IsSrgbImage(VkFormat format)
{
    return format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_R8G8B8A8_SRGB;
}

bool mrhiVulkanTwinImages(const VkSurfaceFormatKHR* formats, uint32_t count,
                          const mrhiSurfaceCaps* caps)
{
    uint32_t eight = 0;
    uint32_t twinned = 0;
    for (uint32_t i = 0; i < caps->colorCount; ++i)
    {
        mrhiSurfaceColor color = caps->colors[i];
        mrhiFormat twin = mrhiFormatSrgbPair(color.format);
        VkFormat srgb = mrhiVulkanFormat(twin, VK_FORMAT_UNDEFINED);
        if (!IsSrgbImage(srgb))
        {
            continue;
        }
        eight += 1;
        bool listed = false;
        for (uint32_t j = 0; j < count && !listed; ++j)
        {
            mrhiSurfaceColor as;
            listed =
                formats[j].format == srgb && ColorOf(formats[j], &as) && IsSameColor(as, color);
        }
        twinned += listed ? 1 : 0;
    }
    return eight > 0 && twinned == eight;
}

void mrhiVulkanCapsOf(const mrhiVulkanSurfaceFacts* facts, mrhiSurfaceCaps* capsOut)
{
    *capsOut = (mrhiSurfaceCaps){0};
    mrhiSurfaceCaps caps = {0};
    AddColors(facts->formats, facts->formatCount, &caps);
    caps.twinViews = facts->mutableFormat;
    caps.twinImages = mrhiVulkanTwinImages(facts->formats, facts->formatCount, &caps);
    caps.presentModes = ModesOf(facts->modes, facts->modeCount);
    VkCompositeAlphaFlagsKHR alpha = facts->alpha;
    caps.alphaModes |=
        (alpha & (VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR | VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)) != 0
            ? mrhi_alphaOpaque
            : 0u;
    caps.alphaModes |=
        (alpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) != 0 ? mrhi_alphaPremultiplied : 0u;
    caps.usages = UsagesOf(facts->usages);
    // The floors: a color, FIFO, opaque alpha, render targets and a way to
    // sRGB.
    caps.presentable = caps.colorCount > 0 && (caps.presentModes & mrhi_presentFifo) != 0 &&
                       (caps.alphaModes & mrhi_alphaOpaque) != 0 &&
                       (caps.usages & mrhi_textureRenderTarget) != 0 &&
                       (caps.twinViews || caps.twinImages);
    if (caps.presentable)
    {
        *capsOut = caps;
    }
}

void mrhiVulkanSurfaceCaps(const mrhiVulkan* vulkan, VkPhysicalDevice device, bool swapchain,
                           bool mutableFormat, VkSurfaceKHR surface, mrhiSurfaceCaps* capsOut)
{
    *capsOut = (mrhiSurfaceCaps){0};
    VkBool32 presents = VK_FALSE;
    VkSurfaceCapabilitiesKHR surfaceCaps;
    if (!swapchain ||
        vulkan->vkGetPhysicalDeviceSurfaceSupportKHR(device, mrhiVulkanQueueFamily(vulkan, device),
                                                     surface, &presents) != VK_SUCCESS ||
        presents != VK_TRUE ||
        vulkan->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &surfaceCaps) !=
            VK_SUCCESS)
    {
        return;
    }
    VkSurfaceFormatKHR formats[64];
    VkPresentModeKHR modes[16];
    const mrhiVulkanSurfaceFacts facts = {
        .formats = formats,
        .formatCount = ReadFormats(vulkan, device, surface, formats, 64),
        .modes = modes,
        .modeCount = ReadModes(vulkan, device, surface, modes, 16),
        .alpha = surfaceCaps.supportedCompositeAlpha,
        .usages = surfaceCaps.supportedUsageFlags,
        .mutableFormat = mutableFormat,
    };
    mrhiVulkanCapsOf(&facts, capsOut);
}

bool mrhiVulkanPickFormat(const VkSurfaceFormatKHR* formats, uint32_t count, mrhiSurfaceColor color,
                          VkSurfaceFormatKHR* formatOut)
{
    // An sRGB color (twin images) is listed as its unorm twin.
    VkFormat exact = mrhiVulkanFormat(color.format, VK_FORMAT_UNDEFINED);
    mrhiSurfaceColor listedAs = color;
    listedAs.format = IsSrgbImage(exact) ? mrhiFormatSrgbPair(color.format) : color.format;
    bool found = false;
    for (uint32_t i = 0; i < count; ++i)
    {
        mrhiSurfaceColor listed;
        bool same = ColorOf(formats[i], &listed) && IsSameColor(listed, listedAs);
        // The color's own format wins over its twin.
        if (same && (!found || formats[i].format == exact))
        {
            *formatOut = formats[i];
            found = true;
        }
    }
    return found;
}

bool mrhiVulkanSurfaceFormat(const mrhiVulkan* vulkan, VkPhysicalDevice device,
                             VkSurfaceKHR surface, mrhiSurfaceColor color,
                             VkSurfaceFormatKHR* formatOut)
{
    VkSurfaceFormatKHR formats[64];
    uint32_t count = ReadFormats(vulkan, device, surface, formats, 64);
    return mrhiVulkanPickFormat(formats, count, color, formatOut);
}
