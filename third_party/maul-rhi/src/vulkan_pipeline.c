// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Vulkan device's shaders, pipeline layouts, compute pipelines,
// answers and cache (vulkan_pipeline.h). Pipelines are made at the call,
// with no thread (family record 0017), and answered at the next
// poll as the contract's requests are.

#include "vulkan_pipeline.h"

#include "container.h"
#include "invariant.h"
#include "reflection.h"
#include "vulkan_label.h"

#include <string.h>

mrhiResult mrhiVulkanStatus(VkResult result)
{
    switch (result)
    {
    case VK_ERROR_OUT_OF_HOST_MEMORY:
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return mrhi_errorCapacity;
    case VK_ERROR_DEVICE_LOST:
        return mrhi_errorDeviceLost;
    default:
        return mrhi_errorPlatform;
    }
}

mrhiResult mrhiVulkanPipelinesInit(mrhiVulkanPipelines* pipelines)
{
    const VkPipelineCacheCreateInfo info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    VkResult result =
        pipelines->api->vkCreatePipelineCache(pipelines->device, &info, nullptr, &pipelines->cache);
    return result == VK_SUCCESS ? mrhi_success : mrhiVulkanStatus(result);
}

static void DestroyPipeline(mrhiVulkanPipelines* pipelines, mrhiVulkanPipeline* pipeline)
{
    pipelines->api->vkDestroyPipeline(pipelines->device, pipeline->pipeline, nullptr);
    mrhiVulkanDropLayout(pipelines, pipeline);
    *pipeline = (mrhiVulkanPipeline){0};
}

void mrhiVulkanPipelinesDestroy(mrhiVulkanPipelines* pipelines)
{
    for (uint32_t i = 0; i < pipelines->pipelineSlots.capacity; ++i)
    {
        DestroyPipeline(pipelines, &pipelines->pipelines[i]);
    }
    for (uint32_t i = 0; i < pipelines->shaderSlots.capacity; ++i)
    {
        pipelines->api->vkDestroyShaderModule(pipelines->device, pipelines->shaders[i], nullptr);
    }
    pipelines->api->vkDestroyPipelineCache(pipelines->device, pipelines->cache, nullptr);
}

mrhiResult mrhiVulkanCreateShader(mrhiVulkanPipelines* pipelines, const mrhiShaderDef* def,
                                  const mrhiContainer* container, uint64_t* handleOut)
{
    uint32_t handle = mrhiVulkanTakeSlot(&pipelines->shaderSlots);
    if (handle == 0)
    {
        return mrhi_errorCapacity;
    }
    // The container is 8-byte aligned and its sections too, so its SPIR-V
    // is read as words where it lies.
    const VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = container->spirvBytes,
        .pCode = (const uint32_t*)container->spirv,
    };
    VkResult result = pipelines->api->vkCreateShaderModule(pipelines->device, &info, nullptr,
                                                           &pipelines->shaders[handle - 1]);
    if (result != VK_SUCCESS)
    {
        mrhiVulkanGiveSlot(&pipelines->shaderSlots, handle);
        return mrhiVulkanStatus(result);
    }
    mrhiVulkanName(pipelines->api, pipelines->device, VK_OBJECT_TYPE_SHADER_MODULE,
                   MRHI_VULKAN_HANDLE(pipelines->shaders[handle - 1]), def->label,
                   def->labelLength);
    *handleOut = handle;
    return mrhi_success;
}

void mrhiVulkanDestroyShader(mrhiVulkanPipelines* pipelines, uint64_t handle)
{
    pipelines->api->vkDestroyShaderModule(pipelines->device, pipelines->shaders[handle - 1],
                                          nullptr);
    pipelines->shaders[handle - 1] = VK_NULL_HANDLE;
    mrhiVulkanGiveSlot(&pipelines->shaderSlots, handle);
}

static VkDescriptorType DescriptorOf(mrhiBindingKind kind)
{
    switch (kind)
    {
    case mrhi_bindingUniformBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case mrhi_bindingStorageBuffer:
    case mrhi_bindingReadOnlyStorageBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case mrhi_bindingSampler:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    case mrhi_bindingSampledTexture:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    default:
        MRHI_ASSERT(kind == mrhi_bindingStorageTexture);
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    }
}

