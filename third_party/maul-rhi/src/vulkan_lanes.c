// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A frame's lanes and batches on Vulkan (vulkan_lanes.h). Batches are
// submitted in the order they open, which is the frame's order of their
// first passes, and a batch waits only for passes earlier in the frame,
// whose batches have closed by then: every wait names a value whose
// signal is submitted before it.

#include "vulkan_lanes.h"

#include "invariant.h"
#include "vulkan_query.h"

// Notes a failure of the device, the first only.
static void Note(mrhiVulkanRecording* recording, VkResult result)
{
    if (recording->result == VK_SUCCESS && result != VK_SUCCESS)
    {
        recording->result = result;
        // A command buffer it cannot begin or end is out of the command
        // lists budget, unless the frame failed already for another.
        recording->budget =
            recording->budget != 0 ? recording->budget : mrhi_diagnosticSubmitCommandLists;
    }
}

// Begins a lane's next part and makes it the command buffer recording.
static VkResult BeginPart(mrhiVulkanRecording* recording, uint32_t lane)
{
    mrhiVulkanSlot* slot = recording->slot;
    // A batch takes a part, and each native pass one more.
    MRHI_ASSERT(slot->partsUsed < recording->frames->partLimit);
    VkCommandBuffer commands = slot->parts[slot->partsUsed++];
    const VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    recording->laneCommands[lane] = commands;
    slot->commands = commands;
    return recording->frames->api->vkBeginCommandBuffer(commands, &begin);
}

// Ends a lane's part and adds it, then a command buffer of the program's
// (none when NULL), to the lane's batch.
static VkResult EndPart(mrhiVulkanRecording* recording, uint32_t lane, void* native)
{
    VkCommandBuffer commands = recording->laneCommands[lane];
    VkResult result = recording->frames->api->vkEndCommandBuffer(commands);
    VkCommandBufferSubmitInfo* submits = recording->laneSubmits[lane];
    uint32_t* count = &recording->laneSubmitCount[lane];
    MRHI_ASSERT(*count + (native != nullptr ? 2u : 1u) <= MRHI_VULKAN_BATCH_SUBMITS);
    submits[(*count)++] = (VkCommandBufferSubmitInfo){
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = commands,
    };
    if (native != nullptr)
    {
        submits[(*count)++] = (VkCommandBufferSubmitInfo){
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = (VkCommandBuffer)native,
        };
    }
    return result;
}

// Opens a batch on a lane that waits for the other lanes' given values.
static void Open(mrhiVulkanRecording* recording, uint32_t lane, const uint64_t* waits)
{
    mrhiVulkanSlot* slot = recording->slot;
    // A pass opens one batch at most, beside the frame's first and last.
    MRHI_ASSERT(slot->batchCount < recording->frames->batchLimit);
    mrhiVulkanBatch* batch = &slot->batches[slot->batchCount++];
    *batch = (mrhiVulkanBatch){.lane = lane};
    for (uint32_t l = 0; l < MRHI_VULKAN_LANES; ++l)
    {
        batch->waits[l] = l == lane ? 0 : waits[l];
    }
    recording->laneBatch[lane] = slot->batchCount;
    recording->laneSubmitCount[lane] = 0;
    recording->laneStarted[lane] = true;
    if (recording->result == VK_SUCCESS)
    {
        Note(recording, BeginPart(recording, lane));
    }
}

// Closes a lane's open batch, if any: its command buffers join the
// slot's, and with more than one lane it signals the lane's next value.
// After the device failed nothing is ended, as a part may not have begun,
// and the frame is not submitted.
static void Close(mrhiVulkanRecording* recording, uint32_t lane)
{
    if (recording->laneBatch[lane] == 0 || recording->result != VK_SUCCESS)
    {
        return;
    }
    Note(recording, EndPart(recording, lane, nullptr));
    mrhiVulkanSlot* slot = recording->slot;
    mrhiVulkanBatch* batch = &slot->batches[recording->laneBatch[lane] - 1];
    batch->firstSubmit = slot->submitCount;
    batch->submitCount = recording->laneSubmitCount[lane];
    for (uint32_t i = 0; i < batch->submitCount; ++i)
    {
        slot->submits[slot->submitCount++] = recording->laneSubmits[lane][i];
    }
    if (MRHI_VULKAN_SPLIT)
    {
        batch->signal = ++recording->frames->laneValues[lane];
        recording->laneLast[lane] = batch->signal;
    }
    recording->laneBatch[lane] = 0;
}

