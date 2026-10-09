// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Passes (mrhi-0008): each declares every resource it uses, as an access
// or a target, checked against the resource and the pass's class. Within
// a pass a subresource is either only read or written by one use
// (WebGPU's usage scopes), and a declared resource is written before it
// is read.

#include "capabilities_core.h"
#include "device_core.h"
#include "heap_core.h"
#include "label.h"

#include <string.h>

#define PASS_DEF_COOKIE 0x6D727061u

mrhiPassDef mrhiDefaultPassDef(void)
{
    mrhiPassDef def = {0};
    def.cookie = PASS_DEF_COOKIE;
    def.passClass = mrhi_passGraphics;
    def.timestampBegin = MRHI_NO_QUERY;
    def.timestampEnd = MRHI_NO_QUERY;
    return def;
}

bool mrhiIsImported(const mrhiFrameResource* resource)
{
    return resource->kind == mrhiImportedTexture || resource->kind == mrhiImportedBuffer;
}

bool mrhiOutlivesFrame(const mrhiFrameResource* resource)
{
    return mrhiIsImported(resource) || resource->kind == mrhiSurfaceImage;
}

static bool IsBuffer(const mrhiFrameResource* resource)
{
    return resource->kind == mrhiFrameBuffer || resource->kind == mrhiImportedBuffer;
}

const mrhiTextureDef* mrhiFrameTextureOf(const mrhiFrameResource* resource)
{
    return &resource->texture;
}

void mrhiTrackedLayers(const mrhiFrameResource* resource, const mrhiFrameUse* use,
                       uint32_t* baseOut, uint32_t* countOut)
{
    bool volume = !IsBuffer(resource) && resource->texture.kind == mrhi_texture3d;
    *baseOut = volume ? 0 : use->baseLayer;
    *countOut = volume ? 1 : use->layerCount;
}

uint32_t mrhiFindFrameResource(const mrhiDevice* device, mrhiResourceId id)
{
    if (id.generation != device->frameSerial || id.index1 == 0 ||
        id.index1 > device->frameResourceCount)
    {
        return 0;
    }
    const mrhiFrameResource* resource = &device->frameResources[id.index1 - 1];
    bool live = true;
    if (resource->kind == mrhiImportedTexture)
    {
        live = mrhiPoolIsLive(&device->textures, resource->index1, resource->generation);
    }
    else if (resource->kind == mrhiImportedBuffer)
    {
        live = mrhiPoolIsLive(&device->buffers, resource->index1, resource->generation);
    }
    return live ? id.index1 : 0;
}

// The usage bit a use needs of a texture or buffer.
static uint32_t UsageOf(uint8_t use, bool buffer)
{
    switch (use)
    {
    case mrhi_accessSampled:
        return mrhi_textureSampled;
    case mrhi_accessUniform:
        return mrhi_bufferUniform;
    case mrhi_accessVertex:
        return mrhi_bufferVertex;
    case mrhi_accessIndex:
        return mrhi_bufferIndex;
    case mrhi_accessIndirect:
        return mrhi_bufferIndirect;
    case mrhi_accessStorageRead:
    case mrhi_accessStorageWrite:
    case mrhi_accessStorageReadWrite:
        return buffer ? mrhi_bufferStorage : mrhi_textureStorage;
    case mrhi_accessCopySource:
        return buffer ? mrhi_bufferCopySource : mrhi_textureCopySource;
    case mrhi_accessCopyDestination:
        return buffer ? mrhi_bufferCopyDestination : mrhi_textureCopyDestination;
    case mrhi_accessQueryResolve:
        return mrhi_bufferQueryResolve;
    default:
        return mrhi_textureRenderTarget;
    }
}

uint8_t mrhiFormatPlanes(mrhiFormat format)
{
    return mrhiFormatHasStencil(format) ? 3 : 1;
}