static VkShaderStageFlags StagesOf(mrhiShaderStages stages)
{
    VkShaderStageFlags flags = 0;
    flags |= (stages & mrhi_stageVertex) != 0 ? VK_SHADER_STAGE_VERTEX_BIT : 0;
    flags |= (stages & mrhi_stageFragment) != 0 ? VK_SHADER_STAGE_FRAGMENT_BIT : 0;
    flags |= (stages & mrhi_stageCompute) != 0 ? VK_SHADER_STAGE_COMPUTE_BIT : 0;
    return flags;
}

// Makes one binding table's set layout.
static VkResult MakeSet(mrhiVulkanPipelines* pipelines, const mrhiReflection* reflection,
                        uint32_t table, VkDescriptorSetLayout* setOut)
{
    VkDescriptorSetLayoutBinding bindings[MRHI_TABLE_BINDINGS];
    uint32_t count = 0;
    for (uint32_t i = 0; i < reflection->bindingCount; ++i)
    {
        const mrhiShaderBinding* binding = &reflection->bindings[i];
        if (binding->table == table)
        {
            MRHI_ASSERT(count < MRHI_TABLE_BINDINGS);
            bindings[count++] = (VkDescriptorSetLayoutBinding){
                .binding = binding->slot,
                .descriptorType = DescriptorOf(binding->kind),
                .descriptorCount = 1,
                .stageFlags = StagesOf(binding->stages),
            };
        }
    }
    const VkDescriptorSetLayoutCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = count,
        .pBindings = bindings,
    };
    return pipelines->api->vkCreateDescriptorSetLayout(pipelines->device, &info, nullptr, setOut);
}

mrhiResult mrhiVulkanMakeLayout(mrhiVulkanPipelines* pipelines, const mrhiReflection* reflection,
                                VkShaderStageFlags rootStages, mrhiVulkanPipeline* pipelineOut)
{
    *pipelineOut = (mrhiVulkanPipeline){0};
    // A container reading heaps has the heap's set after every table's,
    // empty layouts standing for the tables it lacks.
    bool heap = reflection->heapUses != 0;
    MRHI_ASSERT(!heap || pipelines->heapLayout != VK_NULL_HANDLE);
    uint32_t tables = heap ? MRHI_VULKAN_TABLES : 0;
    for (uint32_t i = 0; i < reflection->bindingCount; ++i)
    {
        uint32_t table = reflection->bindings[i].table + 1u;
        tables = table > tables ? table : tables;
    }
    VkResult result = VK_SUCCESS;
    for (uint32_t t = 0; t < tables && result == VK_SUCCESS; ++t)
    {
        result = MakeSet(pipelines, reflection, t, &pipelineOut->sets[t]);
        pipelineOut->setCount += result == VK_SUCCESS ? 1 : 0;
    }
    pipelineOut->heap = heap;
    pipelineOut->sets[MRHI_VULKAN_TABLES] = pipelines->heapLayout;
    pipelineOut->rootStages = reflection->rootBlockBytes > 0 ? rootStages : 0;
    const VkPushConstantRange root = {
        .stageFlags = rootStages,
        .size = reflection->rootBlockBytes,
    };
    const VkPipelineLayoutCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = pipelineOut->setCount + (heap ? 1u : 0u),
        .pSetLayouts = pipelineOut->sets,
        .pushConstantRangeCount = reflection->rootBlockBytes > 0 ? 1 : 0,
        .pPushConstantRanges = &root,
    };
    if (result == VK_SUCCESS)
    {
        result = pipelines->api->vkCreatePipelineLayout(pipelines->device, &info, nullptr,
                                                        &pipelineOut->layout);
    }
    if (result != VK_SUCCESS)
    {
        mrhiVulkanDropLayout(pipelines, pipelineOut);
        return mrhiVulkanStatus(result);
    }
    return mrhi_success;
}

void mrhiVulkanDropLayout(mrhiVulkanPipelines* pipelines, mrhiVulkanPipeline* pipeline)
{
    pipelines->api->vkDestroyPipelineLayout(pipelines->device, pipeline->layout, nullptr);
    for (uint32_t t = 0; t < pipeline->setCount; ++t)
    {
        pipelines->api->vkDestroyDescriptorSetLayout(pipelines->device, pipeline->sets[t], nullptr);
    }
    pipeline->layout = VK_NULL_HANDLE;
    pipeline->setCount = 0;
}

