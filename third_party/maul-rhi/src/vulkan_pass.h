// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Passes on Vulkan (vulkan_pass.c) and the state of recording one
// submitted frame, which the recorder (vulkan_record.c) keeps.

#ifndef MAUL_RHI_SRC_VULKAN_PASS_H
#define MAUL_RHI_SRC_VULKAN_PASS_H

#include "command.h"
#include "vulkan_frame.h"

typedef struct mrhiVulkanRecording
{
    mrhiVulkanFrames* frames;
    mrhiVulkanSlot* slot;
    const mrhiDriverFrame* frame;
    // The frame's serial, and the occlusion query open in the pass.
    uint64_t serial;
    uint32_t openQuery;
    // The pass recording, and the pipeline it set last.
    const mrhiDriverPass* pass;
    const mrhiVulkanPipeline* pipeline;
    // The first failure: views or descriptors the device could not make.
    mrhiResult status;
} mrhiVulkanRecording;

// A frame resource's image and def, and its buffer.
typedef struct mrhiVulkanFrameTexture
{
    VkImage image;
    const mrhiTextureDef* def;
} mrhiVulkanFrameTexture;

mrhiVulkanFrameTexture mrhiVulkanFrameImage(const mrhiVulkanRecording* recording, uint32_t index1);
VkBuffer mrhiVulkanFrameBuffer(const mrhiVulkanRecording* recording, uint32_t index1);

// The Vulkan aspects of a contract aspect on a format.
VkImageAspectFlags mrhiVulkanAspect(mrhiTextureAspect aspect, mrhiFormat format);

// Begins a pass's rendering with its targets, setting the default
// viewport, scissor, blend constant and stencil reference; nothing for a
// pass without targets.
void mrhiVulkanBeginPass(mrhiVulkanRecording* recording);

// Ends a pass's rendering, if it began one.
void mrhiVulkanEndPass(mrhiVulkanRecording* recording);

// Writes a table's bindings into a new descriptor set of the pipeline's
// layout and binds it.
void mrhiVulkanBindTable(mrhiVulkanRecording* recording, const mrhiCommand* command);

// Sets a viewport with +Y up, as every driver has it: a negative height
// flips Vulkan's.
void mrhiVulkanSetViewport(const mrhiVulkanRecording* recording, float x, float y, float width,
                           float height, float minDepth, float maxDepth);

#endif // MAUL_RHI_SRC_VULKAN_PASS_H
