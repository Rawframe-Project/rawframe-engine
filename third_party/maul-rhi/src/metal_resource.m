// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Metal driver's objects (mrhi-0003). Buffers and textures live in
// private memory, since the frame graph fills and reads them; a
// transient render target lives in tile memory where the device keeps
// one there. A texture with view formats, or with stencil, which a view
// sees as X32_Stencil8, may be viewed in another pixel format. Samplers
// are made to sit in argument buffers, for the heaps.

#include "metal_resource.h"

#include "invariant.h"
#include "metal_names.h"

static uint64_t HandleOf(id object)
{
    return (uint64_t)(uintptr_t)(void*)object;
}

id mrhiMetalObject(uint64_t handle)
{
    MRHI_ASSERT(handle != 0);
    return (id)(void*)(uintptr_t)handle;
}

void mrhiMetalRelease(uint64_t handle)
{
    [mrhiMetalObject(handle) release];
}

mrhiResult mrhiMetalCreateBuffer(id<MTLDevice> device, const mrhiBufferDef* def,
                                 uint64_t* handleOut)
{
    @autoreleasepool
    {
        id<MTLBuffer> buffer = [device newBufferWithLength:def->size
                                                   options:MTLResourceStorageModePrivate];
        *handleOut = HandleOf(buffer);
        if (buffer == nil)
        {
            return mrhi_errorCapacity;
        }
        if (def->labelLength > 0)
        {
            buffer.label = mrhiMetalLabel(def->label, def->labelLength);
        }
    }
    return mrhi_success;
}

// Whether a transient render target lives in tile memory: on Apple
// family GPUs, which keep memoryless textures.
static bool IsMemoryless(id<MTLDevice> device, const mrhiTextureDef* def)
{
    return (def->usage & mrhi_textureTransient) != 0 && [device supportsFamily:MTLGPUFamilyApple1];
}

static MTLTextureUsage UsageOf(const mrhiTextureDef* def)
{
    MTLTextureUsage usage = MTLTextureUsageUnknown;
    usage |= (def->usage & mrhi_textureSampled) != 0 ? MTLTextureUsageShaderRead : 0;
    usage |= (def->usage & mrhi_textureStorage) != 0
                 ? MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite
                 : 0;
    usage |= (def->usage & mrhi_textureRenderTarget) != 0 ? MTLTextureUsageRenderTarget : 0;
    bool views = def->format == mrhi_formatDepthStencil;
    for (int i = 0; i < MRHI_VIEW_FORMATS; ++i)
    {
        views = views || def->viewFormats[i] != mrhi_formatNone;
    }
    return usage | (views ? MTLTextureUsagePixelFormatView : 0);
}

// A texture's descriptor in private memory, which the caller's
// autorelease pool owns.
static MTLTextureDescriptor* Describe(const mrhiTextureDef* def)
{
    MTLTextureDescriptor* descriptor = [[[MTLTextureDescriptor alloc] init] autorelease];
    bool volume = def->kind == mrhi_texture3d;
    bool cube = def->kind == mrhi_textureCube || def->kind == mrhi_textureCubeArray;
    descriptor.textureType = mrhiMetalTextureType(def->kind, def->sampleCount);
    descriptor.pixelFormat = mrhiMetalFormat(def->format);
    descriptor.width = def->width;
    descriptor.height = def->height;
    descriptor.depth = volume ? def->depthOrLayers : 1;
    descriptor.arrayLength = volume ? 1 : cube ? def->depthOrLayers / 6 : def->depthOrLayers;
    descriptor.mipmapLevelCount = def->mipLevels;
    descriptor.sampleCount = def->sampleCount;
    descriptor.usage = UsageOf(def);
    descriptor.storageMode = MTLStorageModePrivate;
    return descriptor;
}

mrhiResult mrhiMetalCreateTexture(id<MTLDevice> device, const mrhiTextureDef* def,
                                  uint64_t* handleOut)
{
    @autoreleasepool
    {
        MTLTextureDescriptor* descriptor = Describe(def);
        if (IsMemoryless(device, def))
        {
            descriptor.storageMode = MTLStorageModeMemoryless;
        }
        id<MTLTexture> texture = [device newTextureWithDescriptor:descriptor];
        *handleOut = HandleOf(texture);
        if (texture == nil)
        {
            return mrhi_errorCapacity;
        }
        if (def->labelLength > 0)
        {
            texture.label = mrhiMetalLabel(def->label, def->labelLength);
        }
    }
    return mrhi_success;
}

