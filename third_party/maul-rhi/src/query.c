// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Query sets (mrhi-0012): device objects of occlusion, timestamp or
// pipeline statistics queries (mrhi-0023), each holding a run of the
// device's query marks, which record the frame that last wrote each
// query; the occlusion queries a render pass brackets its draws with;
// and the statistics queries a pass of the graphics class brackets its
// work with.

#include "encoder_core.h"
#include "label.h"

#include <stdatomic.h>

#define QUERY_SET_DEF_COOKIE 0x6D727173u
// WebGPU's bound on a set's queries.
#define MOST_QUERIES 4096

mrhiQuerySetDef mrhiDefaultQuerySetDef(void)
{
    mrhiQuerySetDef def = {0};
    def.cookie = QUERY_SET_DEF_COOKIE;
    def.type = mrhi_queryOcclusion;
    def.count = 1;
    return def;
}

// The first run of count marks no live set holds, first fit: its start,
// or false when the device's marks lack one.
static bool FindRun(const mrhiDevice* device, uint32_t count, uint32_t* firstOut)
{
    uint32_t sets = device->deviceLimits.querySets;
    uint32_t start = 0;
    bool moved = true;
    while (moved)
    {
        moved = false;
        for (uint32_t i = 0; i < sets; ++i)
        {
            // A free slot is zeroed: its empty run overlaps nothing.
            const mrhiQuerySetSlot* slot = &device->querySetSlots[i];
            if (slot->first < start + count && start < slot->first + slot->count)
            {
                start = slot->first + slot->count;
                moved = true;
            }
        }
        if (start > device->deviceLimits.queries - count)
        {
            return false;
        }
    }
    *firstOut = start;
    return true;
}

mrhiResult mrhiCreateQuerySet(mrhiDevice* device, const mrhiQuerySetDef* def,
                              mrhiQuerySetId* setOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (def == nullptr || setOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    mrhiResult status = mrhiCheckObjectDef(device, MRHI_DEF_HEAD(def), QUERY_SET_DEF_COOKIE);
    if (status != mrhi_success)
    {
        return status;
    }
    if (def->type > mrhi_queryPipelineStatistics || def->count == 0 || def->count > MOST_QUERIES)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticQuerySetDef);
    }
    if ((def->type == mrhi_queryTimestamp && !device->features.timestampQuery) ||
        (def->type == mrhi_queryPipelineStatistics && !device->features.pipelineStatisticsQuery))
    {
        return mrhi_errorUnsupported;
    }
    status = mrhiDeviceUsable(device);
    if (status != mrhi_success)
    {
        return status;
    }
    uint32_t first = 0;
    uint32_t index1 = 0;
    uint32_t generation = 0;
    if (def->count > device->deviceLimits.queries || !FindRun(device, def->count, &first) ||
        !mrhiPoolAcquire(&device->querySets, &index1, &generation))
    {
        return mrhi_errorCapacity;
    }
    uint64_t handle = 0;
    mrhiQuerySetDef driverDef = *def;
    mrhiDropLabel(&driverDef.label, &driverDef.labelLength);
    status = mrhiDriverStatus(
        device, device->driver.vtable->createQuerySet(device->driver.self, &driverDef, &handle));
    if (status != mrhi_success)
    {
        mrhiPoolRelease(&device->querySets, index1);
        return status;
    }
    device->querySetSlots[index1 - 1] = (mrhiQuerySetSlot){
        .handle = handle,
        .first = first,
        .count = def->count,
        .type = def->type,
    };
    // No frame has serial 0, so the new set's queries are unwritten.
    for (uint32_t i = 0; i < def->count; ++i)
    {
        atomic_init(&device->queryMarks[first + i], 0);
    }
    *setOut = (mrhiQuerySetId){index1, generation};
    return mrhi_success;
}

mrhiResult mrhiDestroyQuerySet(mrhiDevice* device, mrhiQuerySetId set)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (!mrhiPoolIsLive(&device->querySets, set.index1, set.generation))
    {
        return mrhi_errorStale;
    }
    mrhiQuerySetSlot* slot = &device->querySetSlots[set.index1 - 1];
    device->driver.vtable->destroyQuerySet(device->driver.self, slot->handle);
    *slot = (mrhiQuerySetSlot){0};
    mrhiPoolRelease(&device->querySets, set.index1);
    return mrhi_success;
}

