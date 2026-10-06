// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Passes on Vulkan (vulkan_pass.h): dynamic rendering over views of
// the pass's targets made for the frame, with the core's derived loads
// and stores; binding tables written into descriptor sets of the
// pipeline's layouts, from pools each frame slot grows as it needs and
// resets when it is reused; and the viewport flipped to +Y up.

#include "vulkan_pass.h"

#include "capabilities_core.h"
#include "invariant.h"
#include "vulkan_adapter.h"

#include <string.h>

// The contract's load and store operations are Vulkan's.
static_assert((int)mrhi_loadKeep == (int)VK_ATTACHMENT_LOAD_OP_LOAD &&
                  (int)mrhi_loadClear == (int)VK_ATTACHMENT_LOAD_OP_CLEAR &&
                  (int)mrhi_loadDiscard == (int)VK_ATTACHMENT_LOAD_OP_DONT_CARE &&
                  (int)mrhi_storeKeep == (int)VK_ATTACHMENT_STORE_OP_STORE &&
                  (int)mrhi_storeDiscard == (int)VK_ATTACHMENT_STORE_OP_DONT_CARE,
              "the same operations");

static const mrhiDriverResource* Find(const mrhiDriverFrame* frame, uint32_t index1)
{
    MRHI_ASSERT(index1 != 0 && index1 <= frame->resourceCount);
    return &frame->resources[index1 - 1];
}

mrhiVulkanFrameTexture mrhiVulkanFrameImage(const mrhiVulkanRecording* recording, uint32_t index1)
{
    const mrhiDriverResource* resource = Find(recording->frame, index1);
    VkImage image = resource->kind == mrhiDriverDeviceTexture
                        ? recording->frames->objects->textures[resource->handle - 1].image
                    : resource->kind == mrhiDriverSurfaceImage
                        ? mrhiVulkanSwapchainImage(recording->frames->swapchains, resource->handle,
                                                   resource->image)
                        : recording->slot->images[index1 - 1];
    return (mrhiVulkanFrameTexture){.image = image, .def = resource->texture};
}

VkBuffer mrhiVulkanFrameBuffer(const mrhiVulkanRecording* recording, uint32_t index1)
{
    const mrhiDriverResource* resource = Find(recording->frame, index1);
    return resource->kind == mrhiDriverDeviceBuffer
               ? recording->frames->objects->buffers[resource->handle - 1].buffer
               : recording->slot->buffers[index1 - 1];
}