uint32_t mrhiUsageOf(const mrhiFrameResource* resource, uint8_t use)
{
    return UsageOf(use, IsBuffer(resource));
}

// Whether an imported object or a surface's image was made with the
// usage a use needs; a declared resource takes any.
static bool IsUsageMade(const mrhiDevice* device, const mrhiFrameResource* resource, uint8_t use)
{
    uint32_t usage = UsageOf(use, IsBuffer(resource));
    if (resource->kind == mrhiImportedTexture || resource->kind == mrhiSurfaceImage)
    {
        return (resource->texture.usage & usage) != 0;
    }
    if (resource->kind == mrhiImportedBuffer)
    {
        return (device->bufferSlots[resource->index1 - 1].usage & usage) != 0;
    }
    return true;
}

// Whether an access kind suits the resource type and the pass's class.
static bool IsKindAllowed(mrhiAccessKind kind, bool buffer, mrhiPassClass passClass)
{
    bool textureOnly = kind == mrhi_accessSampled;
    bool bufferOnly = kind == mrhi_accessUniform || kind == mrhi_accessVertex ||
                      kind == mrhi_accessIndex || kind == mrhi_accessIndirect ||
                      kind == mrhi_accessQueryResolve;
    bool copy = kind == mrhi_accessCopySource || kind == mrhi_accessCopyDestination;
    // Queries are resolved only in graphics passes (mrhi-0012).
    bool graphicsOnly =
        kind == mrhi_accessVertex || kind == mrhi_accessIndex || kind == mrhi_accessQueryResolve;
    bool classOk = passClass == mrhi_passGraphics ||
                   (passClass == mrhi_passAsyncCompute && !graphicsOnly) ||
                   (passClass == mrhi_passTransfer && copy);
    return classOk && !(buffer ? textureOnly : bufferOnly);
}

// The layers of a texture at a mip: its depth there for a 3D texture.
static uint32_t LayersAt(const mrhiTextureDef* def, uint32_t mip)
{
    if (def->kind != mrhi_texture3d)
    {
        return def->depthOrLayers;
    }
    uint32_t depth = def->depthOrLayers >> mip;
    return depth > 0 ? depth : 1;
}

// Whether an access kind is a read the sealed state allows: sampling a
// texture, or a buffer's reads other than copies.
static bool IsSealedRead(mrhiAccessKind kind, bool buffer)
{
    if (!buffer)
    {
        return kind == mrhi_accessSampled;
    }
    return kind == mrhi_accessUniform || kind == mrhi_accessVertex || kind == mrhi_accessIndex ||
           kind == mrhi_accessIndirect || kind == mrhi_accessStorageRead;
}