void mrhiVulkanBeginLanes(mrhiVulkanRecording* recording)
{
    mrhiVulkanFrames* frames = recording->frames;
    const mrhiDriverFrame* frame = recording->frame;
    for (uint32_t p = 0; p < frame->passCount; ++p)
    {
        frames->followed[frame->passes[p].id.index1] = false;
    }
    for (uint32_t p = 0; p < frame->passCount; ++p)
    {
        const mrhiDriverPass* pass = &frame->passes[p];
        for (uint32_t c = 0; c < MRHI_PASS_CLASSES; ++c)
        {
            uint32_t followed = pass->waitFor[c].index1;
            frames->followed[followed] = frames->followed[followed] ||
                                         (followed != 0 && mrhiVulkanLaneOf((mrhiPassClass)c) !=
                                                               mrhiVulkanLaneOf(pass->passClass));
        }
    }
    const uint64_t none[MRHI_VULKAN_LANES] = {0};
    Open(recording, 0, none);
    // A part that failed to begin records nothing.
    if (recording->result != VK_SUCCESS)
    {
        return;
    }
    mrhiVulkanResetQueries(recording);
    // With more than one lane the resets are a batch every lane waits for.
    if (MRHI_VULKAN_SPLIT)
    {
        Close(recording, 0);
    }
}

void mrhiVulkanEnterPass(mrhiVulkanRecording* recording)
{
    mrhiVulkanFrames* frames = recording->frames;
    mrhiVulkanSlot* slot = recording->slot;
    const mrhiDriverPass* pass = recording->pass;
    uint32_t lane = mrhiVulkanLaneOf(pass->passClass);
    uint64_t waits[MRHI_VULKAN_LANES] = {0};
    bool follows = false;
    for (uint32_t c = 0; c < MRHI_PASS_CLASSES; ++c)
    {
        uint32_t followed = pass->waitFor[c].index1;
        uint32_t other = mrhiVulkanLaneOf((mrhiPassClass)c);
        if (followed == 0 || other == lane)
        {
            continue;
        }
        // Each pass another lane follows ends its batch.
        const mrhiVulkanBatch* batch = &slot->batches[frames->passBatches[followed] - 1];
        MRHI_ASSERT(batch->signal != 0);
        waits[other] = batch->signal > waits[other] ? batch->signal : waits[other];
        follows = true;
    }
    if (!recording->laneStarted[lane])
    {
        waits[0] = slot->batches[0].signal > waits[0] ? slot->batches[0].signal : waits[0];
    }
    if (follows)
    {
        Close(recording, lane);
    }
    if (recording->laneBatch[lane] == 0)
    {
        Open(recording, lane, waits);
    }
    frames->passBatches[pass->id.index1] = recording->laneBatch[lane];
    slot->commands = recording->laneCommands[lane];
}

void mrhiVulkanLeavePass(mrhiVulkanRecording* recording)
{
    if (recording->frames->followed[recording->pass->id.index1])
    {
        Close(recording, mrhiVulkanLaneOf(recording->pass->passClass));
    }
}

void mrhiVulkanEndLanes(mrhiVulkanRecording* recording)
{
    if (!MRHI_VULKAN_SPLIT)
    {
        recording->slot->commands = recording->laneCommands[0];
        return;
    }
    uint64_t waits[MRHI_VULKAN_LANES];
    for (uint32_t l = 0; l < MRHI_VULKAN_LANES; ++l)
    {
        Close(recording, l);
        waits[l] = recording->laneLast[l];
    }
    Open(recording, 0, waits);
}

void mrhiVulkanCloseLanes(mrhiVulkanRecording* recording)
{
    Close(recording, 0);
}

bool mrhiVulkanNextPart(mrhiVulkanRecording* recording, void* native)
{
    uint32_t lane = mrhiVulkanLaneOf(recording->pass->passClass);
    Note(recording, EndPart(recording, lane, native));
    if (recording->result == VK_SUCCESS)
    {
        Note(recording, BeginPart(recording, lane));
    }
    return recording->result == VK_SUCCESS;
}
