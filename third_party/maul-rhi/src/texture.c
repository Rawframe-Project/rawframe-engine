// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Textures: the def's shape checked against its kind, its format's block
// and the device's limits, its usages and sample count against the
// format's capabilities on the device, and its view formats limited to
// the format's sRGB or linear twin. Destroying one destroys its views
// first.

#include "capabilities_core.h"
#include "device_core.h"

#define TEXTURE_DEF_COOKIE 0x6D727478u

mrhiTextureDef mrhiDefaultTextureDef(void)
{
    mrhiTextureDef def = {0};
    def.cookie = TEXTURE_DEF_COOKIE;
    def.kind = mrhi_texture2d;
    def.depthOrLayers = 1;
    def.mipLevels = 1;
    def.sampleCount = 1;
    return def;
}

// The mips of a full chain for a texture's largest dimension.
static uint32_t FullChain(const mrhiTextureDef* def)
{
    uint32_t largest = def->width > def->height ? def->width : def->height;
    if (def->kind == mrhi_texture3d && def->depthOrLayers > largest)
    {
        largest = def->depthOrLayers;
    }
    uint32_t levels = 1;
    while (largest > 1)
    {
        largest >>= 1;
        ++levels;
    }
    return levels;
}

// Whether the size and layers fit the kind and the format's block, and
// the mips a full chain.
static bool IsShapeValid(const mrhiTextureDef* def)
{
    mrhiFormatBlock block = mrhiGetFormatBlock(def->format);
    bool compressed = block.width > 1 || block.height > 1;
    bool sized = def->width > 0 && def->height > 0 && def->depthOrLayers > 0 &&
                 def->width % block.width == 0 && def->height % block.height == 0;
    bool square = def->width == def->height;
    bool layers = false;
    switch (def->kind)
    {
    case mrhi_texture2d:
        layers = def->depthOrLayers == 1;
        break;
    case mrhi_texture2dArray:
        layers = true;
        break;
    case mrhi_textureCube:
        layers = square && def->depthOrLayers == 6;
        break;
    case mrhi_textureCubeArray:
        layers = square && def->depthOrLayers % 6 == 0;
        break;
    case mrhi_texture3d:
        layers = !compressed;
        break;
    default:
        return false;
    }
    return sized && layers && def->mipLevels >= 1 && def->mipLevels <= FullChain(def);
}

// Whether the size is within the device's limits.
static bool FitsLimits(const mrhiTextureDef* def, const mrhiLimits* limits)
{
    if (def->kind == mrhi_texture3d)
    {
        uint32_t most = limits->textureDimension3d;
        return def->width <= most && def->height <= most && def->depthOrLayers <= most;
    }
    uint32_t most = limits->textureDimension2d;
    return def->width <= most && def->height <= most &&
           def->depthOrLayers <= limits->textureArrayLayers;
}

// Whether the sample count is 1, 2 or 4, several only on a 2D texture of
// one mip.
static bool AreSamplesValid(const mrhiTextureDef* def)
{
    bool samples = def->sampleCount == 1 || def->sampleCount == 2 || def->sampleCount == 4;
    return samples &&
           (def->sampleCount == 1 || (def->kind == mrhi_texture2d && def->mipLevels == 1));
}

// Whether the usages are well formed: known bits, a transient texture
// only a render target, several samples only on a render target without
// storage.
static bool IsUsageValid(const mrhiTextureDef* def)
{
    mrhiTextureUsage usage = def->usage;
    mrhiTextureUsage transient = mrhi_textureTransient | mrhi_textureRenderTarget;
    bool bits = usage != 0 && (usage & ~mrhiTextureUsageKnown) == 0 &&
                ((usage & mrhi_textureTransient) == 0 || usage == transient);
    return bits && (def->sampleCount == 1 || ((usage & mrhi_textureRenderTarget) != 0 &&
                                              (usage & mrhi_textureStorage) == 0));
}

// Whether every view format is unused or the format's twin.
static bool AreViewFormatsValid(const mrhiTextureDef* def)
{
    mrhiFormat twin = mrhiFormatSrgbPair(def->format);
    for (uint32_t i = 0; i < MRHI_VIEW_FORMATS; ++i)
    {
        mrhiFormat format = def->viewFormats[i];
        if (format != mrhi_formatNone && format != twin)
        {
            return false;
        }
    }
    return true;
}

