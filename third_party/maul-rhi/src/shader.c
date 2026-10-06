// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Shader containers on a device (mrhi-0009): the container checked as
// hostile input and against the device, its reflection kept for the
// shader and its pipelines, and its code handed to the driver.

#include "device_core.h"

#include <string.h>

#define SHADER_DEF_COOKIE 0x6D727368u

mrhiShaderDef mrhiDefaultShaderDef(void)
{
    mrhiShaderDef def = {0};
    def.cookie = SHADER_DEF_COOKIE;
    return def;
}

// Whether a container's bindings fit the device's limits: slots, binding
// sizes, and each stage's count of each kind; and each of its four
// tables MRHI_TABLE_BINDINGS, so that a table set while recording fits
// one chunk. Tables and color outputs need no check: the floor's four
// tables and eight color attachments are the container's own bounds.
static bool AreBindingsWithin(const mrhiLimits* limits, const mrhiContainer* container)
{
    for (uint32_t stage = mrhi_stageVertex; stage <= mrhi_stageCompute; stage <<= 1)
    {
        uint32_t counts[mrhi_bindingStorageTexture + 1] = {0};
        for (uint32_t i = 0; i < container->bindingCount; ++i)
        {
            mrhiShaderBinding binding = mrhiContainerBinding(container, i);
            counts[binding.kind] += (binding.stages & stage) != 0 ? 1 : 0;
        }
        if (counts[mrhi_bindingUniformBuffer] > limits->uniformBuffersPerStage ||
            counts[mrhi_bindingStorageBuffer] + counts[mrhi_bindingReadOnlyStorageBuffer] >
                limits->storageBuffersPerStage ||
            counts[mrhi_bindingSampler] > limits->samplersPerStage ||
            counts[mrhi_bindingSampledTexture] > limits->sampledTexturesPerStage ||
            counts[mrhi_bindingStorageTexture] > limits->storageTexturesPerStage)
        {
            return false;
        }
    }
    uint32_t tables[4] = {0};
    for (uint32_t i = 0; i < container->bindingCount; ++i)
    {
        mrhiShaderBinding binding = mrhiContainerBinding(container, i);
        if (++tables[binding.table] > MRHI_TABLE_BINDINGS)
        {
            return false;
        }
        uint64_t bytes = binding.kind == mrhi_bindingUniformBuffer ? limits->uniformBindingBytes
                                                                   : limits->storageBindingBytes;
        if (binding.slot >= limits->bindingsPerTable || binding.minSize > bytes)
        {
            return false;
        }
    }
    return true;
}

// The fragment builtins an entry reads that count as inter-stage
// variables.
static uint32_t StageBuiltins(mrhiShaderBuiltins builtins)
{
    const mrhiShaderBuiltins counted[] = {mrhi_builtinFrontFacing, mrhi_builtinSampleIndex,
                                          mrhi_builtinSampleMaskIn, mrhi_builtinPrimitiveIndex};
    uint32_t count = 0;
    for (size_t i = 0; i < sizeof(counted) / sizeof(counted[0]); ++i)
    {
        count += (builtins & counted[i]) != 0 ? 1 : 0;
    }
    return count;
}

// Whether an entry fits the device's limits: a compute entry's
// workgroup, and the vertex inputs and inter-stage variables of the
// others, with the fragment builtins that count as variables.
static bool IsEntryWithin(const mrhiLimits* limits, const mrhiContainer* container,
                          mrhiShaderEntry entry)
{
    if (entry.stage == mrhi_stageCompute)
    {
        uint64_t invocations = (uint64_t)entry.workgroup[0] * entry.workgroup[1];
        return entry.workgroup[0] <= limits->workgroupSizeX &&
               entry.workgroup[1] <= limits->workgroupSizeY &&
               entry.workgroup[2] <= limits->workgroupSizeZ &&
               invocations * entry.workgroup[2] <= limits->workgroupInvocations &&
               entry.workgroupStorageBytes <= limits->workgroupStorageBytes;
    }
    // Unique input locations below the limit keep their count within it.
    if (entry.variableCount + StageBuiltins(entry.builtins) > limits->interStageVariables)
    {
        return false;
    }
    for (uint32_t i = 0; i < entry.inputCount; ++i)
    {
        if (mrhiContainerInput(container, entry.firstInput + i).location >=
            limits->vertexAttributes)
        {
            return false;
        }
    }
    for (uint32_t i = 0; i < entry.variableCount; ++i)
    {
        if (mrhiContainerVariable(container, entry.firstVariable + i).location >=
            limits->interStageVariables)
        {
            return false;
        }
    }
    return true;
}