VkImageAspectFlags mrhiVulkanAspect(mrhiTextureAspect aspect, mrhiFormat format)
{
    if (aspect == mrhi_aspectDepthOnly)
    {
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    if (aspect == mrhi_aspectStencilOnly)
    {
        return VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    VkImageAspectFlags flags = 0;
    flags |= mrhiFormatHasDepth(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0;
    flags |= mrhiFormatHasStencil(format) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0;
    return flags != 0 ? flags : VK_IMAGE_ASPECT_COLOR_BIT;
}

// What a view is: its type, format, aspects and part, and the one use
// it has, so that a format the image's other usages refuse still views.
typedef struct ViewShape
{
    VkImageViewType type;
    mrhiFormat format;
    VkImageAspectFlags aspects;
    uint32_t mip;
    uint32_t mips;
    uint32_t layer;
    uint32_t layers;
    VkImageUsageFlags usage;
} ViewShape;

// Makes a view the slot destroys with the frame; VK_NULL_HANDLE, with
// the recording failed, when the device refuses.
static VkImageView MakeView(mrhiVulkanRecording* recording, VkImage image, const ViewShape* shape)
{
    mrhiVulkanSlot* slot = recording->slot;
    MRHI_ASSERT(slot->viewCount < recording->frames->viewLimit);
    const VkImageViewUsageCreateInfo usage = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
        .usage = shape->usage,
    };
    const VkImageViewCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = &usage,
        .image = image,
        .viewType = shape->type,
        .format = mrhiVulkanFormat(shape->format, recording->frames->depthStencil),
        .subresourceRange = {shape->aspects, shape->mip, shape->mips, shape->layer, shape->layers},
    };
    VkImageView view = VK_NULL_HANDLE;
    VkResult result =
        recording->frames->api->vkCreateImageView(recording->frames->device, &info, nullptr, &view);
    if (result != VK_SUCCESS)
    {
        recording->status = mrhiVulkanStatus(result);
        return VK_NULL_HANDLE;
    }
    slot->views[slot->viewCount++] = view;
    return view;
}

// A view of one mip and layer (a 3D texture's depth slice) of a target;
// in a multiview pass, of a layer per view from it (mrhi-0020).
static VkImageView TargetView(mrhiVulkanRecording* recording, uint32_t index1, uint32_t mip,
                              uint32_t layer, VkImageUsageFlags use)
{
    mrhiVulkanFrameTexture texture = mrhiVulkanFrameImage(recording, index1);
    uint32_t views = recording->pass->viewCount;
    const ViewShape shape = {
        .type = views > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
        .format = texture.def->format,
        .aspects = mrhiVulkanAspect(mrhi_aspectAll, texture.def->format),
        .mip = mip,
        .mips = 1,
        .layer = layer,
        .layers = views,
        .usage = use,
    };
    return MakeView(recording, texture.image, &shape);
}

static VkClearColorValue ClearOf(const mrhiClearColor* clear, mrhiFormat format)
{
    VkClearColorValue value;
    switch (mrhiGetFormatTarget(format).scalar)
    {
    case mrhi_scalarSint32:
        value.int32[0] = (int32_t)clear->red;
        value.int32[1] = (int32_t)clear->green;
        value.int32[2] = (int32_t)clear->blue;
        value.int32[3] = (int32_t)clear->alpha;
        break;
    case mrhi_scalarUint32:
        value.uint32[0] = (uint32_t)clear->red;
        value.uint32[1] = (uint32_t)clear->green;
        value.uint32[2] = (uint32_t)clear->blue;
        value.uint32[3] = (uint32_t)clear->alpha;
        break;
    default:
        value.float32[0] = clear->red;
        value.float32[1] = clear->green;
        value.float32[2] = clear->blue;
        value.float32[3] = clear->alpha;
        break;
    }
    return value;
}

static VkRenderingAttachmentInfo ColorOf(mrhiVulkanRecording* recording, uint32_t i)
{
    const mrhiDriverPass* pass = recording->pass;
    const mrhiColorTarget* target = &pass->colorTargets[i];
    VkRenderingAttachmentInfo color = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (target->resource.index1 == 0)
    {
        return color;
    }
    mrhiFormat format = mrhiVulkanFrameImage(recording, target->resource.index1).def->format;
    color.imageView = TargetView(recording, target->resource.index1, target->mip, target->layer,
                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = (VkAttachmentLoadOp)target->load;
    color.storeOp = (VkAttachmentStoreOp)pass->colorStores[i];
    color.clearValue.color = ClearOf(&target->clear, format);
    if (target->resolve.index1 != 0)
    {
        color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        color.resolveImageView =
            TargetView(recording, target->resolve.index1, target->resolveMip, target->resolveLayer,
                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    return color;
}

void mrhiVulkanSetViewport(const mrhiVulkanRecording* recording, float x, float y, float width,
                           float height, float minDepth, float maxDepth)
{
    const VkViewport viewport = {
        .x = x,
        .y = y + height,
        .width = width,
        .height = -height,
        .minDepth = minDepth,
        .maxDepth = maxDepth,
    };
    recording->frames->api->vkCmdSetViewport(recording->slot->commands, 0, 1, &viewport);
}

// The depth and stencil attachments of a pass's depth target.
static void DepthOf(mrhiVulkanRecording* recording, VkRenderingAttachmentInfo* depthOut,
                    VkRenderingAttachmentInfo* stencilOut, bool* hasDepth, bool* hasStencil)
{
    const mrhiDriverPass* pass = recording->pass;
    const mrhiDepthTarget* target = &pass->depthTarget;
    mrhiFormat format = mrhiVulkanFrameImage(recording, target->resource.index1).def->format;
    VkImageView view = TargetView(recording, target->resource.index1, target->mip, target->layer,
                                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
    VkImageLayout layout = target->readOnly ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                            : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    *depthOut = (VkRenderingAttachmentInfo){
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = view,
        .imageLayout = layout,
        .loadOp = (VkAttachmentLoadOp)target->depthLoad,
        .storeOp = (VkAttachmentStoreOp)pass->depthStore,
        .clearValue.depthStencil = {.depth = target->clearDepth},
    };
    *stencilOut = *depthOut;
    stencilOut->loadOp = (VkAttachmentLoadOp)target->stencilLoad;
    stencilOut->storeOp = (VkAttachmentStoreOp)pass->stencilStore;
    stencilOut->clearValue.depthStencil =
        (VkClearDepthStencilValue){.stencil = target->clearStencil};
    *hasDepth = mrhiFormatHasDepth(format);
    *hasStencil = mrhiFormatHasStencil(format);
}

static bool HasTargets(const mrhiDriverPass* pass)
{
    return pass->colorTargetCount > 0 || pass->depthTarget.resource.index1 != 0;
}

void mrhiVulkanBeginPass(mrhiVulkanRecording* recording)
{
    const mrhiDriverPass* pass = recording->pass;
    recording->pipeline = nullptr;
    if (!HasTargets(pass))
    {
        return;
    }
    VkRenderingAttachmentInfo colors[MRHI_COLOR_TARGETS];
    for (uint32_t i = 0; i < pass->colorTargetCount; ++i)
    {
        colors[i] = ColorOf(recording, i);
    }
    VkRenderingAttachmentInfo depth = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    VkRenderingAttachmentInfo stencil = depth;
    bool hasDepth = false;
    bool hasStencil = false;
    if (pass->depthTarget.resource.index1 != 0)
    {
        DepthOf(recording, &depth, &stencil, &hasDepth, &hasStencil);
    }
    const VkRenderingInfo info = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.extent = {pass->width, pass->height}},
        .layerCount = 1,
        // A view per layer from the targets' (mrhi-0020); Vulkan ignores
        // the layer count then.
        .viewMask = pass->viewCount > 1 ? (1u << pass->viewCount) - 1 : 0,
        .colorAttachmentCount = pass->colorTargetCount,
        .pColorAttachments = colors,
        .pDepthAttachment = hasDepth ? &depth : nullptr,
        .pStencilAttachment = hasStencil ? &stencil : nullptr,
    };
    VkCommandBuffer commands = recording->slot->commands;
    const mrhiVulkanDevice* api = recording->frames->api;
    api->vkCmdBeginRendering(commands, &info);
    mrhiVulkanSetViewport(recording, 0.0f, 0.0f, (float)pass->width, (float)pass->height, 0.0f,
                          1.0f);
    const VkRect2D scissor = {.extent = {pass->width, pass->height}};
    api->vkCmdSetScissor(commands, 0, 1, &scissor);
    const float constants[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    api->vkCmdSetBlendConstants(commands, constants);
    api->vkCmdSetStencilReference(commands, VK_STENCIL_FACE_FRONT_AND_BACK, 0);
}

void mrhiVulkanEndPass(mrhiVulkanRecording* recording)
{
    if (HasTargets(recording->pass))
    {
        recording->frames->api->vkCmdEndRendering(recording->slot->commands);
    }
}

// Makes the slot's next descriptor pool.
static VkResult AddPool(mrhiVulkanRecording* recording)
{
    mrhiVulkanSlot* slot = recording->slot;
    if (slot->poolCount == MRHI_VULKAN_POOLS)
    {
        return VK_ERROR_OUT_OF_POOL_MEMORY;
    }
    static const VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, MRHI_VULKAN_POOL_DESCRIPTORS},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MRHI_VULKAN_POOL_DESCRIPTORS},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, MRHI_VULKAN_POOL_DESCRIPTORS},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MRHI_VULKAN_POOL_DESCRIPTORS},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MRHI_VULKAN_POOL_DESCRIPTORS},
    };
    const VkDescriptorPoolCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = MRHI_VULKAN_POOL_SETS,
        .poolSizeCount = sizeof(sizes) / sizeof(sizes[0]),
        .pPoolSizes = sizes,
    };
    VkResult result = recording->frames->api->vkCreateDescriptorPool(
        recording->frames->device, &info, nullptr, &slot->pools[slot->poolCount]);
    slot->poolCount += result == VK_SUCCESS ? 1 : 0;
    return result;
}

