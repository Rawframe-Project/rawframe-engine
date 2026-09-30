// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The WebGPU names of the contract's features, limits and formats.

#include "webgpu_names.h"

#include "capabilities_core.h"
#include "invariant.h"

#include <string.h>

const mrhiWebGpuFeature mrhiWebGpuFeatures[] = {
    {"timestamp-query", offsetof(mrhiFeatures, timestampQuery)},
    {"texture-compression-bc", offsetof(mrhiFeatures, textureCompressionBc)},
    {"texture-compression-etc2", offsetof(mrhiFeatures, textureCompressionEtc2)},
    {"texture-compression-astc", offsetof(mrhiFeatures, textureCompressionAstc)},
    {"float32-filterable", offsetof(mrhiFeatures, float32Filterable)},
    {"rg11b10ufloat-renderable", offsetof(mrhiFeatures, rg11b10Renderable)},
    {"dual-source-blending", offsetof(mrhiFeatures, dualSourceBlending)},
    {"depth-clip-control", offsetof(mrhiFeatures, unclippedDepth)},
    {"shader-f16", offsetof(mrhiFeatures, shaderF16)},
    {"subgroups", offsetof(mrhiFeatures, subgroups)},
    {"indirect-first-instance", offsetof(mrhiFeatures, indirectFirstInstance)},
};
const size_t mrhiWebGpuFeatureCount = sizeof(mrhiWebGpuFeatures) / sizeof(mrhiWebGpuFeatures[0]);

const mrhiWebGpuLimit mrhiWebGpuLimits[] = {
    {"maxTextureDimension2D", offsetof(mrhiLimits, textureDimension2d), false},
    {"maxTextureDimension3D", offsetof(mrhiLimits, textureDimension3d), false},
    {"maxTextureArrayLayers", offsetof(mrhiLimits, textureArrayLayers), false},
    {"maxBindGroups", offsetof(mrhiLimits, bindingTables), false},
    {"maxBindingsPerBindGroup", offsetof(mrhiLimits, bindingsPerTable), false},
    {"maxSampledTexturesPerShaderStage", offsetof(mrhiLimits, sampledTexturesPerStage), false},
    {"maxSamplersPerShaderStage", offsetof(mrhiLimits, samplersPerStage), false},
    {"maxStorageBuffersPerShaderStage", offsetof(mrhiLimits, storageBuffersPerStage), false},
    {"maxStorageTexturesPerShaderStage", offsetof(mrhiLimits, storageTexturesPerStage), false},
    {"maxUniformBuffersPerShaderStage", offsetof(mrhiLimits, uniformBuffersPerStage), false},
    {"maxUniformBufferBindingSize", offsetof(mrhiLimits, uniformBindingBytes), false},
    {"maxStorageBufferBindingSize", offsetof(mrhiLimits, storageBindingBytes), true},
    {"minUniformBufferOffsetAlignment", offsetof(mrhiLimits, uniformOffsetAlignment), false},
    {"minStorageBufferOffsetAlignment", offsetof(mrhiLimits, storageOffsetAlignment), false},
    {"maxVertexBuffers", offsetof(mrhiLimits, vertexBuffers), false},
    {"maxBindGroupsPlusVertexBuffers", offsetof(mrhiLimits, tablesPlusVertexBuffers), false},
    {"maxBufferSize", offsetof(mrhiLimits, bufferBytes), true},
    {"maxVertexAttributes", offsetof(mrhiLimits, vertexAttributes), false},
    {"maxVertexBufferArrayStride", offsetof(mrhiLimits, vertexStride), false},
    {"maxInterStageShaderVariables", offsetof(mrhiLimits, interStageVariables), false},
    {"maxColorAttachments", offsetof(mrhiLimits, colorAttachments), false},
    {"maxColorAttachmentBytesPerSample", offsetof(mrhiLimits, colorBytesPerSample), false},
    {"maxComputeWorkgroupStorageSize", offsetof(mrhiLimits, workgroupStorageBytes), false},
    {"maxComputeInvocationsPerWorkgroup", offsetof(mrhiLimits, workgroupInvocations), false},
    {"maxComputeWorkgroupSizeX", offsetof(mrhiLimits, workgroupSizeX), false},
    {"maxComputeWorkgroupSizeY", offsetof(mrhiLimits, workgroupSizeY), false},
    {"maxComputeWorkgroupSizeZ", offsetof(mrhiLimits, workgroupSizeZ), false},
    {"maxComputeWorkgroupsPerDimension", offsetof(mrhiLimits, workgroupsPerDimension), false},
    {"maxImmediateSize", offsetof(mrhiLimits, rootBlockBytes), false},
};
const size_t mrhiWebGpuLimitCount = sizeof(mrhiWebGpuLimits) / sizeof(mrhiWebGpuLimits[0]);

