// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Wayland window icons.

#include "wayland_icon.h"

#include "icon.h"

#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static uint32_t Side(const mwinIconCopyImage* image)
{
    return image->width > image->height ? image->width : image->height;
}

// An image into a square of ARGB with premultiplied alpha, centred.
static void Fill(const mwinIconCopyImage* image, uint32_t* square)
{
    uint32_t side = Side(image);
    uint32_t left = (side - image->width) / 2;
    uint32_t top = (side - image->height) / 2;
    memset(square, 0, (size_t)side * side * 4);
    for (uint32_t y = 0; y < image->height; y++)
    {
        for (uint32_t x = 0; x < image->width; x++)
        {
            const uint8_t* rgba = image->pixels + ((size_t)y * image->width + x) * 4;
            uint32_t alpha = rgba[3];
            uint32_t red = (rgba[0] * alpha + 127) / 255;
            uint32_t green = (rgba[1] * alpha + 127) / 255;
            uint32_t blue = (rgba[2] * alpha + 127) / 255;
            square[(size_t)(top + y) * side + left + x] =
                alpha << 24 | red << 16 | green << 8 | blue;
        }
    }
}

// Sets an icon on the toplevel, or none, and commits the surface.
static void Apply(mwinWaylandWindow* window, struct xdg_toplevel_icon_v1* icon)
{
    const mwinWaylandApi* api = &window->platform->api;
    struct xdg_toplevel_icon_manager_v1* manager = window->platform->toplevelIcons;
    (void)api->proxyMarshalFlags((struct wl_proxy*)manager, XDG_TOPLEVEL_ICON_MANAGER_V1_SET_ICON,
                                 nullptr, mwinWlVersion(api, manager), 0, window->toplevel, icon);
    (void)mwinWlRequest(api, window->surface, WL_SURFACE_COMMIT, nullptr, 0);
}

// A buffer of each image, in one pool, added to the icon.
static bool AddBuffers(mwinWaylandPlatform* platform, const mwinIconCopy* copy,
                       struct xdg_toplevel_icon_v1* icon, struct wl_buffer** buffers)
{
    const mwinWaylandApi* api = &platform->api;
    size_t bytes = 0;
    for (uint32_t i = 0; i < copy->count; i++)
    {
        bytes += (size_t)Side(&copy->images[i]) * Side(&copy->images[i]) * 4;
    }
    void* memory = nullptr;
    int fd = mwinWaylandMapMemory(bytes, &memory);
    if (fd < 0)
    {
        return false;
    }
    struct wl_proxy* pool = api->proxyMarshalFlags(
        (struct wl_proxy*)platform->shm, WL_SHM_CREATE_POOL, &wl_shm_pool_interface,
        mwinWlVersion(api, platform->shm), 0, nullptr, fd, (int32_t)bytes);
    (void)close(fd);
    size_t offset = 0;
    for (uint32_t i = 0; i < copy->count; i++)
    {
        int32_t side = (int32_t)Side(&copy->images[i]);
        Fill(&copy->images[i], (uint32_t*)((unsigned char*)memory + offset));
        buffers[i] = (struct wl_buffer*)api->proxyMarshalFlags(
            pool, WL_SHM_POOL_CREATE_BUFFER, &wl_buffer_interface, mwinWlVersion(api, pool), 0,
            nullptr, (int32_t)offset, side, side, side * 4, WL_SHM_FORMAT_ARGB8888);
        (void)api->proxyMarshalFlags((struct wl_proxy*)icon, XDG_TOPLEVEL_ICON_V1_ADD_BUFFER,
                                     nullptr, mwinWlVersion(api, icon), 0, buffers[i], 1);
        offset += (size_t)side * (size_t)side * 4;
    }
    // The buffers keep the memory; the pool and the mapping can go.
    (void)mwinWlRequest(api, pool, WL_SHM_POOL_DESTROY, nullptr, WL_MARSHAL_FLAG_DESTROY);
    munmap(memory, bytes);
    return true;
}

mwinOutcome mwinWaylandSetIcon(mwinWaylandWindow* window, const mwinRequest* request)
{
    mwinWaylandPlatform* platform = window->platform;
    const mwinWaylandApi* api = &platform->api;
    const mwinIconCopy* copy = request->value.icon;
    if (platform->toplevelIcons == nullptr || platform->shm == nullptr)
    {
        return mwin_outcomeUnsupported;
    }
    if (copy->count == 0)
    {
        Apply(window, nullptr);
        return mwin_outcomeDone;
    }
    struct xdg_toplevel_icon_v1* icon =
        mwinWlCreateFor(api, platform->toplevelIcons, XDG_TOPLEVEL_ICON_MANAGER_V1_CREATE_ICON,
                        &xdg_toplevel_icon_v1_interface, nullptr);
    struct wl_buffer* buffers[MWIN_ICON_IMAGES] = {nullptr};
    bool added = AddBuffers(platform, copy, icon, buffers);
    if (added)
    {
        Apply(window, icon);
    }
    // The toplevel keeps its icon once set; the buffers must outlive the
    // icon object.
    (void)mwinWlRequest(api, icon, XDG_TOPLEVEL_ICON_V1_DESTROY, nullptr, WL_MARSHAL_FLAG_DESTROY);
    for (uint32_t i = 0; i < copy->count; i++)
    {
        if (buffers[i] != nullptr)
        {
            (void)mwinWlRequest(api, buffers[i], WL_BUFFER_DESTROY, nullptr,
                                WL_MARSHAL_FLAG_DESTROY);
        }
    }
    return added ? mwin_outcomeDone : mwin_outcomeFailed;
}
