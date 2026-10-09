// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Pipelines (mrhi-0009): checked against their shader's reflection,
// started in the driver with a tag that carries their slot, and answered
// in the device's queue, whose room each creation reserves.

#include "invariant.h"
#include "label.h"
#include "pipeline_core.h"

#include <float.h>

#define COMPUTE_PIPELINE_DEF_COOKIE 0x6D726370u

mrhiComputePipelineDef mrhiDefaultComputePipelineDef(void)
{
    mrhiComputePipelineDef def = {0};
    def.cookie = COMPUTE_PIPELINE_DEF_COOKIE;
    return def;
}

// Whether a value converts to a constant's type exactly: a boolean 0 or
// 1, an integer in its type's range, or a finite float within 32-bit
// range. NaN fails every comparison.
static bool IsValueValid(mrhiConstantType type, double value)
{
    switch (type)
    {
    case mrhi_constantBool:
        return value == 0.0 || value == 1.0;
    case mrhi_constantInt32:
        return value >= -2147483648.0 && value <= 2147483647.0 && (double)(int32_t)value == value;
    case mrhi_constantUint32:
        return value >= 0.0 && value <= 4294967295.0 && (double)(uint32_t)value == value;
    default:
        return value >= -(double)FLT_MAX && value <= (double)FLT_MAX;
    }
}

// The index of a constant id in the reflection, or constantCount.
static uint32_t FindConstant(const mrhiReflection* reflection, uint32_t id)
{
    uint32_t i = 0;
    while (i < reflection->constantCount && reflection->constants[i].id != id)
    {
        ++i;
    }
    return i;
}

bool mrhiAreConstantsValid(const mrhiReflection* reflection, const mrhiConstantValue* values,
                           uint32_t count)
{
    if (count > reflection->constantCount || (values == nullptr && count > 0))
    {
        return false;
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t at = FindConstant(reflection, values[i].id);
        if (at == reflection->constantCount ||
            !IsValueValid(reflection->constants[at].type, values[i].value))
        {
            return false;
        }
        for (uint32_t j = 0; j < i; ++j)
        {
            if (values[j].id == values[i].id)
            {
                return false;
            }
        }
    }
    for (uint32_t i = 0; i < reflection->constantCount; ++i)
    {
        bool given = false;
        for (uint32_t j = 0; j < count && !given; ++j)
        {
            given = values[j].id == reflection->constants[i].id;
        }
        if (reflection->constants[i].required && !given)
        {
            return false;
        }
    }
    return true;
}

mrhiResult mrhiTakePipelineSlot(mrhiDevice* device, mrhiPipelineKind kind,
                                mrhiReflection* reflection, uint32_t* index1Out,
                                uint32_t* generationOut)
{
    if (!mrhiHasAnswerRoom(device) ||
        !mrhiPoolAcquire(&device->pipelines, index1Out, generationOut))
    {
        return mrhi_errorCapacity;
    }
    ++reflection->references;
    device->pipelineSlots[*index1Out - 1] = (mrhiPipelineSlot){
        .kind = kind,
        .state = mrhiPipelinePending,
        .request = device->lastRequest + 1,
        .reflection = reflection,
    };
    return mrhi_success;
}

void mrhiFreePipelineSlot(mrhiDevice* device, uint32_t index1)
{
    mrhiPipelineSlot* slot = &device->pipelineSlots[index1 - 1];
    mrhiReleaseReflection(&device->allocator, slot->reflection);
    *slot = (mrhiPipelineSlot){0};
    mrhiPoolRelease(&device->pipelines, index1);
}

mrhiRequestId mrhiStartPipeline(mrhiDevice* device, uint32_t index1)
{
    ++device->pendingCount;
    device->lastRequest = device->pipelineSlots[index1 - 1].request;
    return (mrhiRequestId){device->lastRequest, 1};
}

uint64_t mrhiPipelineTag(const mrhiDevice* device, uint32_t index1)
{
    return (uint64_t)index1 << 32 | device->pipelineSlots[index1 - 1].request;
}

mrhiShaderSlot* mrhiCheckPipelineHead(mrhiDevice* device, mrhiDefHead head, uint32_t cookie,
                                      mrhiShaderId shader, mrhiResult* statusOut)
{
    *statusOut = mrhiCheckObjectDef(device, head, cookie);
    if (*statusOut == mrhi_success)
    {
        *statusOut = mrhiDeviceUsable(device);
    }
    if (*statusOut != mrhi_success)
    {
        return nullptr;
    }
    if (!mrhiPoolIsLive(&device->shaders, shader.index1, shader.generation))
    {
        *statusOut = mrhi_errorStale;
        return nullptr;
    }
    return &device->shaderSlots[shader.index1 - 1];
}

// Checks a compute pipeline def: the shader, with the entry's index, or
// NULL with the refusal, invalid input counted as misuse.
static mrhiShaderSlot* CheckComputeDef(mrhiDevice* device, const mrhiComputePipelineDef* def,
                                       uint32_t* entryOut, mrhiResult* statusOut)
{
    mrhiShaderSlot* shader = mrhiCheckPipelineHead(
        device, MRHI_DEF_HEAD(def), COMPUTE_PIPELINE_DEF_COOKIE, def->shader, statusOut);
    if (shader == nullptr)
    {
        return nullptr;
    }
    const mrhiReflection* reflection = shader->reflection;
    *entryOut = def->entry == nullptr
                    ? reflection->entryCount
                    : mrhiFindEntry(reflection, def->entry, def->entryLength, mrhi_stageCompute);
    if (*entryOut == reflection->entryCount)
    {
        *statusOut = mrhiDeviceMisuse(device, mrhi_diagnosticPipelineEntry);
        return nullptr;
    }
    if (!mrhiAreConstantsValid(reflection, def->constants, def->constantCount))
    {
        *statusOut = mrhiDeviceMisuse(device, mrhi_diagnosticPipelineConstants);
        return nullptr;
    }
    return shader;
}

