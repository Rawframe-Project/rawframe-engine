// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Graphics pipelines on Vulkan (vulkan_pipeline.h): a def the core has
// checked as WebGPU checks it, translated as the contract's Vulkan rows
// map it, for dynamic rendering, with the viewport, scissor, blend
// constant and stencil reference set while recording.

#include "capabilities_core.h"
#include "container.h"
#include "invariant.h"
#include "reflection.h"
#include "vulkan_adapter.h"
#include "vulkan_label.h"
#include "vulkan_pipeline.h"

// Each vertex format's Vulkan format, as its mapping row names it.
static const VkFormat s_vertexFormats[] = {
    [mrhi_vertexUint8] = VK_FORMAT_R8_UINT,
    [mrhi_vertexUint8x2] = VK_FORMAT_R8G8_UINT,
    [mrhi_vertexUint8x4] = VK_FORMAT_R8G8B8A8_UINT,
    [mrhi_vertexSint8] = VK_FORMAT_R8_SINT,
    [mrhi_vertexSint8x2] = VK_FORMAT_R8G8_SINT,
    [mrhi_vertexSint8x4] = VK_FORMAT_R8G8B8A8_SINT,
    [mrhi_vertexUnorm8] = VK_FORMAT_R8_UNORM,
    [mrhi_vertexUnorm8x2] = VK_FORMAT_R8G8_UNORM,
    [mrhi_vertexUnorm8x4] = VK_FORMAT_R8G8B8A8_UNORM,
    [mrhi_vertexSnorm8] = VK_FORMAT_R8_SNORM,
    [mrhi_vertexSnorm8x2] = VK_FORMAT_R8G8_SNORM,
    [mrhi_vertexSnorm8x4] = VK_FORMAT_R8G8B8A8_SNORM,
    [mrhi_vertexUint16] = VK_FORMAT_R16_UINT,
    [mrhi_vertexUint16x2] = VK_FORMAT_R16G16_UINT,
    [mrhi_vertexUint16x4] = VK_FORMAT_R16G16B16A16_UINT,
    [mrhi_vertexSint16] = VK_FORMAT_R16_SINT,
    [mrhi_vertexSint16x2] = VK_FORMAT_R16G16_SINT,
    [mrhi_vertexSint16x4] = VK_FORMAT_R16G16B16A16_SINT,
    [mrhi_vertexUnorm16] = VK_FORMAT_R16_UNORM,
    [mrhi_vertexUnorm16x2] = VK_FORMAT_R16G16_UNORM,
    [mrhi_vertexUnorm16x4] = VK_FORMAT_R16G16B16A16_UNORM,
    [mrhi_vertexSnorm16] = VK_FORMAT_R16_SNORM,
    [mrhi_vertexSnorm16x2] = VK_FORMAT_R16G16_SNORM,
    [mrhi_vertexSnorm16x4] = VK_FORMAT_R16G16B16A16_SNORM,
    [mrhi_vertexFloat16] = VK_FORMAT_R16_SFLOAT,
    [mrhi_vertexFloat16x2] = VK_FORMAT_R16G16_SFLOAT,
    [mrhi_vertexFloat16x4] = VK_FORMAT_R16G16B16A16_SFLOAT,
    [mrhi_vertexFloat32] = VK_FORMAT_R32_SFLOAT,
    [mrhi_vertexFloat32x2] = VK_FORMAT_R32G32_SFLOAT,
    [mrhi_vertexFloat32x3] = VK_FORMAT_R32G32B32_SFLOAT,
    [mrhi_vertexFloat32x4] = VK_FORMAT_R32G32B32A32_SFLOAT,
    [mrhi_vertexUint32] = VK_FORMAT_R32_UINT,
    [mrhi_vertexUint32x2] = VK_FORMAT_R32G32_UINT,
    [mrhi_vertexUint32x3] = VK_FORMAT_R32G32B32_UINT,
    [mrhi_vertexUint32x4] = VK_FORMAT_R32G32B32A32_UINT,
    [mrhi_vertexSint32] = VK_FORMAT_R32_SINT,
    [mrhi_vertexSint32x2] = VK_FORMAT_R32G32_SINT,
    [mrhi_vertexSint32x3] = VK_FORMAT_R32G32B32_SINT,
    [mrhi_vertexSint32x4] = VK_FORMAT_R32G32B32A32_SINT,
    [mrhi_vertexUnorm1010102] = VK_FORMAT_A2B10G10R10_UNORM_PACK32,
    [mrhi_vertexUnorm8x4Bgra] = VK_FORMAT_B8G8R8A8_UNORM,
};