// A new set of a layout from the slot's pools, moving to the next pool
// when one is full.
static VkDescriptorSet TakeSet(mrhiVulkanRecording* recording, VkDescriptorSetLayout layout)
{
    mrhiVulkanSlot* slot = recording->slot;
    for (;;)
    {
        VkResult result = slot->poolInUse < slot->poolCount ? VK_SUCCESS : AddPool(recording);
        VkDescriptorSet set = VK_NULL_HANDLE;
        if (result == VK_SUCCESS)
        {
            const VkDescriptorSetAllocateInfo info = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = slot->pools[slot->poolInUse],
                .descriptorSetCount = 1,
                .pSetLayouts = &layout,
            };
            result = recording->frames->api->vkAllocateDescriptorSets(recording->frames->device,
                                                                      &info, &set);
        }
        if (result == VK_SUCCESS)
        {
            return set;
        }
        bool full = result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL;
        if (!full || slot->poolInUse == MRHI_VULKAN_POOLS - 1)
        {
            recording->status = full ? mrhi_errorCapacity : mrhiVulkanStatus(result);
            return VK_NULL_HANDLE;
        }
        ++slot->poolInUse;
    }
}

static VkImageViewType ViewTypeOf(mrhiTextureKind kind)
{
    static const VkImageViewType types[] = {
        [mrhi_texture2d] = VK_IMAGE_VIEW_TYPE_2D,
        [mrhi_texture2dArray] = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
        [mrhi_textureCube] = VK_IMAGE_VIEW_TYPE_CUBE,
        [mrhi_textureCubeArray] = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY,
        [mrhi_texture3d] = VK_IMAGE_VIEW_TYPE_3D,
    };
    MRHI_ASSERT(kind <= mrhi_texture3d);
    return types[kind];
}

