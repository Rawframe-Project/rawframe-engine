// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The open frame's resources (mrhi-0008): textures and buffers the graph
// makes, whose usages its passes will decide, device textures and
// buffers imported once per frame, and images acquired from surfaces.
// Their ids carry the frame's serial, so they end with it.

#include "device_core.h"
#include "instance_core.h"

// Adds a resource to the open frame: success with its id, or the refusal.
static mrhiResult Add(mrhiDevice* device, mrhiFrameResource resource, mrhiResourceId* resourceOut)
{
    if (!device->frameOpen || device->frameCompiled)
    {
        return mrhi_errorState;
    }
    if (device->frameResourceCount == device->deviceLimits.frameResources)
    {
        return mrhi_errorCapacity;
    }
    device->frameResources[device->frameResourceCount++] = resource;
    *resourceOut = (mrhiResourceId){device->frameResourceCount, device->frameSerial};
    return mrhi_success;
}

mrhiResult mrhiDeclareTexture(mrhiDevice* device, const mrhiTextureDef* def,
                              mrhiResourceId* resourceOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (def == nullptr || resourceOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    mrhiResult status = mrhiCheckTextureShape(device, def, false);
    if (status != mrhi_success)
    {
        return status;
    }
    if (def->usage != 0)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticTransientUsage);
    }
    mrhiFrameResource resource = {.kind = mrhiFrameTexture, .texture = *def};
    resource.texture.next = nullptr;
    resource.texture.label = nullptr;
    resource.texture.labelLength = 0;
    return Add(device, resource, resourceOut);
}

mrhiResult mrhiDeclareBuffer(mrhiDevice* device, const mrhiBufferDef* def,
                             mrhiResourceId* resourceOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (def == nullptr || resourceOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    mrhiResult status = mrhiCheckBufferShape(device, def);
    if (status != mrhi_success)
    {
        return status;
    }
    if (def->usage != 0)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticTransientUsage);
    }
    return Add(device, (mrhiFrameResource){.kind = mrhiFrameBuffer, .size = def->size},
               resourceOut);
}

// Imports a live device object once per frame: success with the frame's
// id for it, or the refusal.
static mrhiResult Import(mrhiDevice* device, mrhiImport* import, mrhiFrameResource resource,
                         mrhiResourceId* resourceOut)
{
    if (device->frameOpen && import->frame == device->frameSerial)
    {
        *resourceOut = (mrhiResourceId){import->resource, device->frameSerial};
        return mrhi_success;
    }
    mrhiResult status = Add(device, resource, resourceOut);
    if (status == mrhi_success)
    {
        *import = (mrhiImport){device->frameSerial, resourceOut->index1};
    }
    return status;
}

mrhiResult mrhiImportTexture(mrhiDevice* device, mrhiTextureId texture, mrhiResourceId* resourceOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (resourceOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    if (!mrhiPoolIsLive(&device->textures, texture.index1, texture.generation))
    {
        return mrhi_errorStale;
    }
    const mrhiTextureSlot* slot = &device->textureSlots[texture.index1 - 1];
    mrhiFrameResource resource = {
        .kind = mrhiImportedTexture,
        .texture = slot->def,
        .handle = slot->handle,
        .index1 = texture.index1,
        .generation = texture.generation,
        .initialState = slot->state,
        .sealed = slot->state == mrhi_stateSealed,
        .resting = slot->resting,
    };
    return Import(device, &device->textureSlots[texture.index1 - 1].import, resource, resourceOut);
}

mrhiResult mrhiImportBuffer(mrhiDevice* device, mrhiBufferId buffer, mrhiResourceId* resourceOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (resourceOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    if (!mrhiPoolIsLive(&device->buffers, buffer.index1, buffer.generation))
    {
        return mrhi_errorStale;
    }
    const mrhiBufferSlot* slot = &device->bufferSlots[buffer.index1 - 1];
    mrhiFrameResource resource = {
        .kind = mrhiImportedBuffer,
        .size = slot->size,
        .handle = slot->handle,
        .index1 = buffer.index1,
        .generation = buffer.generation,
        .initialState = slot->state,
        .sealed = slot->state == mrhi_stateSealed,
    };
    return Import(device, &device->bufferSlots[buffer.index1 - 1].import, resource, resourceOut);
}

// Finds an imported object of the open frame to seal or unseal: its
// resource, or NULL with the refusal in statusOut.
static mrhiFrameResource* FindSealable(mrhiDevice* device, mrhiResourceId id, mrhiResult* statusOut)
{
    *statusOut = mrhi_errorUnsupported;
    if (!device->features.bindlessSampling)
    {
        return nullptr;
    }
    *statusOut = mrhi_errorState;
    if (!device->frameOpen || device->frameCompiled)
    {
        return nullptr;
    }
    *statusOut = mrhi_errorStale;
    uint32_t slot = mrhiFindFrameResource(device, id);
    if (slot == 0)
    {
        return nullptr;
    }
    mrhiFrameResource* resource = &device->frameResources[slot - 1];
    *statusOut = mrhiIsImported(resource)
                     ? mrhi_success
                     : mrhiDeviceMisuse(device, mrhi_diagnosticSealNotImported);
    return *statusOut == mrhi_success ? resource : nullptr;
}

mrhiResult mrhiSealResource(mrhiDevice* device, mrhiResourceId resource)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = mrhi_success;
    mrhiFrameResource* found = FindSealable(device, resource, &status);
    if (found == nullptr)
    {
        return status;
    }
    // A sealed texture is sampled, and an adopted image rests as a target.
    if (found->kind == mrhiImportedTexture && ((found->texture.usage & mrhi_textureSampled) == 0 ||
                                               found->resting != mrhi_stateUndefined))
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticSealTexture);
    }
    found->seal = true;
    return mrhi_success;
}