// Makes the use of an access: success, stale, or invalid (not counted).
static mrhiResult UseOfAccess(const mrhiDevice* device, const mrhiAccess* access,
                              mrhiPassClass passClass, mrhiFrameUse* useOut)
{
    uint32_t slot = mrhiFindFrameResource(device, access->resource);
    if (slot == 0)
    {
        return mrhi_errorStale;
    }
    const mrhiFrameResource* resource = &device->frameResources[slot - 1];
    mrhiAccessKind kind = access->kind;
    if (kind > mrhi_accessQueryResolve || !IsKindAllowed(kind, IsBuffer(resource), passClass) ||
        !IsUsageMade(device, resource, kind))
    {
        return mrhi_errorInvalid;
    }
    if (resource->sealed && !IsSealedRead(kind, IsBuffer(resource)))
    {
        return mrhi_errorInvalid;
    }
    bool overwrites = kind == mrhi_accessStorageWrite || kind == mrhi_accessCopyDestination ||
                      kind == mrhi_accessQueryResolve;
    // The states follow the kinds up to the targets' states.
    *useOut = (mrhiFrameUse){
        .resource = slot,
        .use = kind,
        .state = kind == mrhi_accessQueryResolve ? mrhi_stateQueryResolve
                                                 : (mrhiResourceState)(kind + 1),
        .planes = 1,
        .reads = !overwrites,
        .writes = overwrites || kind == mrhi_accessStorageReadWrite,
        .mipCount = 1,
        .layerCount = 1,
    };
    // A sealed resource stays in its state.
    if (resource->sealed)
    {
        useOut->state = mrhi_stateSealed;
    }
    if (IsBuffer(resource))
    {
        return mrhi_success;
    }
    const mrhiTextureDef* def = mrhiFrameTextureOf(resource);
    const mrhiTextureRange* range = &access->range;
    uint32_t layers = def->kind == mrhi_texture3d ? 1 : def->depthOrLayers;
    uint8_t planes = mrhiFormatPlanes(def->format);
    useOut->planes = range->aspect == mrhi_aspectDepthOnly     ? 1
                     : range->aspect == mrhi_aspectStencilOnly ? 2
                                                               : planes;
    useOut->baseMip = range->baseMip;
    useOut->mipCount = mrhiResolveCount(range->mipCount, range->baseMip, def->mipLevels);
    useOut->baseLayer = range->baseLayer;
    useOut->layerCount = mrhiResolveCount(range->layerCount, range->baseLayer, layers);
    bool valid = range->aspect <= mrhi_aspectStencilOnly &&
                 mrhiFormatHasAspect(def->format, range->aspect) &&
                 mrhiIsRangeValid(useOut->baseMip, useOut->mipCount, def->mipLevels) &&
                 mrhiIsRangeValid(useOut->baseLayer, useOut->layerCount, layers);
    return valid ? mrhi_success : mrhi_errorInvalid;
}

// The size and samples every target of a pass shares.
typedef struct TargetShape
{
    bool set;
    uint32_t width;
    uint32_t height;
    uint32_t samples;
} TargetShape;

// Whether a target's size at its mip and samples match the pass's other
// targets; the first sets them.
static bool Agrees(TargetShape* shape, const mrhiTextureDef* def, uint32_t mip)
{
    uint32_t width = def->width >> mip > 0 ? def->width >> mip : 1;
    uint32_t height = def->height >> mip > 0 ? def->height >> mip : 1;
    if (!shape->set)
    {
        *shape = (TargetShape){true, width, height, def->sampleCount};
        return true;
    }
    return shape->width == width && shape->height == height && shape->samples == def->sampleCount;
}

// The views a pass def renders: 1 for 0 (mrhi-0020).
static uint32_t ViewsOf(const mrhiPassDef* def)
{
    return def->viewCount > 1 ? def->viewCount : 1;
}

// Finds a texture a target uses at a mip from a layer, one layer per
// view, every view on a layer of a texture that is not 3D: its slot, or
// 0 with the refusal in statusOut.
static uint32_t FindTarget(const mrhiDevice* device, mrhiResourceId id, uint32_t mip,
                           uint32_t layer, uint32_t views, uint8_t use, mrhiResult* statusOut)
{
    uint32_t slot = mrhiFindFrameResource(device, id);
    if (slot == 0)
    {
        *statusOut = mrhi_errorStale;
        return 0;
    }
    const mrhiFrameResource* resource = &device->frameResources[slot - 1];
    *statusOut = mrhi_errorInvalid;
    if (IsBuffer(resource) || resource->sealed || !IsUsageMade(device, resource, use))
    {
        return 0;
    }
    const mrhiTextureDef* def = mrhiFrameTextureOf(resource);
    if (mip >= def->mipLevels || layer >= LayersAt(def, mip) ||
        views > LayersAt(def, mip) - layer || (views > 1 && def->kind == mrhi_texture3d))
    {
        return 0;
    }
    *statusOut = mrhi_success;
    return slot;
}