// Whether a container fits the device: its root block, bindings and
// entries within the limits, and the features its code needs.
static bool IsWithin(const mrhiDevice* device, const mrhiContainer* container)
{
    if (container->rootBlockBytes > device->limits.rootBlockBytes ||
        (container->float16 && !device->features.shaderF16) ||
        (container->builtins & mrhi_builtinPrimitiveIndex) != 0 ||
        (container->heapUses != 0 && !device->features.bindlessSampling) ||
        ((container->builtins & mrhi_builtinViewIndex) != 0 && !device->features.multiview) ||
        ((container->heapUses & (mrhi_heapUseStorageTextures | mrhi_heapUseStorageBuffers)) != 0 &&
         !device->features.bindlessHeterogeneous) ||
        !AreBindingsWithin(&device->limits, container))
    {
        return false;
    }
    for (uint32_t i = 0; i < container->entryCount; ++i)
    {
        if (!IsEntryWithin(&device->limits, container, mrhiContainerEntry(container, i)))
        {
            return false;
        }
    }
    return true;
}

// Checks a def and its container: success with the container, or the
// refusal, invalid input counted as misuse.
static mrhiResult CheckShaderDef(mrhiDevice* device, const mrhiShaderDef* def,
                                 mrhiContainer* containerOut)
{
    mrhiResult status = mrhiCheckObjectDef(device, MRHI_DEF_HEAD(def), SHADER_DEF_COOKIE);
    if (status != mrhi_success)
    {
        return status;
    }
    if (def->bytes == nullptr || (uintptr_t)def->bytes % 8 != 0)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticShaderBytes);
    }
    status = mrhiParseContainer(def->bytes, def->byteCount, containerOut);
    if (status == mrhi_errorInvalid)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticShaderContainer);
    }
    return status == mrhi_success && !IsWithin(device, containerOut) ? mrhi_errorUnsupported
                                                                     : status;
}

mrhiResult mrhiCreateShader(mrhiDevice* device, const mrhiShaderDef* def, mrhiShaderId* shaderOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (def == nullptr || shaderOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    mrhiContainer container = {0};
    mrhiResult status = CheckShaderDef(device, def, &container);
    if (status != mrhi_success)
    {
        return status;
    }
    status = mrhiDeviceUsable(device);
    if (status != mrhi_success)
    {
        return status;
    }
    uint32_t index1 = 0;
    uint32_t generation = 0;
    if (!mrhiPoolAcquire(&device->shaders, &index1, &generation))
    {
        return mrhi_errorCapacity;
    }
    mrhiShaderSlot* slot = &device->shaderSlots[index1 - 1];
    slot->reflection = mrhiKeepReflection(&device->allocator, &container);
    if (slot->reflection == nullptr)
    {
        mrhiPoolRelease(&device->shaders, index1);
        return mrhi_errorCapacity;
    }
    status = mrhiDriverStatus(device, device->driver.vtable->createShader(
                                          device->driver.self, def, &container, &slot->handle));
    if (status != mrhi_success)
    {
        mrhiReleaseReflection(&device->allocator, slot->reflection);
        *slot = (mrhiShaderSlot){0};
        mrhiPoolRelease(&device->shaders, index1);
        return status;
    }
    *shaderOut = (mrhiShaderId){index1, generation};
    return mrhi_success;
}

mrhiResult mrhiDestroyShader(mrhiDevice* device, mrhiShaderId shader)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (!mrhiPoolIsLive(&device->shaders, shader.index1, shader.generation))
    {
        return mrhi_errorStale;
    }
    mrhiShaderSlot* slot = &device->shaderSlots[shader.index1 - 1];
    device->driver.vtable->destroyShader(device->driver.self, slot->handle);
    mrhiReleaseReflection(&device->allocator, slot->reflection);
    *slot = (mrhiShaderSlot){0};
    mrhiPoolRelease(&device->shaders, shader.index1);
    return mrhi_success;
}

mrhiResult mrhiGetShaderInfo(mrhiDevice* device, mrhiShaderId shader, mrhiShaderInfo* infoOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (infoOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    if (!mrhiPoolIsLive(&device->shaders, shader.index1, shader.generation))
    {
        return mrhi_errorStale;
    }
    const mrhiReflection* reflection = device->shaderSlots[shader.index1 - 1].reflection;
    *infoOut = (mrhiShaderInfo){
        .entryCount = reflection->entryCount,
        .bindingCount = reflection->bindingCount,
        .rootBlockBytes = reflection->rootBlockBytes,
        .heapUses = reflection->heapUses,
    };
    memcpy(infoOut->digest, reflection->digest, MRHI_DIGEST_BYTES);
    return mrhi_success;
}

void mrhiDestroyShaders(mrhiDevice* device)
{
    for (uint32_t i = 0; i < device->deviceLimits.shaders; ++i)
    {
        mrhiShaderSlot* slot = &device->shaderSlots[i];
        if (slot->reflection != nullptr)
        {
            device->driver.vtable->destroyShader(device->driver.self, slot->handle);
            mrhiReleaseReflection(&device->allocator, slot->reflection);
            *slot = (mrhiShaderSlot){0};
        }
    }
}
