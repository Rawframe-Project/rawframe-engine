// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Records a submitted frame on Vulkan (vulkan_frame.h): the core's
// barriers, each state standing for a stage, an access and a layout,
// then each kept pass's commands: pipelines, binding tables, the root
// block, dynamic state, draws and dispatches in passes (vulkan_pass.c),
// queries (vulkan_query.c), copies, uploads and readbacks, and each
// pass's label and debug groups and markers (vulkan_label.c).

#include "capabilities_core.h"
#include "invariant.h"
#include "vulkan_adapter.h"
#include "vulkan_heap.h"
#include "vulkan_label.h"
#include "vulkan_pass.h"
#include "vulkan_query.h"

#include "maul-rhi/encoder.h"

#include <string.h>

// The barriers one vkCmdPipelineBarrier2 takes at most.
#define BATCH 32

// Every shader stage a state may name.
#define SHADERS                                                                                    \
    (VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |             \
     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)

// What a resource state is to Vulkan.
typedef struct Use
{
    VkPipelineStageFlags2 stages;
    VkAccessFlags2 access;
    VkImageLayout layout;
} Use;

static const Use s_uses[] = {
    [mrhi_stateUndefined] = {VK_PIPELINE_STAGE_2_NONE, 0, VK_IMAGE_LAYOUT_UNDEFINED},
    [mrhi_stateSampled] = {SHADERS, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
    [mrhi_stateUniform] = {SHADERS, VK_ACCESS_2_UNIFORM_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED},
    [mrhi_stateVertex] = {VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT,
                          VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED},
    [mrhi_stateIndex] = {VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT,
                         VK_IMAGE_LAYOUT_UNDEFINED},
    [mrhi_stateIndirect] = {VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                            VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED},
    [mrhi_stateStorageRead] = {SHADERS, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                               VK_IMAGE_LAYOUT_GENERAL},
    [mrhi_stateStorageWrite] = {SHADERS, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                VK_IMAGE_LAYOUT_GENERAL},
    [mrhi_stateStorageReadWrite] = {SHADERS,
                                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                    VK_IMAGE_LAYOUT_GENERAL},
    [mrhi_stateCopySource] = {VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL},
    [mrhi_stateCopyDestination] = {VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL},
    [mrhi_stateColorTarget] = {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                               VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
    [mrhi_stateResolve] = {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                           VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
    [mrhi_stateDepthTarget] = {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                   VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                               VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                   VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
    [mrhi_stateDepthRead] = {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                 VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT | SHADERS,
                             VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                             VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
    [mrhi_stateQueryResolve] = {VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT,
                                VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED},
    // The present semaphore is signalled after all commands, which orders
    // the transition to presenting before it.
    [mrhi_statePresent] = {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0,
                           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR},
    // A sealed texture is sampled by every shader stage.
    [mrhi_stateSealed] = {SHADERS, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
};

static_assert(sizeof s_uses / sizeof s_uses[0] == mrhi_stateSealed + 1, "a use per state");

// A sealed buffer is read every way a buffer is read.
static const Use s_sealedBuffer = {
    SHADERS | VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT |
        VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
    VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
        VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT |
        VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
    VK_IMAGE_LAYOUT_UNDEFINED,
};

// What a state is to Vulkan for a buffer.
static const Use* BufferUse(mrhiResourceState state)
{
    return state == mrhi_stateSealed ? &s_sealedBuffer : &s_uses[state];
}

// The barriers gathered for one vkCmdPipelineBarrier2.
typedef struct Batch
{
    VkImageMemoryBarrier2 images[BATCH];
    VkBufferMemoryBarrier2 buffers[BATCH];
    uint32_t imageCount;
    uint32_t bufferCount;
} Batch;

static void Flush(const mrhiVulkanRecording* recording, Batch* batch)
{
    if (batch->imageCount + batch->bufferCount == 0)
    {
        return;
    }
    const VkDependencyInfo dependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = batch->bufferCount,
        .pBufferMemoryBarriers = batch->buffers,
        .imageMemoryBarrierCount = batch->imageCount,
        .pImageMemoryBarriers = batch->images,
    };
    recording->frames->api->vkCmdPipelineBarrier2(recording->slot->commands, &dependency);
    batch->imageCount = 0;
    batch->bufferCount = 0;
}

static bool IsTexture(const mrhiDriverFrame* frame, uint32_t index1)
{
    MRHI_ASSERT(index1 != 0 && index1 <= frame->resourceCount);
    mrhiDriverResourceKind kind = frame->resources[index1 - 1].kind;
    return kind == mrhiDriverDeviceTexture || kind == mrhiDriverTransientTexture ||
           kind == mrhiDriverSurfaceImage;
}

static void AddBarrier(const mrhiVulkanRecording* recording, const mrhiBarrier* barrier,
                       Batch* batch)
{
    const Use* before = &s_uses[barrier->before];
    const Use* after = &s_uses[barrier->after];
    uint32_t index1 = barrier->resource.index1;
    if (!IsTexture(recording->frame, index1))
    {
        before = BufferUse(barrier->before);
        after = BufferUse(barrier->after);
        batch->buffers[batch->bufferCount++] = (VkBufferMemoryBarrier2){
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = before->stages,
            .srcAccessMask = before->access,
            .dstStageMask = after->stages,
            .dstAccessMask = after->access,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = mrhiVulkanFrameBuffer(recording, index1),
            .size = VK_WHOLE_SIZE,
        };
    }
    else
    {
        const mrhiTextureRange* range = &barrier->range;
        mrhiVulkanFrameTexture texture = mrhiVulkanFrameImage(recording, index1);
        // A surface image's first transition waits for its acquire, whose
        // semaphore the frame waits on at all commands.
        bool acquired = barrier->before == mrhi_stateUndefined &&
                        recording->frame->resources[index1 - 1].kind == mrhiDriverSurfaceImage;
        batch->images[batch->imageCount++] = (VkImageMemoryBarrier2){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = acquired ? VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT : before->stages,
            .srcAccessMask = before->access,
            .dstStageMask = after->stages,
            .dstAccessMask = after->access,
            .oldLayout = before->layout,
            .newLayout = after->layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = texture.image,
            .subresourceRange = {mrhiVulkanAspect(range->aspect, texture.def->format),
                                 range->baseMip, range->mipCount, range->baseLayer,
                                 range->layerCount},
        };
    }
    if (batch->imageCount == BATCH || batch->bufferCount == BATCH)
    {
        Flush(recording, batch);
    }
}

// Records the barriers from the cursor on that come before a pass (a
// null id for the frame's end), moving the cursor past them.
static void Barriers(const mrhiVulkanRecording* recording, size_t* cursor, mrhiPassId pass)
{
    const mrhiDriverFrame* frame = recording->frame;
    Batch batch = {.imageCount = 0};
    size_t at = *cursor;
    while (at < frame->barrierCount && frame->barriers[at].pass.index1 == pass.index1 &&
           frame->barriers[at].pass.generation == pass.generation)
    {
        AddBarrier(recording, &frame->barriers[at], &batch);
        ++at;
    }
    Flush(recording, &batch);
    *cursor = at;
}

// A copy's region between a buffer side and a texture side, and the
// bytes it spans in the buffer.
static VkBufferImageCopy RegionOf(const mrhiCommandBufferSide* buffer,
                                  const mrhiCommandTextureSide* texture, const mrhiTextureDef* def,
                                  const mrhiCommand* command, uint64_t* bytesOut)
{
    mrhiFormatBlock block = mrhiGetFormatBlock(def->format);
    uint32_t texelBytes = mrhiGetFormatCopy(def->format, texture->aspect).bytes;
    MRHI_ASSERT(texelBytes > 0);
    bool volume = def->kind == mrhi_texture3d;
    uint32_t width = (uint32_t)command->b;
    uint32_t height = (uint32_t)command->c;
    uint32_t depth = (uint32_t)command->d;
    uint64_t rows = height / block.height;
    uint64_t rowBytes = (uint64_t)width / block.width * texelBytes;
    *bytesOut = (uint64_t)buffer->bytesPerRow * buffer->rowsPerImage * (depth - 1) +
                (uint64_t)buffer->bytesPerRow * (rows - 1) + rowBytes;
    return (VkBufferImageCopy){
        .bufferOffset = buffer->offset,
        .bufferRowLength = buffer->bytesPerRow / texelBytes * block.width,
        .bufferImageHeight = buffer->rowsPerImage * block.height,
        .imageSubresource =
            {
                .aspectMask = mrhiVulkanAspect(texture->aspect, def->format),
                .mipLevel = texture->mip,
                .baseArrayLayer = volume ? 0 : texture->z,
                .layerCount = volume ? 1 : depth,
            },
        .imageOffset = {(int32_t)texture->x, (int32_t)texture->y, volume ? (int32_t)texture->z : 0},
        .imageExtent = {width, height, volume ? depth : 1},
    };
}

// A buffer side's Vulkan buffer: staging and the readback ring are
// object 0.
static VkBuffer SideBuffer(const mrhiVulkanRecording* recording, const mrhiCommand* command,
                           uint32_t object)
{
    if (object != 0)
    {
        return mrhiVulkanFrameBuffer(recording, object);
    }
    bool upload =
        command->type == mrhiCommandWriteBuffer || command->type == mrhiCommandWriteTexture;
    return upload ? recording->slot->staging : recording->frames->readback;
}

static void NoteReadback(const mrhiVulkanRecording* recording, uint64_t offset, uint64_t size)
{
    mrhiVulkanSlot* slot = recording->slot;
    MRHI_ASSERT(slot->readbackCount < recording->frames->readbackLimit);
    slot->readbacks[slot->readbackCount++] = (mrhiVulkanRange){.offset = offset, .size = size};
}

static void CopyBuffers(const mrhiVulkanRecording* recording, const mrhiCommand* command)
{
    mrhiCommandBufferSide sides[2];
    memcpy(sides, &command[1], sizeof(sides));
    const VkBufferCopy region = {
        .srcOffset = sides[0].offset,
        .dstOffset = sides[1].offset,
        .size = command->b,
    };
    recording->frames->api->vkCmdCopyBuffer(
        recording->slot->commands, SideBuffer(recording, command, sides[0].object),
        SideBuffer(recording, command, sides[1].object), 1, &region);
    if (command->type == mrhiCommandReadBuffer)
    {
        NoteReadback(recording, sides[1].offset, command->b);
    }
}

static void CopyWithTexture(const mrhiVulkanRecording* recording, const mrhiCommand* command)
{
    bool fromBuffer =
        command->type == mrhiCommandCopyBufferToTexture || command->type == mrhiCommandWriteTexture;
    mrhiCommandBufferSide buffer;
    mrhiCommandTextureSide texture;
    memcpy(&buffer, fromBuffer ? &command[1] : &command[2], sizeof(buffer));
    memcpy(&texture, fromBuffer ? &command[2] : &command[1], sizeof(texture));
    mrhiVulkanFrameTexture image = mrhiVulkanFrameImage(recording, texture.object);
    uint64_t bytes = 0;
    VkBufferImageCopy region = RegionOf(&buffer, &texture, image.def, command, &bytes);
    VkBuffer side = SideBuffer(recording, command, buffer.object);
    const mrhiVulkanDevice* api = recording->frames->api;
    if (fromBuffer)
    {
        api->vkCmdCopyBufferToImage(recording->slot->commands, side, image.image,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        return;
    }
    api->vkCmdCopyImageToBuffer(recording->slot->commands, image.image,
                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, side, 1, &region);
    if (command->type == mrhiCommandReadTexture)
    {
        NoteReadback(recording, buffer.offset, bytes);
    }
}

static void CopyTextures(const mrhiVulkanRecording* recording, const mrhiCommand* command)
{
    mrhiCommandTextureSide sides[2];
    memcpy(sides, &command[1], sizeof(sides));
    const mrhiCommandTextureSide* source = &sides[0];
    const mrhiCommandTextureSide* target = &sides[1];
    mrhiVulkanFrameTexture from = mrhiVulkanFrameImage(recording, source->object);
    mrhiVulkanFrameTexture to = mrhiVulkanFrameImage(recording, target->object);
    bool volume = from.def->kind == mrhi_texture3d;
    uint32_t depth = (uint32_t)command->d;
    const VkImageCopy region = {
        .srcSubresource = {mrhiVulkanAspect(source->aspect, from.def->format), source->mip,
                           volume ? 0 : source->z, volume ? 1 : depth},
        .srcOffset = {(int32_t)source->x, (int32_t)source->y, volume ? (int32_t)source->z : 0},
        .dstSubresource = {mrhiVulkanAspect(target->aspect, to.def->format), target->mip,
                           volume ? 0 : target->z, volume ? 1 : depth},
        .dstOffset = {(int32_t)target->x, (int32_t)target->y, volume ? (int32_t)target->z : 0},
        .extent = {(uint32_t)command->b, (uint32_t)command->c, volume ? depth : 1},
    };
    recording->frames->api->vkCmdCopyImage(recording->slot->commands, from.image,
                                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, to.image,
                                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

static void SetPipeline(mrhiVulkanRecording* recording, const mrhiCommand* command)
{
    const mrhiVulkanPipeline* pipeline = &recording->frames->pipelines->pipelines[command->b - 1];
    recording->pipeline = pipeline;
    recording->frames->api->vkCmdBindPipeline(recording->slot->commands, pipeline->bindPoint,
                                              pipeline->pipeline);
    // The pass's heap, bound again with each pipeline reading heaps,
    // since pipelines of other table layouts disturb it (mrhi-0015).
    uint64_t heap = recording->pass->heap;
    if (pipeline->heap && heap != 0)
    {
        VkDescriptorSet set = mrhiVulkanHeapSet(recording->frames->heaps, heap);
        recording->frames->api->vkCmdBindDescriptorSets(recording->slot->commands,
                                                        pipeline->bindPoint, pipeline->layout,
                                                        MRHI_VULKAN_TABLES, 1, &set, 0, nullptr);
    }
}

// Sets dynamic state from a command and the payload that follows it.
static void SetState(const mrhiVulkanRecording* recording, const mrhiCommand* command)
{
    const mrhiVulkanDevice* api = recording->frames->api;
    VkCommandBuffer commands = recording->slot->commands;
    switch (command->type)
    {
    case mrhiCommandRootBlock:
        api->vkCmdPushConstants(commands, recording->pipeline->layout,
                                recording->pipeline->rootStages, command->a, (uint32_t)command->b,
                                &command[1]);
        break;
    case mrhiCommandViewport:
    {
        mrhiViewport viewport;
        memcpy(&viewport, &command[1], sizeof(viewport));
        mrhiVulkanSetViewport(recording, viewport.x, viewport.y, viewport.width, viewport.height,
                              viewport.minDepth, viewport.maxDepth);
        break;
    }
    case mrhiCommandScissor:
    {
        const VkRect2D scissor = {{(int32_t)command->a, (int32_t)command->b},
                                  {(uint32_t)command->c, (uint32_t)command->d}};
        api->vkCmdSetScissor(commands, 0, 1, &scissor);
        break;
    }
    case mrhiCommandBlendConstant:
    {
        mrhiClearColor color;
        memcpy(&color, &command[1], sizeof(color));
        const float constants[4] = {color.red, color.green, color.blue, color.alpha};
        api->vkCmdSetBlendConstants(commands, constants);
        break;
    }
    default:
        MRHI_ASSERT(command->type == mrhiCommandStencilReference);
        api->vkCmdSetStencilReference(commands, VK_STENCIL_FACE_FRONT_AND_BACK, command->a);
        break;
    }
}

static void BindBuffer(const mrhiVulkanRecording* recording, const mrhiCommand* command)
{
    VkBuffer buffer = mrhiVulkanFrameBuffer(recording, (uint32_t)command->b);
    VkCommandBuffer commands = recording->slot->commands;
    if (command->type == mrhiCommandIndexBuffer)
    {
        VkIndexType type =
            command->a == mrhi_indexUint16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
        recording->frames->api->vkCmdBindIndexBuffer(commands, buffer, command->c, type);
        return;
    }
    const VkDeviceSize offset = command->c;
    const VkDeviceSize size = command->d;
    recording->frames->api->vkCmdBindVertexBuffers2(commands, command->a, 1, &buffer, &offset,
                                                    &size, nullptr);
}

static void Draw(const mrhiVulkanRecording* recording, const mrhiCommand* command)
{
    const mrhiVulkanDevice* api = recording->frames->api;
    VkCommandBuffer commands = recording->slot->commands;
    switch (command->type)
    {
    case mrhiCommandDraw:
        api->vkCmdDraw(commands, command->a, (uint32_t)command->b, (uint32_t)command->c,
                       (uint32_t)command->d);
        break;
    case mrhiCommandDrawIndexed:
        api->vkCmdDrawIndexed(commands, command->a, (uint32_t)command->b, (uint32_t)command->c,
                              (int32_t)(uint32_t)(command->c >> 32), (uint32_t)command->d);
        break;
    case mrhiCommandDispatch:
        api->vkCmdDispatch(commands, command->a, (uint32_t)command->b, (uint32_t)command->c);
        break;
    case mrhiCommandDrawIndirect:
        api->vkCmdDrawIndirect(commands, mrhiVulkanFrameBuffer(recording, command->a), command->c,
                               1, 0);
        break;
    case mrhiCommandDrawIndexedIndirect:
        api->vkCmdDrawIndexedIndirect(commands, mrhiVulkanFrameBuffer(recording, command->a),
                                      command->c, 1, 0);
        break;
    default:
        MRHI_ASSERT(command->type == mrhiCommandDispatchIndirect);
        api->vkCmdDispatchIndirect(commands, mrhiVulkanFrameBuffer(recording, command->a),
                                   command->c);
        break;
    }
}

static void RecordCommand(mrhiVulkanRecording* recording, const mrhiCommand* command)
{
    switch (command->type)
    {
    case mrhiCommandGraphicsPipeline:
    case mrhiCommandComputePipeline:
        SetPipeline(recording, command);
        break;
    case mrhiCommandRootBlock:
    case mrhiCommandViewport:
    case mrhiCommandScissor:
    case mrhiCommandBlendConstant:
    case mrhiCommandStencilReference:
        SetState(recording, command);
        break;
    case mrhiCommandBindings:
        mrhiVulkanBindTable(recording, command);
        break;
    case mrhiCommandVertexBuffer:
    case mrhiCommandIndexBuffer:
        BindBuffer(recording, command);
        break;
    case mrhiCommandDraw:
    case mrhiCommandDrawIndexed:
    case mrhiCommandDispatch:
    case mrhiCommandDrawIndirect:
    case mrhiCommandDrawIndexedIndirect:
    case mrhiCommandDispatchIndirect:
        Draw(recording, command);
        break;
    case mrhiCommandCopyBuffer:
    case mrhiCommandWriteBuffer:
    case mrhiCommandReadBuffer:
        CopyBuffers(recording, command);
        break;
    case mrhiCommandCopyBufferToTexture:
    case mrhiCommandCopyTextureToBuffer:
    case mrhiCommandWriteTexture:
    case mrhiCommandReadTexture:
        CopyWithTexture(recording, command);
        break;
    case mrhiCommandCopyTexture:
        CopyTextures(recording, command);
        break;
    case mrhiCommandBeginOcclusionQuery:
    case mrhiCommandEndOcclusionQuery:
    case mrhiCommandResolveQueries:
        mrhiVulkanQuery(recording, command);
        break;
    case mrhiCommandPushDebugGroup:
        mrhiVulkanBeginLabel(recording->frames->api, recording->slot->commands,
                             (const char*)&command[1], (size_t)command->b);
        break;
    case mrhiCommandPopDebugGroup:
        mrhiVulkanEndLabel(recording->frames->api, recording->slot->commands);
        break;
    case mrhiCommandDebugMarker:
        mrhiVulkanInsertLabel(recording->frames->api, recording->slot->commands,
                              (const char*)&command[1], (size_t)command->b);
        break;
    default:
        MRHI_ASSERT(false);
        break;
    }
}

mrhiResult mrhiVulkanRecord(mrhiVulkanFrames* frames, mrhiVulkanSlot* slot,
                            const mrhiDriverFrame* frame)
{
    mrhiVulkanRecording recording = {
        .frames = frames,
        .slot = slot,
        .frame = frame,
        .serial = frames->submitted + 1,
        .status = mrhi_success,
    };
    mrhiVulkanResetQueries(&recording);
    size_t barrier = 0;
    for (uint32_t p = 0; p < frame->passCount && recording.status == mrhi_success; ++p)
    {
        recording.pass = &frame->passes[p];
        bool labelled = recording.pass->labelLength > 0;
        if (labelled)
        {
            mrhiVulkanBeginLabel(frames->api, slot->commands, recording.pass->label,
                                 recording.pass->labelLength);
        }
        Barriers(&recording, &barrier, recording.pass->id);
        mrhiVulkanPassTimestamp(&recording, false);
        mrhiVulkanBeginPass(&recording);
        for (uint32_t chunk = recording.pass->firstChunk; chunk != 0;
             chunk = frame->chunks[chunk - 1].next)
        {
            const mrhiCommandChunk* at = &frame->chunks[chunk - 1];
            for (uint32_t i = 0; i < at->count; i += 1u + at->commands[i].payload)
            {
                RecordCommand(&recording, &at->commands[i]);
            }
        }
        mrhiVulkanEndPass(&recording);
        mrhiVulkanPassTimestamp(&recording, true);
        if (labelled)
        {
            mrhiVulkanEndLabel(frames->api, slot->commands);
        }
    }
    Barriers(&recording, &barrier, (mrhiPassId){0});
    return recording.status;
}