// Makes the uses of a color target and its resolve: how many, or 0 with
// the refusal in statusOut.
static uint32_t UsesOfColor(const mrhiDevice* device, const mrhiColorTarget* target, uint32_t views,
                            TargetShape* shape, mrhiFrameUse* usesOut, mrhiResult* statusOut)
{
    uint32_t slot = FindTarget(device, target->resource, target->mip, target->layer, views,
                               mrhiUseColorTarget, statusOut);
    if (slot == 0)
    {
        return 0;
    }
    const mrhiTextureDef* def = mrhiFrameTextureOf(&device->frameResources[slot - 1]);
    *statusOut = mrhi_errorInvalid;
    mrhiFormat format = mrhiViewFormatOf(def, target->viewFormat);
    if (target->load > mrhi_loadDiscard || target->store > mrhi_storeDiscard ||
        mrhiFormatHasDepth(def->format) || format == mrhi_formatNone ||
        !Agrees(shape, def, target->mip))
    {
        return 0;
    }
    usesOut[0] = (mrhiFrameUse){
        .resource = slot,
        .use = mrhiUseColorTarget,
        .state = mrhi_stateColorTarget,
        .planes = 1,
        .reads = target->load == mrhi_loadKeep,
        .writes = true,
        .baseMip = target->mip,
        .mipCount = 1,
        .baseLayer = target->layer,
        .layerCount = views,
    };
    *statusOut = mrhi_success;
    if (target->resolve.index1 == 0)
    {
        return 1;
    }
    uint32_t resolve = FindTarget(device, target->resolve, target->resolveMip, target->resolveLayer,
                                  views, mrhiUseResolve, statusOut);
    if (resolve == 0)
    {
        return 0;
    }
    const mrhiTextureDef* into = mrhiFrameTextureOf(&device->frameResources[resolve - 1]);
    TargetShape resolveShape = {0};
    Agrees(&resolveShape, into, target->resolveMip);
    // The resolve takes the target's own format, viewed in its view
    // format too.
    if (def->sampleCount == 1 || into->sampleCount != 1 || into->format != def->format ||
        mrhiViewFormatOf(into, format) == mrhi_formatNone || resolveShape.width != shape->width ||
        resolveShape.height != shape->height)
    {
        *statusOut = mrhi_errorInvalid;
        return 0;
    }
    usesOut[1] = (mrhiFrameUse){
        .resource = resolve,
        .use = mrhiUseResolve,
        .state = mrhi_stateResolve,
        .planes = 1,
        .writes = true,
        .baseMip = target->resolveMip,
        .mipCount = 1,
        .baseLayer = target->resolveLayer,
        .layerCount = views,
    };
    return 2;
}

// Makes the use of a depth target: success, or the refusal.
static mrhiResult UseOfDepth(const mrhiDevice* device, const mrhiDepthTarget* target,
                             uint32_t views, TargetShape* shape, mrhiFrameUse* useOut)
{
    mrhiResult status = mrhi_success;
    uint32_t slot = FindTarget(device, target->resource, target->mip, target->layer, views,
                               mrhiUseDepthTarget, &status);
    if (slot == 0)
    {
        return status;
    }
    const mrhiTextureDef* def = mrhiFrameTextureOf(&device->frameResources[slot - 1]);
    bool stencil = mrhiFormatHasStencil(def->format);
    bool known = target->depthLoad <= mrhi_loadDiscard && target->depthStore <= mrhi_storeDiscard &&
                 target->stencilLoad <= mrhi_loadDiscard &&
                 target->stencilStore <= mrhi_storeDiscard;
    bool keeps = target->depthLoad == mrhi_loadKeep && target->depthStore == mrhi_storeKeep &&
                 (!stencil ||
                  (target->stencilLoad == mrhi_loadKeep && target->stencilStore == mrhi_storeKeep));
    if (!known || !mrhiFormatHasDepth(def->format) || (target->readOnly && !keeps) ||
        !Agrees(shape, def, target->mip))
    {
        return mrhi_errorInvalid;
    }
    *useOut = (mrhiFrameUse){
        .resource = slot,
        .use = mrhiUseDepthTarget,
        .state = target->readOnly ? mrhi_stateDepthRead : mrhi_stateDepthTarget,
        .planes = mrhiFormatPlanes(def->format),
        .reads =
            target->depthLoad == mrhi_loadKeep || (stencil && target->stencilLoad == mrhi_loadKeep),
        .writes = !target->readOnly,
        .baseMip = target->mip,
        .mipCount = 1,
        .baseLayer = target->layer,
        .layerCount = views,
    };
    return mrhi_success;
}