static const VkStencilOp s_stencilOps[] = {
    [mrhi_stencilKeep] = VK_STENCIL_OP_KEEP,
    [mrhi_stencilZero] = VK_STENCIL_OP_ZERO,
    [mrhi_stencilReplace] = VK_STENCIL_OP_REPLACE,
    [mrhi_stencilInvert] = VK_STENCIL_OP_INVERT,
    [mrhi_stencilIncrementClamp] = VK_STENCIL_OP_INCREMENT_AND_CLAMP,
    [mrhi_stencilDecrementClamp] = VK_STENCIL_OP_DECREMENT_AND_CLAMP,
    [mrhi_stencilIncrementWrap] = VK_STENCIL_OP_INCREMENT_AND_WRAP,
    [mrhi_stencilDecrementWrap] = VK_STENCIL_OP_DECREMENT_AND_WRAP,
};

static const VkBlendFactor s_blendFactors[] = {
    [mrhi_blendZero] = VK_BLEND_FACTOR_ZERO,
    [mrhi_blendOne] = VK_BLEND_FACTOR_ONE,
    [mrhi_blendSrc] = VK_BLEND_FACTOR_SRC_COLOR,
    [mrhi_blendOneMinusSrc] = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
    [mrhi_blendSrcAlpha] = VK_BLEND_FACTOR_SRC_ALPHA,
    [mrhi_blendOneMinusSrcAlpha] = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    [mrhi_blendDst] = VK_BLEND_FACTOR_DST_COLOR,
    [mrhi_blendOneMinusDst] = VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
    [mrhi_blendDstAlpha] = VK_BLEND_FACTOR_DST_ALPHA,
    [mrhi_blendOneMinusDstAlpha] = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
    [mrhi_blendSrcAlphaSaturated] = VK_BLEND_FACTOR_SRC_ALPHA_SATURATE,
    [mrhi_blendConstant] = VK_BLEND_FACTOR_CONSTANT_COLOR,
    [mrhi_blendOneMinusConstant] = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
};

// Topologies, front faces, cull modes, blend operations and color
// writes share Vulkan's values; compare functions sit one above them.
static_assert((int)mrhi_topologyTriangleStrip == (int)VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP &&
                  (int)mrhi_frontClockwise == (int)VK_FRONT_FACE_CLOCKWISE &&
                  (int)mrhi_cullBack == (int)VK_CULL_MODE_BACK_BIT &&
                  (int)mrhi_blendMax == (int)VK_BLEND_OP_MAX &&
                  (int)mrhi_writeAlpha == (int)VK_COLOR_COMPONENT_A_BIT,
              "shared values");

static VkCompareOp CompareOf(mrhiCompareFunction compare)
{
    return compare == mrhi_compareNone ? VK_COMPARE_OP_ALWAYS : (VkCompareOp)(compare - 1);
}

static VkStencilOpState StencilOf(const mrhiStencilFace* face, uint32_t readMask,
                                  uint32_t writeMask)
{
    return (VkStencilOpState){
        .failOp = s_stencilOps[face->failOp],
        .passOp = s_stencilOps[face->passOp],
        .depthFailOp = s_stencilOps[face->depthFailOp],
        .compareOp = CompareOf(face->compare),
        .compareMask = readMask,
        .writeMask = writeMask,
    };
}

// The vertex input a def describes, in room for its buffers and
// attributes.
typedef struct VertexInput
{
    VkVertexInputBindingDescription buffers[MRHI_VULKAN_VERTEX_BUFFERS];
    VkVertexInputAttributeDescription attributes[MRHI_VULKAN_VERTEX_ATTRIBUTES];
    VkPipelineVertexInputStateCreateInfo info;
} VertexInput;

static void VertexInputOf(const mrhiGraphicsPipelineDef* def, VertexInput* input)
{
    MRHI_ASSERT(def->vertexBufferCount <= MRHI_VULKAN_VERTEX_BUFFERS &&
                def->vertexAttributeCount <= MRHI_VULKAN_VERTEX_ATTRIBUTES);
    for (uint32_t i = 0; i < def->vertexBufferCount; ++i)
    {
        input->buffers[i] = (VkVertexInputBindingDescription){
            .binding = i,
            .stride = def->vertexBuffers[i].stride,
            .inputRate = def->vertexBuffers[i].stepMode == mrhi_stepInstance
                             ? VK_VERTEX_INPUT_RATE_INSTANCE
                             : VK_VERTEX_INPUT_RATE_VERTEX,
        };
    }
    for (uint32_t i = 0; i < def->vertexAttributeCount; ++i)
    {
        const mrhiVertexAttribute* attribute = &def->vertexAttributes[i];
        input->attributes[i] = (VkVertexInputAttributeDescription){
            .location = attribute->location,
            .binding = attribute->buffer,
            .format = s_vertexFormats[attribute->format],
            .offset = attribute->offset,
        };
    }
    input->info = (VkPipelineVertexInputStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = def->vertexBufferCount,
        .pVertexBindingDescriptions = input->buffers,
        .vertexAttributeDescriptionCount = def->vertexAttributeCount,
        .pVertexAttributeDescriptions = input->attributes,
    };
}