double mrhiWebGpuLimitValue(const mrhiLimits* limits, const mrhiWebGpuLimit* limit)
{
    const unsigned char* field = (const unsigned char*)limits + limit->offset;
    if (limit->wide)
    {
        uint64_t wide = 0;
        memcpy(&wide, field, sizeof(wide));
        return (double)wide;
    }
    uint32_t narrow = 0;
    memcpy(&narrow, field, sizeof(narrow));
    return (double)narrow;
}

void mrhiSetWebGpuLimit(mrhiLimits* limits, const mrhiWebGpuLimit* limit, double value)
{
    unsigned char* field = (unsigned char*)limits + limit->offset;
    if (limit->wide)
    {
        uint64_t wide = value < 18446744073709551615.0 ? (uint64_t)value : UINT64_MAX;
        memcpy(field, &wide, sizeof(wide));
        return;
    }
    uint32_t narrow = value < 4294967295.0 ? (uint32_t)value : UINT32_MAX;
    memcpy(field, &narrow, sizeof(narrow));
}

// Each format's name, from its WebGPU row.
static const char* const s_formats[MRHI_KNOWN_FORMATS + 1] = {
    [mrhi_formatRgba8Unorm] = "rgba8unorm",
    [mrhi_formatRgba8UnormSrgb] = "rgba8unorm-srgb",
    [mrhi_formatBgra8Unorm] = "bgra8unorm",
    [mrhi_formatBgra8UnormSrgb] = "bgra8unorm-srgb",
    [mrhi_formatR8Unorm] = "r8unorm",
    [mrhi_formatRg8Unorm] = "rg8unorm",
    [mrhi_formatR16Float] = "r16float",
    [mrhi_formatRg16Float] = "rg16float",
    [mrhi_formatRgba16Float] = "rgba16float",
    [mrhi_formatR32Float] = "r32float",
    [mrhi_formatRg32Float] = "rg32float",
    [mrhi_formatRgba32Float] = "rgba32float",
    [mrhi_formatR32Uint] = "r32uint",
    [mrhi_formatR32Sint] = "r32sint",
    [mrhi_formatRgb10a2Unorm] = "rgb10a2unorm",
    [mrhi_formatRg11b10Ufloat] = "rg11b10ufloat",
    [mrhi_formatDepth32Float] = "depth32float",
    [mrhi_formatDepthStencil] = "depth24plus-stencil8",
    [mrhi_formatBc1RgbaUnorm] = "bc1-rgba-unorm",
    [mrhi_formatBc1RgbaUnormSrgb] = "bc1-rgba-unorm-srgb",
    [mrhi_formatBc2RgbaUnorm] = "bc2-rgba-unorm",
    [mrhi_formatBc2RgbaUnormSrgb] = "bc2-rgba-unorm-srgb",
    [mrhi_formatBc3RgbaUnorm] = "bc3-rgba-unorm",
    [mrhi_formatBc3RgbaUnormSrgb] = "bc3-rgba-unorm-srgb",
    [mrhi_formatBc4RUnorm] = "bc4-r-unorm",
    [mrhi_formatBc4RSnorm] = "bc4-r-snorm",
    [mrhi_formatBc5RgUnorm] = "bc5-rg-unorm",
    [mrhi_formatBc5RgSnorm] = "bc5-rg-snorm",
    [mrhi_formatBc6hRgbUfloat] = "bc6h-rgb-ufloat",
    [mrhi_formatBc6hRgbFloat] = "bc6h-rgb-float",
    [mrhi_formatBc7RgbaUnorm] = "bc7-rgba-unorm",
    [mrhi_formatBc7RgbaUnormSrgb] = "bc7-rgba-unorm-srgb",
    [mrhi_formatEtc2Rgb8Unorm] = "etc2-rgb8unorm",
    [mrhi_formatEtc2Rgb8UnormSrgb] = "etc2-rgb8unorm-srgb",
    [mrhi_formatEtc2Rgb8a1Unorm] = "etc2-rgb8a1unorm",
    [mrhi_formatEtc2Rgb8a1UnormSrgb] = "etc2-rgb8a1unorm-srgb",
    [mrhi_formatEtc2Rgba8Unorm] = "etc2-rgba8unorm",
    [mrhi_formatEtc2Rgba8UnormSrgb] = "etc2-rgba8unorm-srgb",
    [mrhi_formatEacR11Unorm] = "eac-r11unorm",
    [mrhi_formatEacR11Snorm] = "eac-r11snorm",
    [mrhi_formatEacRg11Unorm] = "eac-rg11unorm",
    [mrhi_formatEacRg11Snorm] = "eac-rg11snorm",
    [mrhi_formatAstc4x4Unorm] = "astc-4x4-unorm",
    [mrhi_formatAstc4x4UnormSrgb] = "astc-4x4-unorm-srgb",
    [mrhi_formatAstc5x4Unorm] = "astc-5x4-unorm",
    [mrhi_formatAstc5x4UnormSrgb] = "astc-5x4-unorm-srgb",
    [mrhi_formatAstc5x5Unorm] = "astc-5x5-unorm",
    [mrhi_formatAstc5x5UnormSrgb] = "astc-5x5-unorm-srgb",
    [mrhi_formatAstc6x5Unorm] = "astc-6x5-unorm",
    [mrhi_formatAstc6x5UnormSrgb] = "astc-6x5-unorm-srgb",
    [mrhi_formatAstc6x6Unorm] = "astc-6x6-unorm",
    [mrhi_formatAstc6x6UnormSrgb] = "astc-6x6-unorm-srgb",
    [mrhi_formatAstc8x5Unorm] = "astc-8x5-unorm",
    [mrhi_formatAstc8x5UnormSrgb] = "astc-8x5-unorm-srgb",
    [mrhi_formatAstc8x6Unorm] = "astc-8x6-unorm",
    [mrhi_formatAstc8x6UnormSrgb] = "astc-8x6-unorm-srgb",
    [mrhi_formatAstc8x8Unorm] = "astc-8x8-unorm",
    [mrhi_formatAstc8x8UnormSrgb] = "astc-8x8-unorm-srgb",
    [mrhi_formatAstc10x5Unorm] = "astc-10x5-unorm",
    [mrhi_formatAstc10x5UnormSrgb] = "astc-10x5-unorm-srgb",
    [mrhi_formatAstc10x6Unorm] = "astc-10x6-unorm",
    [mrhi_formatAstc10x6UnormSrgb] = "astc-10x6-unorm-srgb",
    [mrhi_formatAstc10x8Unorm] = "astc-10x8-unorm",
    [mrhi_formatAstc10x8UnormSrgb] = "astc-10x8-unorm-srgb",
    [mrhi_formatAstc10x10Unorm] = "astc-10x10-unorm",
    [mrhi_formatAstc10x10UnormSrgb] = "astc-10x10-unorm-srgb",
    [mrhi_formatAstc12x10Unorm] = "astc-12x10-unorm",
    [mrhi_formatAstc12x10UnormSrgb] = "astc-12x10-unorm-srgb",
    [mrhi_formatAstc12x12Unorm] = "astc-12x12-unorm",
    [mrhi_formatAstc12x12UnormSrgb] = "astc-12x12-unorm-srgb",
};

const char* mrhiWebGpuFormat(mrhiFormat format)
{
    MRHI_ASSERT(mrhiIsFormatKnown(format));
    return s_formats[format];
}

void mrhiWebGpuViewFormats(const mrhiTextureDef* def, char out[MRHI_WEBGPU_VIEW_FORMAT_BYTES])
{
    size_t length = 0;
    out[0] = '\0';
    // A transient attachment is only rendered to, through its own format,
    // and WebGPU refuses view formats on one.
    if ((def->usage & mrhi_textureTransient) != 0)
    {
        return;
    }
    for (uint32_t i = 0; i < MRHI_VIEW_FORMATS && def->viewFormats[i] != mrhi_formatNone; ++i)
    {
        const char* name = mrhiWebGpuFormat(def->viewFormats[i]);
        size_t bytes = strlen(name);
        MRHI_ASSERT(length + bytes + 2 <= MRHI_WEBGPU_VIEW_FORMAT_BYTES);
        if (length > 0)
        {
            out[length++] = ',';
        }
        memcpy(out + length, name, bytes + 1);
        length += bytes;
    }
}