// Whether two ranges of one start and count overlap.
static bool Overlaps(uint32_t baseA, uint32_t countA, uint32_t baseB, uint32_t countB)
{
    return baseA < baseB + countB && baseB < baseA + countA;
}

// Whether two uses of a pass meet on a subresource.
static bool Meet(const mrhiFrameUse* a, const mrhiFrameUse* b)
{
    return a->resource == b->resource && (a->planes & b->planes) != 0 &&
           Overlaps(a->baseMip, a->mipCount, b->baseMip, b->mipCount) &&
           Overlaps(a->baseLayer, a->layerCount, b->baseLayer, b->layerCount);
}

// Whether two uses of a pass conflict: where they meet, one writes, or a
// texture would need two states at once.
static bool Conflicts(const mrhiDevice* device, const mrhiFrameUse* a, const mrhiFrameUse* b)
{
    bool texture = !IsBuffer(&device->frameResources[a->resource - 1]);
    return Meet(a, b) && (a->writes || b->writes || (texture && a->state != b->state));
}

// Whether the pass's uses keep WebGPU's usage scopes, and read declared
// resources only after a pass wrote them.
static bool AreUsesValid(const mrhiDevice* device, const mrhiFrameUse* uses, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        const mrhiFrameResource* resource = &device->frameResources[uses[i].resource - 1];
        if (uses[i].reads && !mrhiIsImported(resource) && !resource->written)
        {
            return false;
        }
        for (uint32_t j = i + 1; j < count; ++j)
        {
            if (Conflicts(device, &uses[i], &uses[j]))
            {
                return false;
            }
        }
    }
    return true;
}

// Makes the uses of a pass at the end of the frame's use table: how many,
// or 0 with the refusal in statusOut.
static uint32_t MakeUses(const mrhiDevice* device, const mrhiPassDef* def, mrhiResult* statusOut)
{
    mrhiFrameUse* uses = &device->frameUses[device->frameUseCount];
    uint32_t count = 0;
    *statusOut = mrhi_success;
    for (uint32_t i = 0; i < def->accessCount && *statusOut == mrhi_success; ++i)
    {
        *statusOut = UseOfAccess(device, &def->accesses[i], def->passClass, &uses[count++]);
    }
    bool targets = def->colorTargetCount > 0 || def->depthTarget.resource.index1 != 0;
    if (*statusOut == mrhi_success && targets && def->passClass != mrhi_passGraphics)
    {
        *statusOut = mrhi_errorInvalid;
    }
    TargetShape shape = {0};
    for (uint32_t i = 0; i < def->colorTargetCount && *statusOut == mrhi_success; ++i)
    {
        count += UsesOfColor(device, &def->colorTargets[i], ViewsOf(def), &shape, &uses[count],
                             statusOut);
    }
    if (*statusOut == mrhi_success && def->depthTarget.resource.index1 != 0)
    {
        *statusOut = UseOfDepth(device, &def->depthTarget, ViewsOf(def), &shape, &uses[count]);
        // Sampling a read-only depth target shares its state, which allows
        // it.
        for (uint32_t i = 0; i < def->accessCount && *statusOut == mrhi_success; ++i)
        {
            if (uses[i].state == mrhi_stateSampled && uses[count].state == mrhi_stateDepthRead &&
                Meet(&uses[i], &uses[count]))
            {
                uses[i].state = mrhi_stateDepthRead;
            }
        }
        ++count;
    }
    if (*statusOut == mrhi_success && !AreUsesValid(device, uses, count))
    {
        *statusOut = mrhi_errorInvalid;
    }
    return *statusOut == mrhi_success ? count : 0;
}