// A constant's value in its type's 32 bits, as the core checked it.
static uint32_t BitsOf(mrhiConstantType type, double value)
{
    switch (type)
    {
    case mrhi_constantBool:
        return value != 0.0 ? 1u : 0u;
    case mrhi_constantInt32:
        return (uint32_t)(int32_t)value;
    case mrhi_constantUint32:
        return (uint32_t)value;
    default:
    {
        float single = (float)value;
        uint32_t bits = 0;
        memcpy(&bits, &single, sizeof(bits));
        return bits;
    }
    }
}

void mrhiVulkanSpecialize(mrhiVulkanPipelines* pipelines, const mrhiReflection* reflection,
                          const mrhiConstantValue* values, uint32_t count,
                          VkSpecializationInfo* infoOut)
{
    mrhiVulkanConstants* room = pipelines->constants;
    MRHI_ASSERT(count <= MRHI_VULKAN_CONSTANTS);
    for (uint32_t i = 0; i < count; ++i)
    {
        mrhiConstantType type = mrhi_constantNone;
        for (uint32_t c = 0; c < reflection->constantCount && type == mrhi_constantNone; ++c)
        {
            type =
                reflection->constants[c].id == values[i].id ? reflection->constants[c].type : type;
        }
        MRHI_ASSERT(type != mrhi_constantNone);
        room->data[i] = BitsOf(type, values[i].value);
        room->entries[i] = (VkSpecializationMapEntry){
            .constantID = values[i].id,
            .offset = i * (uint32_t)sizeof(uint32_t),
            .size = sizeof(uint32_t),
        };
    }
    *infoOut = (VkSpecializationInfo){
        .mapEntryCount = count,
        .pMapEntries = room->entries,
        .dataSize = count * sizeof(uint32_t),
        .pData = room->data,
    };
}

void mrhiVulkanEntryName(const mrhiReflection* reflection, uint32_t entry, char* nameOut)
{
    const mrhiShaderEntry* found = &reflection->entries[entry];
    MRHI_ASSERT(found->nameLength < MRHI_VULKAN_NAME);
    memcpy(nameOut, reflection->names + found->nameOffset, found->nameLength);
    nameOut[found->nameLength] = '\0';
}

uint32_t mrhiVulkanTakePipeline(mrhiVulkanPipelines* pipelines)
{
    return mrhiVulkanTakeSlot(&pipelines->pipelineSlots);
}

void mrhiVulkanGivePipeline(mrhiVulkanPipelines* pipelines, uint32_t handle)
{
    mrhiVulkanGiveSlot(&pipelines->pipelineSlots, handle);
}

void mrhiVulkanAnswer(mrhiVulkanPipelines* pipelines, uint32_t handle, uint64_t tag)
{
    // One answer per pipeline slot at most, so the queue never fills.
    MRHI_ASSERT(pipelines->pendingCount < pipelines->pipelineSlots.capacity);
    pipelines->pending[pipelines->pendingCount] =
        (mrhiDriverEvent){.tag = tag, .outcome = mrhi_success};
    pipelines->pendingHandles[pipelines->pendingCount] = handle;
    ++pipelines->pendingCount;
}

mrhiResult mrhiVulkanCreateCompute(mrhiVulkanPipelines* pipelines,
                                   const mrhiDriverComputePipeline* pipeline, uint64_t tag,
                                   uint64_t* handleOut)
{
    uint32_t handle = mrhiVulkanTakePipeline(pipelines);
    if (handle == 0)
    {
        return mrhi_errorCapacity;
    }
    mrhiVulkanPipeline* made = &pipelines->pipelines[handle - 1];
    mrhiResult status =
        mrhiVulkanMakeLayout(pipelines, pipeline->reflection, VK_SHADER_STAGE_COMPUTE_BIT, made);
    if (status != mrhi_success)
    {
        mrhiVulkanGivePipeline(pipelines, handle);
        return status;
    }
    char name[MRHI_VULKAN_NAME];
    mrhiVulkanEntryName(pipeline->reflection, pipeline->entry, name);
    VkSpecializationInfo constants;
    mrhiVulkanSpecialize(pipelines, pipeline->reflection, pipeline->constants,
                         pipeline->constantCount, &constants);
    const VkComputePipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage =
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = pipelines->shaders[pipeline->shader - 1],
                .pName = name,
                .pSpecializationInfo = &constants,
            },
        .layout = made->layout,
    };
    VkResult result = pipelines->api->vkCreateComputePipelines(pipelines->device, pipelines->cache,
                                                               1, &info, nullptr, &made->pipeline);
    if (result != VK_SUCCESS)
    {
        mrhiVulkanDropLayout(pipelines, made);
        mrhiVulkanGivePipeline(pipelines, handle);
        return mrhiVulkanStatus(result);
    }
    made->bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
    mrhiVulkanName(pipelines->api, pipelines->device, VK_OBJECT_TYPE_PIPELINE,
                   MRHI_VULKAN_HANDLE(made->pipeline), pipeline->label, pipeline->labelLength);
    mrhiVulkanAnswer(pipelines, handle, tag);
    *handleOut = handle;
    return mrhi_success;
}

