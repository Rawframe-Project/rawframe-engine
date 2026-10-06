// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Views: a subrange of a texture's mips and layers, seen with a kind the
// texture allows, one of its formats, some of its usages and one of its
// format's aspects. A texture's views form a list through their slots,
// so destroying the texture destroys them without a search.

#include "capabilities_core.h"
#include "device_core.h"
#include "heap_core.h"

#include <stdckdint.h>

#define VIEW_DEF_COOKIE 0x6D727677u

mrhiViewDef mrhiDefaultViewDef(void)
{
    mrhiViewDef def = {0};
    def.cookie = VIEW_DEF_COOKIE;
    def.kind = mrhi_texture2d;
    def.aspect = mrhi_aspectAll;
    def.mipCount = MRHI_REMAINING;
    def.layerCount = MRHI_REMAINING;
    return def;
}

// Whether the def's kind and aspect are known ones. Its usage needs no
// check here: it is refused unless it is some of the texture's.
static bool IsKnown(const mrhiViewDef* def)
{
    return def->kind <= mrhi_texture3d && def->aspect <= mrhi_aspectStencilOnly;
}

uint32_t mrhiResolveCount(uint32_t count, uint32_t base, uint32_t total)
{
    if (count != MRHI_REMAINING)
    {
        return count;
    }
    return base < total ? total - base : 0;
}

bool mrhiIsRangeValid(uint32_t base, uint32_t count, uint32_t total)
{
    uint32_t end = 0;
    return count > 0 && !ckd_add(&end, base, count) && end <= total;
}

// Whether the texture may be seen with the view's kind and layers.
static bool IsKindValid(const mrhiTextureDef* texture, const mrhiViewDef* view)
{
    bool flat = texture->kind != mrhi_texture3d;
    bool cube = texture->kind == mrhi_textureCube || texture->kind == mrhi_textureCubeArray;
    switch (view->kind)
    {
    case mrhi_texture2d:
        return flat && view->layerCount == 1;
    case mrhi_texture2dArray:
        return flat;
    case mrhi_textureCube:
        return cube && view->layerCount == 6;
    case mrhi_textureCubeArray:
        return cube && view->layerCount % 6 == 0;
    default:
        return !flat;
    }
}

// Whether the format, never none once resolved, is the texture's own or
// one it was given.
static bool IsFormatGiven(const mrhiTextureDef* texture, mrhiFormat format)
{
    if (format == texture->format)
    {
        return true;
    }
    for (uint32_t i = 0; i < MRHI_VIEW_FORMATS; ++i)
    {
        if (texture->viewFormats[i] == format)
        {
            return true;
        }
    }
    return false;
}

bool mrhiFormatHasAspect(mrhiFormat format, mrhiTextureAspect aspect)
{
    switch (aspect)
    {
    case mrhi_aspectDepthOnly:
        return mrhiFormatHasDepth(format);
    case mrhi_aspectStencilOnly:
        return mrhiFormatHasStencil(format);
    default:
        return true;
    }
}

bool mrhiResolveView(const mrhiTextureDef* texture, const mrhiViewDef* def,
                     mrhiViewDef* resolvedOut)
{
    mrhiViewDef view = *def;
    view.next = nullptr;
    view.format = view.format == mrhi_formatNone ? texture->format : view.format;
    view.usage = view.usage == 0 ? texture->usage : view.usage;
    uint32_t layers = texture->kind == mrhi_texture3d ? 1 : texture->depthOrLayers;
    view.mipCount = mrhiResolveCount(view.mipCount, view.baseMip, texture->mipLevels);
    view.layerCount = mrhiResolveCount(view.layerCount, view.baseLayer, layers);
    *resolvedOut = view;
    mrhiTextureUsage transient = mrhi_textureTransient | mrhi_textureRenderTarget;
    bool usage = (view.usage & ~texture->usage) == 0 &&
                 ((view.usage & mrhi_textureTransient) == 0 || view.usage == transient);
    return usage && IsFormatGiven(texture, view.format) &&
           mrhiFormatHasAspect(texture->format, view.aspect) &&
           mrhiIsRangeValid(view.baseMip, view.mipCount, texture->mipLevels) &&
           mrhiIsRangeValid(view.baseLayer, view.layerCount, layers) && IsKindValid(texture, &view);
}