// The color targets' formats and blending, in room for all of them.
typedef struct Targets
{
    VkFormat formats[MRHI_COLOR_TARGETS];
    VkPipelineColorBlendAttachmentState blends[MRHI_COLOR_TARGETS];
    VkPipelineColorBlendStateCreateInfo blend;
    VkPipelineRenderingCreateInfo rendering;
} Targets;

static void TargetsOf(const mrhiGraphicsPipelineDef* def, VkFormat depthStencil, Targets* targets)
{
    for (uint32_t i = 0; i < def->colorTargetCount; ++i)
    {
        const mrhiColorTargetState* target = &def->colorTargets[i];
        bool used = target->format != mrhi_formatNone;
        targets->formats[i] =
            used ? mrhiVulkanFormat(target->format, depthStencil) : VK_FORMAT_UNDEFINED;
        targets->blends[i] = (VkPipelineColorBlendAttachmentState){
            .blendEnable = used && target->blend,
            .srcColorBlendFactor = s_blendFactors[target->color.srcFactor],
            .dstColorBlendFactor = s_blendFactors[target->color.dstFactor],
            .colorBlendOp = (VkBlendOp)target->color.operation,
            .srcAlphaBlendFactor = s_blendFactors[target->alpha.srcFactor],
            .dstAlphaBlendFactor = s_blendFactors[target->alpha.dstFactor],
            .alphaBlendOp = (VkBlendOp)target->alpha.operation,
            .colorWriteMask = used ? target->writeMask : 0,
        };
    }
    targets->blend = (VkPipelineColorBlendStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = def->colorTargetCount,
        .pAttachments = targets->blends,
    };
    mrhiFormat depth = def->depthStencilFormat;
    VkFormat vulkanDepth =
        depth != mrhi_formatNone ? mrhiVulkanFormat(depth, depthStencil) : VK_FORMAT_UNDEFINED;
    targets->rendering = (VkPipelineRenderingCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = def->colorTargetCount,
        .pColorAttachmentFormats = targets->formats,
        .depthAttachmentFormat = mrhiFormatHasDepth(depth) ? vulkanDepth : VK_FORMAT_UNDEFINED,
        .stencilAttachmentFormat = mrhiFormatHasStencil(depth) ? vulkanDepth : VK_FORMAT_UNDEFINED,
    };
}

static VkPipelineDepthStencilStateCreateInfo DepthOf(const mrhiGraphicsPipelineDef* def)
{
    bool stencil = mrhiFormatHasStencil(def->depthStencilFormat);
    return (VkPipelineDepthStencilStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = mrhiFormatHasDepth(def->depthStencilFormat),
        .depthWriteEnable = def->depthWrite,
        .depthCompareOp = CompareOf(def->depthCompare),
        .stencilTestEnable = stencil,
        .front = StencilOf(&def->stencilFront, def->stencilReadMask, def->stencilWriteMask),
        .back = StencilOf(&def->stencilBack, def->stencilReadMask, def->stencilWriteMask),
    };
}

static VkPipelineRasterizationStateCreateInfo RasterOf(const mrhiGraphicsPipelineDef* def)
{
    bool bias =
        def->depthBias != 0 || def->depthBiasSlopeScale != 0.0f || def->depthBiasClamp != 0.0f;
    return (VkPipelineRasterizationStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = def->unclippedDepth,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = (VkCullModeFlags)def->cullMode,
        .frontFace = (VkFrontFace)def->frontFace,
        .depthBiasEnable = bias,
        .depthBiasConstantFactor = (float)def->depthBias,
        .depthBiasClamp = def->depthBiasClamp,
        .depthBiasSlopeFactor = def->depthBiasSlopeScale,
        .lineWidth = 1.0f,
    };
}