// Checks what a def says before its uses: success, or the refusal.
// Checks a native pass's def (mrhi-0019): on a Vulkan, D3D12 or Metal device, without
// targets, queries or a heap, its accesses to imported resources only,
// and room for it in the frame.
static mrhiResult CheckNative(mrhiDevice* device, const mrhiPassDef* def)
{
    if (device->adapterInfo.driver != mrhi_driverVulkan &&
        device->adapterInfo.driver != mrhi_driverD3d12 &&
        device->adapterInfo.driver != mrhi_driverMetal)
    {
        return mrhi_errorUnsupported;
    }
    if (def->colorTargetCount > 0 || def->depthTarget.resource.index1 != 0 ||
        def->occlusionQuerySet.index1 != 0 || def->timestampQuerySet.index1 != 0 ||
        def->heap.index1 != 0)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNativePassDef);
    }
    for (uint32_t i = 0; i < def->accessCount; ++i)
    {
        uint32_t slot = mrhiFindFrameResource(device, def->accesses[i].resource);
        if (def->accesses[i].kind == mrhi_accessQueryResolve ||
            (slot != 0 && !mrhiIsImported(&device->frameResources[slot - 1])))
        {
            return mrhiDeviceMisuse(device, mrhi_diagnosticNativePassDef);
        }
    }
    uint32_t natives = 0;
    for (uint32_t i = 0; i < device->framePassCount; ++i)
    {
        natives += device->framePasses[i].native ? 1u : 0u;
    }
    return natives < MRHI_NATIVE_PASSES ? mrhi_success : mrhi_errorCapacity;
}

static mrhiResult CheckDef(mrhiDevice* device, const mrhiPassDef* def)
{
    mrhiResult status = mrhiCheckObjectDef(device, MRHI_DEF_HEAD(def), PASS_DEF_COOKIE);
    if (status != mrhi_success)
    {
        return status;
    }
    bool targets = def->colorTargetCount > 0 || def->depthTarget.resource.index1 != 0;
    if (def->passClass > mrhi_passTransfer)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticPassClass);
    }
    if (def->accesses == nullptr && def->accessCount > 0)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    if (def->colorTargetCount > MRHI_COLOR_TARGETS)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticPassColorTargets);
    }
    if (ViewsOf(def) > 1 && (def->native || !targets))
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticPassViews);
    }
    if (ViewsOf(def) > 1 &&
        (!device->features.multiview || ViewsOf(def) > device->limits.multiviewViews))
    {
        return mrhi_errorUnsupported;
    }
    if (!device->frameOpen || device->frameCompiled)
    {
        return mrhi_errorState;
    }
    // A pass has at most its accesses, two uses per color target and its
    // depth target.
    uint64_t most = (uint64_t)def->accessCount + 2u * def->colorTargetCount + 1u;
    if (device->framePassCount == device->deviceLimits.framePasses ||
        most > device->deviceLimits.frameAccesses - device->frameUseCount)
    {
        return mrhi_errorCapacity;
    }
    return def->native ? CheckNative(device, def) : mrhi_success;
}

