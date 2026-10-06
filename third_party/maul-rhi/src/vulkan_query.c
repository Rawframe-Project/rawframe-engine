// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Queries on Vulkan (vulkan_query.h), pipeline statistics among them
// (mrhi-0023). A frame's recording marks the queries it writes, so that
// a resolve copies those, waiting for their results, and fills the rest
// with 0: Vulkan leaves a query that was never written unwritten in the
// buffer.

#include "vulkan_query.h"

#include "invariant.h"

#include <string.h>

static mrhiVulkanQuerySet* SetOf(const mrhiVulkanRecording* recording, uint64_t handle)
{
    MRHI_ASSERT(handle != 0 && handle <= recording->frames->objects->querySetSlots.capacity);
    return &recording->frames->objects->querySets[handle - 1];
}

static void Reset(const mrhiVulkanRecording* recording, uint64_t handle)
{
    mrhiVulkanQuerySet* set = SetOf(recording, handle);
    if (set->resetSerial == recording->serial)
    {
        return;
    }
    set->resetSerial = recording->serial;
    memset(set->written, 0, sizeof(set->written));
    recording->frames->api->vkCmdResetQueryPool(recording->slot->commands, set->pool, 0,
                                                set->count);
}

static void ResetNamed(const mrhiVulkanRecording* recording, const mrhiDriverPass* pass)
{
    const mrhiDriverFrame* frame = recording->frame;
    for (uint32_t chunk = pass->firstChunk; chunk != 0; chunk = frame->chunks[chunk - 1].next)
    {
        const mrhiCommandChunk* at = &frame->chunks[chunk - 1];
        for (uint32_t i = 0; i < at->count; i += 1u + at->commands[i].payload)
        {
            const mrhiCommand* command = &at->commands[i];
            if (command->type == mrhiCommandBeginOcclusionQuery ||
                command->type == mrhiCommandBeginStatisticsQuery ||
                command->type == mrhiCommandResolveQueries)
            {
                Reset(recording, command->b);
            }
        }
    }
}

void mrhiVulkanResetQueries(const mrhiVulkanRecording* recording)
{
    const mrhiDriverFrame* frame = recording->frame;
    for (uint32_t p = 0; p < frame->passCount; ++p)
    {
        const mrhiDriverPass* pass = &frame->passes[p];
        if (pass->timestampSet != 0)
        {
            Reset(recording, pass->timestampSet);
        }
        ResetNamed(recording, pass);
    }
}

static void Mark(mrhiVulkanQuerySet* set, uint32_t query)
{
    set->written[query / 64] |= UINT64_C(1) << (query % 64);
}

static bool IsWritten(const mrhiVulkanQuerySet* set, uint32_t query)
{
    return (set->written[query / 64] >> (query % 64) & 1u) != 0;
}

void mrhiVulkanPassTimestamp(const mrhiVulkanRecording* recording, bool end)
{
    const mrhiDriverPass* pass = recording->pass;
    uint32_t query = end ? pass->timestampEnd : pass->timestampBegin;
    if (pass->timestampSet == 0 || query == MRHI_NO_QUERY)
    {
        return;
    }
    mrhiVulkanQuerySet* set = SetOf(recording, pass->timestampSet);
    Mark(set, query);
    VkPipelineStageFlags2 stage =
        end ? VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    recording->frames->api->vkCmdWriteTimestamp2(recording->slot->commands, stage, set->pool,
                                                 query);
}

// Copies the queries the frame wrote and fills the others with 0, in
// runs of either.
static void Resolve(const mrhiVulkanRecording* recording, const mrhiCommand* command)
{
    const mrhiVulkanDevice* api = recording->frames->api;
    VkCommandBuffer commands = recording->slot->commands;
    const mrhiVulkanQuerySet* set = SetOf(recording, command->b);
    VkBuffer buffer = mrhiVulkanFrameBuffer(recording, (uint32_t)command->a);
    uint32_t first = (uint32_t)command->c;
    uint32_t count = (uint32_t)(command->c >> 32);
    VkDeviceSize stride = mrhiQueryBytes(set->type);
    uint32_t i = 0;
    while (i < count)
    {
        bool written = IsWritten(set, first + i);
        uint32_t run = 1;
        while (i + run < count && IsWritten(set, first + i + run) == written)
        {
            ++run;
        }
        VkDeviceSize offset = command->d + i * stride;
        if (written)
        {
            api->vkCmdCopyQueryPoolResults(commands, set->pool, first + i, run, buffer, offset,
                                           stride,
                                           VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        }
        else
        {
            api->vkCmdFillBuffer(commands, buffer, offset, run * stride, 0);
        }
        i += run;
    }
}

void mrhiVulkanQuery(mrhiVulkanRecording* recording, const mrhiCommand* command)
{
    const mrhiVulkanDevice* api = recording->frames->api;
    VkCommandBuffer commands = recording->slot->commands;
    switch (command->type)
    {
    case mrhiCommandBeginOcclusionQuery:
    {
        mrhiVulkanQuerySet* set = SetOf(recording, command->b);
        Mark(set, (uint32_t)command->a);
        recording->openQuery = (uint32_t)command->a;
        api->vkCmdBeginQuery(commands, set->pool, recording->openQuery, 0);
        break;
    }
    case mrhiCommandEndOcclusionQuery:
        api->vkCmdEndQuery(commands, SetOf(recording, recording->pass->occlusionSet)->pool,
                           recording->openQuery);
        break;
    case mrhiCommandBeginStatisticsQuery:
    {
        mrhiVulkanQuerySet* set = SetOf(recording, command->b);
        Mark(set, (uint32_t)command->a);
        api->vkCmdBeginQuery(commands, set->pool, (uint32_t)command->a, 0);
        break;
    }
    case mrhiCommandEndStatisticsQuery:
        api->vkCmdEndQuery(commands, SetOf(recording, command->b)->pool, (uint32_t)command->a);
        break;
    default:
        Resolve(recording, command);
        break;
    }
}