mrhiResult mrhiUnsealResource(mrhiDevice* device, mrhiResourceId resource)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = mrhi_success;
    mrhiFrameResource* found = FindSealable(device, resource, &status);
    if (found == nullptr)
    {
        return status;
    }
    found->sealed = false;
    found->seal = false;
    return mrhi_success;
}

// The texture a surface's images are: its configured format, size,
// usages and view formats, with one mip and one layer.
static mrhiTextureDef ImageDef(const mrhiSurfaceConfig* config)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = config->color.format;
    def.width = config->width;
    def.height = config->height;
    def.usage = config->usage;
    for (uint32_t i = 0; i < MRHI_VIEW_FORMATS; ++i)
    {
        def.viewFormats[i] = config->viewFormats[i];
    }
    return def;
}

mrhiResult mrhiAcquireSurfaceImage(mrhiDevice* device, mrhiSurfaceId surface,
                                   mrhiResourceId* imageOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (imageOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    *imageOut = (mrhiResourceId){0};
    if (mrhiFindSurface(device->instance, surface) == 0)
    {
        return mrhi_errorStale;
    }
    const mrhiSurfaceSlot* found = &device->instance->surfaceSlots[surface.index1 - 1];
    if (found->device != device || !device->frameOpen || device->frameCompiled)
    {
        return mrhi_errorState;
    }
    if (device->state == mrhi_deviceLost)
    {
        return mrhi_errorDeviceLost;
    }
    mrhiSwapchainSlot* swapchain = &device->swapchainSlots[found->swapchain - 1];
    if (swapchain->acquired.frame == device->frameSerial)
    {
        uint32_t held = swapchain->acquired.resource;
        *imageOut = held == 0 ? (mrhiResourceId){0} : (mrhiResourceId){held, device->frameSerial};
        return swapchain->acquireOutcome;
    }
    // Room first, so that an image is never acquired and then lost.
    if (device->frameResourceCount == device->deviceLimits.frameResources)
    {
        return mrhi_errorCapacity;
    }
    uint64_t image = 0;
    mrhiResult outcome =
        mrhiDriverStatus(device, device->driver.vtable->acquireImage(device->driver.self,
                                                                     swapchain->handle, &image));
    if (outcome == mrhi_success || outcome == mrhi_suboptimal)
    {
        mrhiFrameResource resource = {
            .kind = mrhiSurfaceImage,
            .texture = ImageDef(&swapchain->config),
            .handle = swapchain->handle,
            .image = image,
        };
        // The frame is open, not compiled, and has room: checked above.
        (void)Add(device, resource, imageOut);
    }
    swapchain->acquired = (mrhiImport){device->frameSerial, imageOut->index1};
    swapchain->acquireOutcome = outcome;
    return outcome;
}

void mrhiReleaseImages(mrhiDevice* device)
{
    for (uint32_t i = 0; i < device->frameResourceCount; ++i)
    {
        const mrhiFrameResource* resource = &device->frameResources[i];
        if (resource->kind == mrhiSurfaceImage)
        {
            device->driver.vtable->releaseImage(device->driver.self, resource->handle,
                                                resource->image);
        }
    }
}