// The layout and size of a render pass's targets, from their textures;
// nothing for a pass without targets.
static void MeasureTargets(const mrhiDevice* device, mrhiFramePass* pass)
{
    pass->layout.views = pass->viewCount;
    for (uint32_t i = 0; i < pass->colorTargetCount; ++i)
    {
        const mrhiColorTarget* target = &pass->colorTargets[i];
        const mrhiTextureDef* texture =
            mrhiFrameTextureOf(&device->frameResources[target->resource.index1 - 1]);
        pass->layout.colors[i] = target->viewFormat;
        pass->layout.samples = texture->sampleCount;
        pass->width = texture->width >> target->mip > 0 ? texture->width >> target->mip : 1;
        pass->height = texture->height >> target->mip > 0 ? texture->height >> target->mip : 1;
    }
    const mrhiDepthTarget* depth = &pass->depthTarget;
    if (depth->resource.index1 != 0)
    {
        const mrhiTextureDef* texture =
            mrhiFrameTextureOf(&device->frameResources[depth->resource.index1 - 1]);
        pass->layout.depth = texture->format;
        pass->layout.samples = texture->sampleCount;
        pass->width = texture->width >> depth->mip > 0 ? texture->width >> depth->mip : 1;
        pass->height = texture->height >> depth->mip > 0 ? texture->height >> depth->mip : 1;
    }
}

mrhiResult mrhiAddPass(mrhiDevice* device, const mrhiPassDef* def, mrhiPassId* passOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (def == nullptr || passOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    mrhiResult status = CheckDef(device, def);
    if (status != mrhi_success)
    {
        return status;
    }
    uint64_t heap = 0;
    status = mrhiCheckPassHeap(device, def->heap, &heap);
    if (status == mrhi_success)
    {
        status = mrhiCheckPassQueries(device, def);
        if (status == mrhi_errorInvalid)
        {
            return mrhiDeviceMisuse(device, mrhi_diagnosticPassQueries);
        }
    }
    uint32_t count = status == mrhi_success ? MakeUses(device, def, &status) : 0;
    if (status != mrhi_success)
    {
        return status == mrhi_errorInvalid ? mrhiDeviceMisuse(device, mrhi_diagnosticPassUses)
                                           : status;
    }
    const mrhiFrameUse* uses = &device->frameUses[device->frameUseCount];
    for (uint32_t i = 0; i < count; ++i)
    {
        device->frameResources[uses[i].resource - 1].written |= uses[i].writes;
    }
    mrhiFramePass* pass = &device->framePasses[device->framePassCount++];
    *pass = (mrhiFramePass){
        .passClass = def->passClass,
        .neverCull = def->neverCull,
        .native = def->native,
        .firstUse = device->frameUseCount,
        .useCount = count,
        .colorTargetCount = def->colorTargetCount,
        .depthTarget = def->depthTarget,
        .viewCount = ViewsOf(def),
        .occlusionSet = def->occlusionQuerySet.index1,
        .occlusionGeneration = def->occlusionQuerySet.generation,
        .heap = heap,
        .labelLength = MAUL_RHI_LABELS ? (uint32_t)def->labelLength : 0,
    };
    if (def->occlusionQuerySet.index1 != 0)
    {
        pass->occlusionHandle = device->querySetSlots[def->occlusionQuerySet.index1 - 1].handle;
    }
    if (pass->labelLength > 0)
    {
        memcpy(&device->frameLabels[(size_t)(device->framePassCount - 1) * MRHI_LABEL_BYTES],
               def->label, def->labelLength);
    }
    mrhiMarkPassTimestamps(device, def, pass);
    for (uint32_t i = 0; i < def->colorTargetCount; ++i)
    {
        pass->colorTargets[i] = def->colorTargets[i];
        // Drivers see the format a target renders in, never none.
        if (def->colorTargets[i].resource.index1 != 0)
        {
            const mrhiTextureDef* texture = mrhiFrameTextureOf(
                &device->frameResources[def->colorTargets[i].resource.index1 - 1]);
            pass->colorTargets[i].viewFormat =
                mrhiViewFormatOf(texture, def->colorTargets[i].viewFormat);
        }
    }
    MeasureTargets(device, pass);
    device->frameUseCount += count;
    *passOut = (mrhiPassId){device->framePassCount, device->frameSerial};
    return mrhi_success;
}