mrhiResult mrhiMetalCreateView(uint64_t texture, const mrhiViewDef* def, uint64_t* handleOut)
{
    @autoreleasepool
    {
        id<MTLTexture> source = mrhiMetalObject(texture);
        MTLPixelFormat format = mrhiMetalAspectFormat(mrhiMetalFormat(def->format), def->aspect);
        MTLTextureType type = mrhiMetalTextureType(def->kind, (uint32_t)source.sampleCount);
        id<MTLTexture> view =
            [source newTextureViewWithPixelFormat:format
                                      textureType:type
                                           levels:NSMakeRange(def->baseMip, def->mipCount)
                                           slices:NSMakeRange(def->baseLayer, def->layerCount)];
        *handleOut = HandleOf(view);
        if (view == nil)
        {
            return mrhi_errorCapacity;
        }
        if (def->labelLength > 0)
        {
            view.label = mrhiMetalLabel(def->label, def->labelLength);
        }
    }
    return mrhi_success;
}

mrhiResult mrhiMetalCreateSampler(id<MTLDevice> device, const mrhiSamplerDef* def,
                                  uint64_t* handleOut)
{
    @autoreleasepool
    {
        MTLSamplerDescriptor* descriptor = [[[MTLSamplerDescriptor alloc] init] autorelease];
        descriptor.minFilter = def->minFilter == mrhi_filterLinear ? MTLSamplerMinMagFilterLinear
                                                                   : MTLSamplerMinMagFilterNearest;
        descriptor.magFilter = def->magFilter == mrhi_filterLinear ? MTLSamplerMinMagFilterLinear
                                                                   : MTLSamplerMinMagFilterNearest;
        descriptor.mipFilter = def->mipFilter == mrhi_filterLinear ? MTLSamplerMipFilterLinear
                                                                   : MTLSamplerMipFilterNearest;
        descriptor.sAddressMode = mrhiMetalAddress(def->addressU);
        descriptor.tAddressMode = mrhiMetalAddress(def->addressV);
        descriptor.rAddressMode = mrhiMetalAddress(def->addressW);
        descriptor.lodMinClamp = def->lodMin;
        descriptor.lodMaxClamp = def->lodMax;
        descriptor.maxAnisotropy = def->maxAnisotropy;
        if (def->compare != mrhi_compareNone)
        {
            descriptor.compareFunction = mrhiMetalCompare(def->compare);
        }
        descriptor.supportArgumentBuffers = YES;
        if (def->labelLength > 0)
        {
            descriptor.label = mrhiMetalLabel(def->label, def->labelLength);
        }
        id<MTLSamplerState> sampler = [device newSamplerStateWithDescriptor:descriptor];
        *handleOut = HandleOf(sampler);
        if (sampler == nil)
        {
            return mrhi_errorCapacity;
        }
    }
    return mrhi_success;
}

mrhiResult mrhiMetalCreateQuerySet(id<MTLDevice> device, const mrhiQuerySetDef* def,
                                   uint64_t* handleOut)
{
    MRHI_ASSERT(def->type == mrhi_queryOcclusion);
    @autoreleasepool
    {
        id<MTLBuffer> results = [device newBufferWithLength:(NSUInteger)def->count * 8
                                                    options:MTLResourceStorageModePrivate];
        *handleOut = HandleOf(results);
        if (results == nil)
        {
            return mrhi_errorCapacity;
        }
        if (def->labelLength > 0)
        {
            results.label = mrhiMetalLabel(def->label, def->labelLength);
        }
    }
    return mrhi_success;
}

void mrhiMetalTextureMemory(id<MTLDevice> device, const mrhiTextureDef* def, uint64_t* bytesOut,
                            uint64_t* alignmentOut)
{
    @autoreleasepool
    {
        MTLSizeAndAlign needs = [device heapTextureSizeAndAlignWithDescriptor:Describe(def)];
        *bytesOut = IsMemoryless(device, def) ? 0 : needs.size;
        *alignmentOut = needs.align;
    }
}

void mrhiMetalBufferMemory(id<MTLDevice> device, const mrhiBufferDef* def, uint64_t* bytesOut,
                           uint64_t* alignmentOut)
{
    MTLSizeAndAlign needs = [device heapBufferSizeAndAlignWithLength:def->size
                                                             options:MTLResourceStorageModePrivate];
    *bytesOut = needs.size;
    *alignmentOut = needs.align;
}