// The two stages, sharing one specialization.
static uint32_t StagesOf(const mrhiVulkanPipelines* pipelines,
                         const mrhiDriverGraphicsPipeline* pipeline,
                         const VkSpecializationInfo* constants, char names[2][MRHI_VULKAN_NAME],
                         VkPipelineShaderStageCreateInfo* stages)
{
    VkShaderModule module = pipelines->shaders[pipeline->shader - 1];
    mrhiVulkanEntryName(pipeline->reflection, pipeline->vertexEntry, names[0]);
    stages[0] = (VkPipelineShaderStageCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = module,
        .pName = names[0],
        .pSpecializationInfo = constants,
    };
    if (pipeline->fragmentEntry == pipeline->reflection->entryCount)
    {
        return 1;
    }
    mrhiVulkanEntryName(pipeline->reflection, pipeline->fragmentEntry, names[1]);
    stages[1] = stages[0];
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].pName = names[1];
    return 2;
}

// Makes the pipeline object for a laid-out pipeline.
static VkResult Make(mrhiVulkanPipelines* pipelines, const mrhiDriverGraphicsPipeline* pipeline,
                     mrhiVulkanPipeline* made)
{
    const mrhiGraphicsPipelineDef* def = pipeline->def;
    VkSpecializationInfo constants;
    mrhiVulkanSpecialize(pipelines, pipeline->reflection, def->constants, def->constantCount,
                         &constants);
    char names[2][MRHI_VULKAN_NAME];
    VkPipelineShaderStageCreateInfo stages[2];
    uint32_t stageCount = StagesOf(pipelines, pipeline, &constants, names, stages);
    VertexInput input;
    VertexInputOf(def, &input);
    Targets targets;
    TargetsOf(def, pipelines->depthStencil, &targets);
    bool strip =
        def->topology == mrhi_topologyLineStrip || def->topology == mrhi_topologyTriangleStrip;
    const VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = (VkPrimitiveTopology)def->topology,
        .primitiveRestartEnable = strip && def->stripIndexFormat != mrhi_indexNone,
    };
    const VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    const VkPipelineRasterizationStateCreateInfo raster = RasterOf(def);
    const VkSampleMask mask = def->sampleMask;
    const VkPipelineMultisampleStateCreateInfo samples = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = (VkSampleCountFlagBits)def->sampleCount,
        .pSampleMask = &mask,
        .alphaToCoverageEnable = def->alphaToCoverage,
    };
    const VkPipelineDepthStencilStateCreateInfo depth = DepthOf(def);
    static const VkDynamicState dynamics[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_BLEND_CONSTANTS,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE,
    };
    const VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = sizeof(dynamics) / sizeof(dynamics[0]),
        .pDynamicStates = dynamics,
    };
    const VkGraphicsPipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &targets.rendering,
        .stageCount = stageCount,
        .pStages = stages,
        .pVertexInputState = &input.info,
        .pInputAssemblyState = &assembly,
        .pViewportState = &viewport,
        .pRasterizationState = &raster,
        .pMultisampleState = &samples,
        .pDepthStencilState = &depth,
        .pColorBlendState = &targets.blend,
        .pDynamicState = &dynamic,
        .layout = made->layout,
    };
    return pipelines->api->vkCreateGraphicsPipelines(pipelines->device, pipelines->cache, 1, &info,
                                                     nullptr, &made->pipeline);
}

mrhiResult mrhiVulkanCreateGraphics(mrhiVulkanPipelines* pipelines,
                                    const mrhiDriverGraphicsPipeline* pipeline, uint64_t tag,
                                    uint64_t* handleOut)
{
    uint32_t handle = mrhiVulkanTakePipeline(pipelines);
    if (handle == 0)
    {
        return mrhi_errorCapacity;
    }
    mrhiVulkanPipeline* made = &pipelines->pipelines[handle - 1];
    mrhiResult status =
        mrhiVulkanMakeLayout(pipelines, pipeline->reflection,
                             VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, made);
    if (status != mrhi_success)
    {
        mrhiVulkanGivePipeline(pipelines, handle);
        return status;
    }
    VkResult result = Make(pipelines, pipeline, made);
    if (result != VK_SUCCESS)
    {
        mrhiVulkanDropLayout(pipelines, made);
        mrhiVulkanGivePipeline(pipelines, handle);
        return mrhiVulkanStatus(result);
    }
    made->bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    mrhiVulkanName(pipelines->api, pipelines->device, VK_OBJECT_TYPE_PIPELINE,
                   MRHI_VULKAN_HANDLE(made->pipeline), pipeline->def->label,
                   pipeline->def->labelLength);
    mrhiVulkanAnswer(pipelines, handle, tag);
    *handleOut = handle;
    return mrhi_success;
}