// Checks the occlusion set a pass def names, if any.
static mrhiResult CheckOcclusionSet(const mrhiDevice* device, const mrhiPassDef* def)
{
    mrhiQuerySetId set = def->occlusionQuerySet;
    if (set.index1 == 0)
    {
        return mrhi_success;
    }
    if (!mrhiPoolIsLive(&device->querySets, set.index1, set.generation))
    {
        return mrhi_errorStale;
    }
    bool renders = def->colorTargetCount > 0 || def->depthTarget.resource.index1 != 0;
    return renders && device->querySetSlots[set.index1 - 1].type == mrhi_queryOcclusion
               ? mrhi_success
               : mrhi_errorInvalid;
}

// Whether a timestamp query is none, or one of the set not yet written
// this frame.
static bool IsTimestampFree(const mrhiDevice* device, const mrhiQuerySetSlot* set, uint32_t query)
{
    return query == MRHI_NO_QUERY ||
           (query < set->count &&
            atomic_load_explicit(&device->queryMarks[set->first + query], memory_order_relaxed) !=
                device->frameNumber);
}

mrhiResult mrhiCheckPassQueries(const mrhiDevice* device, const mrhiPassDef* def)
{
    mrhiResult status = CheckOcclusionSet(device, def);
    if (status != mrhi_success)
    {
        return status;
    }
    mrhiQuerySetId set = def->timestampQuerySet;
    uint32_t begin = def->timestampBegin;
    uint32_t end = def->timestampEnd;
    if (set.index1 == 0)
    {
        return begin == MRHI_NO_QUERY && end == MRHI_NO_QUERY ? mrhi_success : mrhi_errorInvalid;
    }
    if (!mrhiPoolIsLive(&device->querySets, set.index1, set.generation))
    {
        return mrhi_errorStale;
    }
    const mrhiQuerySetSlot* slot = &device->querySetSlots[set.index1 - 1];
    // Passes are added on one thread, so a load and a later store suffice.
    bool valid = slot->type == mrhi_queryTimestamp && def->passClass == mrhi_passGraphics &&
                 begin != end && IsTimestampFree(device, slot, begin) &&
                 IsTimestampFree(device, slot, end);
    return valid ? mrhi_success : mrhi_errorInvalid;
}

void mrhiMarkPassTimestamps(mrhiDevice* device, const mrhiPassDef* def, mrhiFramePass* pass)
{
    pass->timestampBegin = MRHI_NO_QUERY;
    pass->timestampEnd = MRHI_NO_QUERY;
    if (def->timestampQuerySet.index1 == 0)
    {
        return;
    }
    const mrhiQuerySetSlot* set = &device->querySetSlots[def->timestampQuerySet.index1 - 1];
    pass->timestampSet = set->handle;
    pass->timestampBegin = def->timestampBegin;
    pass->timestampEnd = def->timestampEnd;
    uint32_t queries[2] = {def->timestampBegin, def->timestampEnd};
    for (uint32_t i = 0; i < 2; ++i)
    {
        if (queries[i] != MRHI_NO_QUERY)
        {
            atomic_store_explicit(&device->queryMarks[set->first + queries[i]], device->frameNumber,
                                  memory_order_relaxed);
        }
    }
}

// Marks a query of a set as written in the open frame: false when the
// frame already wrote it. Passes record in parallel, so the exchange is
// atomic.
static bool MarkWritten(mrhiDevice* device, const mrhiQuerySetSlot* set, uint32_t query)
{
    uint64_t frame = device->frameNumber;
    return atomic_exchange_explicit(&device->queryMarks[set->first + query], frame,
                                    memory_order_relaxed) != frame;
}

mrhiResult mrhiBeginOcclusionQuery(mrhiDevice* device, mrhiPassId id, uint32_t query)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = mrhi_success;
    mrhiFramePass* pass = mrhiRecordingPass(device, id, &status);
    if (pass == nullptr)
    {
        return status;
    }
    if (pass->occlusionSet == 0 || pass->occlusionOpen)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticOcclusionQuery);
    }
    if (!mrhiPoolIsLive(&device->querySets, pass->occlusionSet, pass->occlusionGeneration))
    {
        return mrhi_errorStale;
    }
    const mrhiQuerySetSlot* set = &device->querySetSlots[pass->occlusionSet - 1];
    if (query >= set->count || !MarkWritten(device, set, query))
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticOcclusionQuery);
    }
    // Open even when refused for capacity, so that its end is not misuse.
    pass->occlusionOpen = true;
    mrhiCommand* record = mrhiTakeCommands(device, pass, 1);
    if (record == nullptr)
    {
        return mrhi_errorCapacity;
    }
    *record = (mrhiCommand){.type = mrhiCommandBeginOcclusionQuery, .a = query, .b = set->handle};
    return mrhi_success;
}