// Checks a def on a live device and resolves it: success, or the
// refusal.
static mrhiResult CheckView(mrhiDevice* device, const mrhiViewDef* def, mrhiViewDef* resolvedOut)
{
    mrhiResult status = mrhiCheckObjectDef(device, MRHI_DEF_HEAD(def), VIEW_DEF_COOKIE);
    if (status != mrhi_success)
    {
        return status;
    }
    if (!IsKnown(def))
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticViewDef);
    }
    status = mrhiDeviceUsable(device);
    if (status != mrhi_success)
    {
        return status;
    }
    if (!mrhiPoolIsLive(&device->textures, def->texture.index1, def->texture.generation))
    {
        return mrhi_errorStale;
    }
    const mrhiTextureDef* texture = &device->textureSlots[def->texture.index1 - 1].def;
    if (!mrhiResolveView(texture, def, resolvedOut))
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticViewRange);
    }
    return mrhiFormatTakes(device, resolvedOut->format, resolvedOut->usage) ? mrhi_success
                                                                            : mrhi_errorUnsupported;
}

mrhiResult mrhiCreateView(mrhiDevice* device, const mrhiViewDef* def, mrhiViewId* viewOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (def == nullptr || viewOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    mrhiViewDef resolved;
    mrhiResult status = CheckView(device, def, &resolved);
    if (status != mrhi_success)
    {
        return status;
    }
    uint32_t index1 = 0;
    uint32_t generation = 0;
    if (!mrhiPoolAcquire(&device->views, &index1, &generation))
    {
        return mrhi_errorCapacity;
    }
    mrhiTextureSlot* texture = &device->textureSlots[def->texture.index1 - 1];
    uint64_t handle = 0;
    status =
        mrhiDriverStatus(device, device->driver.vtable->createView(
                                     device->driver.self, texture->handle, &resolved, &handle));
    if (status != mrhi_success)
    {
        mrhiPoolRelease(&device->views, index1);
        return status;
    }
    device->viewSlots[index1 - 1] = (mrhiViewSlot){
        .handle = handle,
        .def = resolved,
        .texture = def->texture.index1,
        .next = texture->firstView,
    };
    device->viewSlots[index1 - 1].def.label = nullptr;
    device->viewSlots[index1 - 1].def.labelLength = 0;
    if (texture->firstView != 0)
    {
        device->viewSlots[texture->firstView - 1].previous = index1;
    }
    texture->firstView = index1;
    *viewOut = (mrhiViewId){index1, generation};
    return mrhi_success;
}

// Ends a view: the driver's object and the id.
static void EndView(mrhiDevice* device, uint32_t index1)
{
    if (device->viewSlots[index1 - 1].heapRefs > 0)
    {
        mrhiForgetHeapObject(device, mrhiHeapObjectView, index1);
    }
    device->driver.vtable->destroyView(device->driver.self, device->viewSlots[index1 - 1].handle);
    mrhiPoolRelease(&device->views, index1);
}

mrhiResult mrhiDestroyView(mrhiDevice* device, mrhiViewId view)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (!mrhiPoolIsLive(&device->views, view.index1, view.generation))
    {
        return mrhi_errorStale;
    }
    const mrhiViewSlot* slot = &device->viewSlots[view.index1 - 1];
    if (slot->previous != 0)
    {
        device->viewSlots[slot->previous - 1].next = slot->next;
    }
    else
    {
        device->textureSlots[slot->texture - 1].firstView = slot->next;
    }
    if (slot->next != 0)
    {
        device->viewSlots[slot->next - 1].previous = slot->previous;
    }
    EndView(device, view.index1);
    return mrhi_success;
}

void mrhiDestroyViewsOf(mrhiDevice* device, mrhiTextureSlot* texture)
{
    while (texture->firstView != 0)
    {
        uint32_t index1 = texture->firstView;
        texture->firstView = device->viewSlots[index1 - 1].next;
        EndView(device, index1);
    }
}