// The layout a sampled texture is in: read-only depth when the pass
// also tests against it.
static VkImageLayout SampledLayout(const mrhiVulkanRecording* recording, uint32_t object)
{
    const mrhiDepthTarget* depth = &recording->pass->depthTarget;
    return depth->resource.index1 == object && depth->readOnly
               ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
               : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

// Fills one descriptor write from a binding record.
static void WriteOf(mrhiVulkanRecording* recording, const mrhiCommandBinding* binding,
                    VkWriteDescriptorSet* write, VkDescriptorBufferInfo* buffer,
                    VkDescriptorImageInfo* image)
{
    write->dstBinding = binding->slot;
    write->descriptorCount = 1;
    write->pBufferInfo = buffer;
    write->pImageInfo = image;
    switch (binding->kind)
    {
    case mrhi_bindingUniformBuffer:
    case mrhi_bindingStorageBuffer:
    case mrhi_bindingReadOnlyStorageBuffer:
        write->descriptorType = binding->kind == mrhi_bindingUniformBuffer
                                    ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                    : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        *buffer = (VkDescriptorBufferInfo){mrhiVulkanFrameBuffer(recording, binding->object),
                                           binding->offset, binding->size};
        return;
    case mrhi_bindingSampler:
        write->descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        *image = (VkDescriptorImageInfo){
            .sampler = recording->frames->objects->samplers[binding->offset - 1]};
        return;
    default:
        break;
    }
    bool storage = binding->kind == mrhi_bindingStorageTexture;
    mrhiVulkanFrameTexture texture = mrhiVulkanFrameImage(recording, binding->object);
    const ViewShape shape = {
        .type = ViewTypeOf(binding->viewKind),
        .format = binding->viewFormat,
        .aspects = mrhiVulkanAspect(binding->aspect, binding->viewFormat),
        .mip = binding->baseMip,
        .mips = binding->mipCount,
        .layer = (uint32_t)binding->offset,
        .layers = (uint32_t)binding->size,
        .usage = storage ? VK_IMAGE_USAGE_STORAGE_BIT : VK_IMAGE_USAGE_SAMPLED_BIT,
    };
    write->descriptorType =
        storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    *image = (VkDescriptorImageInfo){
        .imageView = MakeView(recording, texture.image, &shape),
        .imageLayout =
            storage ? VK_IMAGE_LAYOUT_GENERAL : SampledLayout(recording, binding->object),
    };
}

void mrhiVulkanBindTable(mrhiVulkanRecording* recording, const mrhiCommand* command)
{
    const mrhiVulkanPipeline* pipeline = recording->pipeline;
    MRHI_ASSERT(pipeline != nullptr && command->a < pipeline->setCount);
    VkDescriptorSet set = TakeSet(recording, pipeline->sets[command->a]);
    if (set == VK_NULL_HANDLE)
    {
        return;
    }
    VkWriteDescriptorSet writes[MRHI_TABLE_BINDINGS];
    VkDescriptorBufferInfo buffers[MRHI_TABLE_BINDINGS];
    VkDescriptorImageInfo images[MRHI_TABLE_BINDINGS];
    uint32_t count = (uint32_t)command->b;
    MRHI_ASSERT(count <= MRHI_TABLE_BINDINGS);
    for (uint32_t i = 0; i < count; ++i)
    {
        mrhiCommandBinding binding;
        memcpy(&binding, &command[1 + i], sizeof(binding));
        writes[i] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
        };
        WriteOf(recording, &binding, &writes[i], &buffers[i], &images[i]);
    }
    const mrhiVulkanDevice* api = recording->frames->api;
    api->vkUpdateDescriptorSets(recording->frames->device, count, writes, 0, nullptr);
    api->vkCmdBindDescriptorSets(recording->slot->commands, pipeline->bindPoint, pipeline->layout,
                                 command->a, 1, &set, 0, nullptr);
}