// A compressed family the device was not granted has no capabilities
// there, so its textures and views are refused here too.
bool mrhiFormatTakes(const mrhiDevice* device, mrhiFormat format, mrhiTextureUsage usage)
{
    const mrhiFormatCaps* caps = &device->formatCaps[mrhiFormatIndex(format)];
    return ((usage & mrhi_textureSampled) == 0 || caps->sampling) &&
           ((usage & mrhi_textureStorage) == 0 || caps->storage) &&
           ((usage & mrhi_textureRenderTarget) == 0 || caps->rendering);
}

// Whether the format can take the usages and sample count on the device.
static bool IsGranted(const mrhiDevice* device, const mrhiTextureDef* def)
{
    const mrhiFormatCaps* caps = &device->formatCaps[mrhiFormatIndex(def->format)];
    return mrhiFormatTakes(device, def->format, def->usage) &&
           (caps->sampleCounts & def->sampleCount) != 0;
}

mrhiResult mrhiCheckTextureShape(mrhiDevice* device, const mrhiTextureDef* def)
{
    mrhiResult status = mrhiCheckObjectDef(device, MRHI_DEF_HEAD(def), TEXTURE_DEF_COOKIE);
    if (status != mrhi_success)
    {
        return status;
    }
    if (!mrhiIsFormatKnown(def->format) || !IsShapeValid(def) || !AreSamplesValid(def) ||
        !AreViewFormatsValid(def))
    {
        return mrhiDeviceMisuse(device);
    }
    if (!FitsLimits(def, &device->limits) ||
        !mrhiFormatFamilyGranted(def->format, &device->features))
    {
        return mrhi_errorUnsupported;
    }
    return mrhi_success;
}

mrhiResult mrhiCheckTextureUsage(mrhiDevice* device, const mrhiTextureDef* def)
{
    if (!IsUsageValid(def))
    {
        return mrhiDeviceMisuse(device);
    }
    return IsGranted(device, def) ? mrhi_success : mrhi_errorUnsupported;
}

// Checks a def on a live device: success, or the refusal.
static mrhiResult CheckTexture(mrhiDevice* device, const mrhiTextureDef* def)
{
    mrhiResult status = mrhiCheckTextureShape(device, def);
    if (status != mrhi_success)
    {
        return status;
    }
    status = mrhiCheckTextureUsage(device, def);
    if (status != mrhi_success)
    {
        return status;
    }
    return mrhiDeviceUsable(device);
}

mrhiResult mrhiCreateTexture(mrhiDevice* device, const mrhiTextureDef* def,
                             mrhiTextureId* textureOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (def == nullptr || textureOut == nullptr)
    {
        return mrhiDeviceMisuse(device);
    }
    mrhiResult status = CheckTexture(device, def);
    if (status != mrhi_success)
    {
        return status;
    }
    uint32_t index1 = 0;
    uint32_t generation = 0;
    if (!mrhiPoolAcquire(&device->textures, &index1, &generation))
    {
        return mrhi_errorCapacity;
    }
    uint64_t handle = 0;
    status = mrhiDriverStatus(
        device, device->driver.vtable->createTexture(device->driver.self, def, &handle));
    if (status != mrhi_success)
    {
        mrhiPoolRelease(&device->textures, index1);
        return status;
    }
    mrhiTextureSlot* slot = &device->textureSlots[index1 - 1];
    *slot = (mrhiTextureSlot){.handle = handle, .def = *def};
    slot->def.next = nullptr;
    slot->def.label = nullptr;
    slot->def.labelLength = 0;
    *textureOut = (mrhiTextureId){index1, generation};
    return mrhi_success;
}

mrhiResult mrhiDestroyTexture(mrhiDevice* device, mrhiTextureId texture)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (!mrhiPoolIsLive(&device->textures, texture.index1, texture.generation))
    {
        return mrhi_errorStale;
    }
    mrhiTextureSlot* slot = &device->textureSlots[texture.index1 - 1];
    mrhiDestroyViewsOf(device, slot);
    device->driver.vtable->destroyTexture(device->driver.self, slot->handle);
    mrhiPoolRelease(&device->textures, texture.index1);
    return mrhi_success;
}