mrhiResult mrhiCreateComputePipeline(mrhiDevice* device, const mrhiComputePipelineDef* def,
                                     mrhiComputePipelineId* pipelineOut, mrhiRequestId* requestOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (def == nullptr || pipelineOut == nullptr || requestOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    uint32_t entry = 0;
    mrhiResult status = mrhi_success;
    mrhiShaderSlot* shader = CheckComputeDef(device, def, &entry, &status);
    if (shader == nullptr)
    {
        return status;
    }
    uint32_t index1 = 0;
    uint32_t generation = 0;
    status =
        mrhiTakePipelineSlot(device, mrhiPipelineCompute, shader->reflection, &index1, &generation);
    if (status != mrhi_success)
    {
        return status;
    }
    mrhiPipelineSlot* slot = &device->pipelineSlots[index1 - 1];
    slot->entries[0] = entry;
    slot->heapUses = shader->reflection->entries[entry].heapUses;
    mrhiDriverComputePipeline pipeline = {
        .label = MAUL_RHI_LABELS ? def->label : nullptr,
        .labelLength = MAUL_RHI_LABELS ? def->labelLength : 0,
        .shader = shader->handle,
        .reflection = shader->reflection,
        .entry = entry,
        .constants = def->constants,
        .constantCount = def->constantCount,
    };
    status = mrhiDriverStatus(device, device->driver.vtable->createComputePipeline(
                                          device->driver.self, &pipeline,
                                          mrhiPipelineTag(device, index1), &slot->handle));
    if (status != mrhi_success)
    {
        mrhiFreePipelineSlot(device, index1);
        return status;
    }
    *pipelineOut = (mrhiComputePipelineId){index1, generation};
    *requestOut = mrhiStartPipeline(device, index1);
    return mrhi_success;
}

mrhiResult mrhiDestroyPipeline(mrhiDevice* device, mrhiPipelineKind kind, uint32_t index1,
                               uint32_t generation)
{
    if (!mrhiPoolIsLive(&device->pipelines, index1, generation) ||
        device->pipelineSlots[index1 - 1].kind != kind)
    {
        return mrhi_errorStale;
    }
    mrhiPipelineSlot* slot = &device->pipelineSlots[index1 - 1];
    if (slot->state == mrhiPipelinePending)
    {
        --device->pendingCount;
        mrhiQueueAnswer(device, mrhi_devicePipelineReady, slot->request, mrhi_errorStale);
    }
    device->driver.vtable->destroyPipeline(device->driver.self, slot->handle);
    mrhiFreePipelineSlot(device, index1);
    return mrhi_success;
}

mrhiResult mrhiDestroyComputePipeline(mrhiDevice* device, mrhiComputePipelineId pipeline)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    return mrhiDestroyPipeline(device, mrhiPipelineCompute, pipeline.index1, pipeline.generation);
}

void mrhiLosePipelines(mrhiDevice* device)
{
    for (uint32_t i = 0; i < device->deviceLimits.pipelines && device->pendingCount > 0; ++i)
    {
        // A free slot is zeroed, which reads as pending.
        mrhiPipelineSlot* slot = &device->pipelineSlots[i];
        bool live = mrhiPoolIsLive(&device->pipelines, i + 1, device->pipelines.generations[i]);
        if (live && slot->state == mrhiPipelinePending)
        {
            slot->state = mrhiPipelineFailed;
            --device->pendingCount;
            mrhiQueueAnswer(device, mrhi_devicePipelineReady, slot->request, mrhi_errorDeviceLost);
        }
    }
}

void mrhiFinishPipeline(mrhiDevice* device, uint64_t tag, mrhiResult outcome)
{
    uint32_t index1 = (uint32_t)(tag >> 32);
    uint32_t request = (uint32_t)tag;
    // The driver reports only pipelines it was not asked to destroy.
    MRHI_ASSERT(index1 <= device->deviceLimits.pipelines);
    mrhiPipelineSlot* slot = &device->pipelineSlots[index1 - 1];
    MRHI_ASSERT(slot->state == mrhiPipelinePending && slot->request == request);
    slot->state = outcome == mrhi_success ? mrhiPipelineReady : mrhiPipelineFailed;
    --device->pendingCount;
    mrhiQueueAnswer(device, mrhi_devicePipelineReady, request, outcome);
}

void mrhiDestroyPipelines(mrhiDevice* device)
{
    for (uint32_t i = 0; i < device->deviceLimits.pipelines; ++i)
    {
        mrhiPipelineSlot* slot = &device->pipelineSlots[i];
        if (slot->reflection != nullptr)
        {
            device->driver.vtable->destroyPipeline(device->driver.self, slot->handle);
            mrhiReleaseReflection(&device->allocator, slot->reflection);
            *slot = (mrhiPipelineSlot){0};
        }
    }
}