void mrhiVulkanForgetPipeline(mrhiVulkanPipelines* pipelines, uint64_t handle)
{
    for (uint32_t i = 0; i < pipelines->pendingCount; ++i)
    {
        if (pipelines->pendingHandles[i] == handle)
        {
            --pipelines->pendingCount;
            memmove(&pipelines->pending[i], &pipelines->pending[i + 1],
                    (pipelines->pendingCount - i) * sizeof(mrhiDriverEvent));
            memmove(&pipelines->pendingHandles[i], &pipelines->pendingHandles[i + 1],
                    (pipelines->pendingCount - i) * sizeof(uint64_t));
            break;
        }
    }
}

void mrhiVulkanDestroyPipeline(mrhiVulkanPipelines* pipelines, uint64_t handle)
{
    mrhiVulkanForgetPipeline(pipelines, handle);
    DestroyPipeline(pipelines, &pipelines->pipelines[handle - 1]);
    mrhiVulkanGivePipeline(pipelines, (uint32_t)handle);
}

size_t mrhiVulkanPollPipelines(mrhiVulkanPipelines* pipelines, mrhiDriverEvent* events,
                               size_t capacity)
{
    size_t moved = pipelines->pendingCount < capacity ? pipelines->pendingCount : capacity;
    memcpy(events, pipelines->pending, moved * sizeof(mrhiDriverEvent));
    pipelines->pendingCount -= (uint32_t)moved;
    memmove(pipelines->pending, pipelines->pending + moved,
            pipelines->pendingCount * sizeof(mrhiDriverEvent));
    memmove(pipelines->pendingHandles, pipelines->pendingHandles + moved,
            pipelines->pendingCount * sizeof(uint64_t));
    return moved;
}

bool mrhiVulkanImportCache(mrhiVulkanPipelines* pipelines, const void* bytes, size_t size)
{
    // Vulkan's own header opens every blob: its size and version, then
    // the vendor, the device and the cache's UUID.
    VkPipelineCacheHeaderVersionOne header;
    if (size < sizeof(header))
    {
        return false;
    }
    memcpy(&header, bytes, sizeof(header));
    const VkPhysicalDeviceProperties* properties = &pipelines->properties;
    if (header.headerSize < sizeof(header) || header.headerSize > size ||
        header.headerVersion != VK_PIPELINE_CACHE_HEADER_VERSION_ONE ||
        header.vendorID != properties->vendorID || header.deviceID != properties->deviceID ||
        memcmp(header.pipelineCacheUUID, properties->pipelineCacheUUID, VK_UUID_SIZE) != 0)
    {
        return false;
    }
    const VkPipelineCacheCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
        .initialDataSize = size,
        .pInitialData = bytes,
    };
    VkPipelineCache cache = VK_NULL_HANDLE;
    if (pipelines->api->vkCreatePipelineCache(pipelines->device, &info, nullptr, &cache) !=
        VK_SUCCESS)
    {
        return false;
    }
    pipelines->api->vkDestroyPipelineCache(pipelines->device, pipelines->cache, nullptr);
    pipelines->cache = cache;
    return true;
}

size_t mrhiVulkanExportCache(mrhiVulkanPipelines* pipelines, void* bytes, size_t capacity)
{
    size_t size = 0;
    if (pipelines->api->vkGetPipelineCacheData(pipelines->device, pipelines->cache, &size,
                                               nullptr) != VK_SUCCESS)
    {
        return 0;
    }
    if (bytes != nullptr && capacity >= size)
    {
        VkResult result = pipelines->api->vkGetPipelineCacheData(pipelines->device,
                                                                 pipelines->cache, &size, bytes);
        return result == VK_SUCCESS ? size : 0;
    }
    return size;
}