mrhiResult mrhiEndOcclusionQuery(mrhiDevice* device, mrhiPassId id)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = mrhi_success;
    mrhiFramePass* pass = mrhiRecordingPass(device, id, &status);
    if (pass == nullptr)
    {
        return status;
    }
    if (!pass->occlusionOpen)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticOcclusionQuery);
    }
    pass->occlusionOpen = false;
    mrhiCommand* record = mrhiTakeCommands(device, pass, 1);
    if (record == nullptr)
    {
        return mrhi_errorCapacity;
    }
    *record = (mrhiCommand){.type = mrhiCommandEndOcclusionQuery};
    return mrhi_success;
}

mrhiResult mrhiBeginStatisticsQuery(mrhiDevice* device, mrhiPassId id, mrhiQuerySetId set,
                                    uint32_t query)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = mrhi_success;
    mrhiFramePass* pass = mrhiRecordingPass(device, id, &status);
    if (pass == nullptr)
    {
        return status;
    }
    if (!mrhiPoolIsLive(&device->querySets, set.index1, set.generation))
    {
        return mrhi_errorStale;
    }
    // Vulkan counts graphics stages only on a graphics queue, and spreads
    // a query over the views of a multiview pass.
    const mrhiQuerySetSlot* slot = &device->querySetSlots[set.index1 - 1];
    if (pass->passClass != mrhi_passGraphics || pass->viewCount > 1 || pass->statisticsOpen ||
        slot->type != mrhi_queryPipelineStatistics || query >= slot->count ||
        !MarkWritten(device, slot, query))
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticStatisticsQuery);
    }
    // Open even when refused for capacity, so that its end is not misuse.
    pass->statisticsOpen = true;
    pass->statisticsQuery = query;
    pass->statisticsSet = slot->handle;
    mrhiCommand* record = mrhiTakeCommands(device, pass, 1);
    if (record == nullptr)
    {
        return mrhi_errorCapacity;
    }
    *record = (mrhiCommand){.type = mrhiCommandBeginStatisticsQuery, .a = query, .b = slot->handle};
    return mrhi_success;
}

mrhiResult mrhiEndStatisticsQuery(mrhiDevice* device, mrhiPassId id)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = mrhi_success;
    mrhiFramePass* pass = mrhiRecordingPass(device, id, &status);
    if (pass == nullptr)
    {
        return status;
    }
    if (!pass->statisticsOpen)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticStatisticsQuery);
    }
    pass->statisticsOpen = false;
    mrhiCommand* record = mrhiTakeCommands(device, pass, 1);
    if (record == nullptr)
    {
        return mrhi_errorCapacity;
    }
    *record = (mrhiCommand){
        .type = mrhiCommandEndStatisticsQuery,
        .a = pass->statisticsQuery,
        .b = pass->statisticsSet,
    };
    return mrhi_success;
}

mrhiResult mrhiResolveQueries(mrhiDevice* device, mrhiPassId id, mrhiQuerySetId set, uint32_t first,
                              uint32_t count, mrhiResourceId resource, uint64_t offset)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = mrhi_success;
    mrhiFramePass* pass = mrhiRecordingPass(device, id, &status);
    if (pass == nullptr)
    {
        return status;
    }
    uint32_t object = mrhiFindFrameResource(device, resource);
    if (!mrhiPoolIsLive(&device->querySets, set.index1, set.generation) || object == 0)
    {
        return mrhi_errorStale;
    }
    // Only a graphics pass declares a buffer with the query resolve access
    // (mrhi-0012), so the pass is one without targets of that class.
    if (mrhiWorkOf(pass) != mrhiWorkCompute)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticComputeOutsideComputePass);
    }
    const mrhiQuerySetSlot* slot = &device->querySetSlots[set.index1 - 1];
    uint64_t total = mrhiBufferBytesOf(&device->frameResources[object - 1]);
    uint64_t bytes = mrhiQueryBytes(slot->type);
    mrhiDiagnosticCode fault = mrhiRangeFault(
        offset % 256 == 0,
        first < slot->count && count <= slot->count - first && offset <= total &&
            count * bytes <= total - offset,
        mrhiPassDeclares(device, pass, object, MRHI_KIND(mrhi_accessQueryResolve), nullptr));
    if (fault != 0)
    {
        return mrhiDeviceMisuse(device, fault);
    }
    mrhiCommand* record = mrhiTakeCommands(device, pass, 1);
    if (record == nullptr)
    {
        return mrhi_errorCapacity;
    }
    *record = (mrhiCommand){
        .type = mrhiCommandResolveQueries,
        .a = object,
        .b = slot->handle,
        .c = first | (uint64_t)count << 32,
        .d = offset,
    };
    return mrhi_success;
}
