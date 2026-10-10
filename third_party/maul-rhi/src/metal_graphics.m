// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Metal driver's render pipeline descriptions (mrhi-0003). Vertex
// attributes take their locations as attribute indices, as SPIRV-Cross's
// MSL reads them, and vertex buffer n lies at buffer index 30 minus n,
// above the container's buffers. A buffer of stride 0 is one element
// every vertex shares: Metal's constant step, with a stride that holds
// its attributes. The blend constant is Metal's blend color, or its
// alpha for alpha factors.

#include "metal_graphics.h"

#include "capabilities_core.h"
#include "invariant.h"
#include "metal_names.h"

static const MTLVertexFormat s_vertexFormats[] = {
    [mrhi_vertexNone] = MTLVertexFormatInvalid,
    [mrhi_vertexUint8] = MTLVertexFormatUChar,
    [mrhi_vertexUint8x2] = MTLVertexFormatUChar2,
    [mrhi_vertexUint8x4] = MTLVertexFormatUChar4,
    [mrhi_vertexSint8] = MTLVertexFormatChar,
    [mrhi_vertexSint8x2] = MTLVertexFormatChar2,
    [mrhi_vertexSint8x4] = MTLVertexFormatChar4,
    [mrhi_vertexUnorm8] = MTLVertexFormatUCharNormalized,
    [mrhi_vertexUnorm8x2] = MTLVertexFormatUChar2Normalized,
    [mrhi_vertexUnorm8x4] = MTLVertexFormatUChar4Normalized,
    [mrhi_vertexSnorm8] = MTLVertexFormatCharNormalized,
    [mrhi_vertexSnorm8x2] = MTLVertexFormatChar2Normalized,
    [mrhi_vertexSnorm8x4] = MTLVertexFormatChar4Normalized,
    [mrhi_vertexUint16] = MTLVertexFormatUShort,
    [mrhi_vertexUint16x2] = MTLVertexFormatUShort2,
    [mrhi_vertexUint16x4] = MTLVertexFormatUShort4,
    [mrhi_vertexSint16] = MTLVertexFormatShort,
    [mrhi_vertexSint16x2] = MTLVertexFormatShort2,
    [mrhi_vertexSint16x4] = MTLVertexFormatShort4,
    [mrhi_vertexUnorm16] = MTLVertexFormatUShortNormalized,
    [mrhi_vertexUnorm16x2] = MTLVertexFormatUShort2Normalized,
    [mrhi_vertexUnorm16x4] = MTLVertexFormatUShort4Normalized,
    [mrhi_vertexSnorm16] = MTLVertexFormatShortNormalized,
    [mrhi_vertexSnorm16x2] = MTLVertexFormatShort2Normalized,
    [mrhi_vertexSnorm16x4] = MTLVertexFormatShort4Normalized,
    [mrhi_vertexFloat16] = MTLVertexFormatHalf,
    [mrhi_vertexFloat16x2] = MTLVertexFormatHalf2,
    [mrhi_vertexFloat16x4] = MTLVertexFormatHalf4,
    [mrhi_vertexFloat32] = MTLVertexFormatFloat,
    [mrhi_vertexFloat32x2] = MTLVertexFormatFloat2,
    [mrhi_vertexFloat32x3] = MTLVertexFormatFloat3,
    [mrhi_vertexFloat32x4] = MTLVertexFormatFloat4,
    [mrhi_vertexUint32] = MTLVertexFormatUInt,
    [mrhi_vertexUint32x2] = MTLVertexFormatUInt2,
    [mrhi_vertexUint32x3] = MTLVertexFormatUInt3,
    [mrhi_vertexUint32x4] = MTLVertexFormatUInt4,
    [mrhi_vertexSint32] = MTLVertexFormatInt,
    [mrhi_vertexSint32x2] = MTLVertexFormatInt2,
    [mrhi_vertexSint32x3] = MTLVertexFormatInt3,
    [mrhi_vertexSint32x4] = MTLVertexFormatInt4,
    [mrhi_vertexUnorm1010102] = MTLVertexFormatUInt1010102Normalized,
    [mrhi_vertexUnorm8x4Bgra] = MTLVertexFormatUChar4Normalized_BGRA,
};

static MTLBlendFactor FactorOf(mrhiBlendFactor factor, bool alpha)
{
    static const MTLBlendFactor factors[] = {
        [mrhi_blendZero] = MTLBlendFactorZero,
        [mrhi_blendOne] = MTLBlendFactorOne,
        [mrhi_blendSrc] = MTLBlendFactorSourceColor,
        [mrhi_blendOneMinusSrc] = MTLBlendFactorOneMinusSourceColor,
        [mrhi_blendSrcAlpha] = MTLBlendFactorSourceAlpha,
        [mrhi_blendOneMinusSrcAlpha] = MTLBlendFactorOneMinusSourceAlpha,
        [mrhi_blendDst] = MTLBlendFactorDestinationColor,
        [mrhi_blendOneMinusDst] = MTLBlendFactorOneMinusDestinationColor,
        [mrhi_blendDstAlpha] = MTLBlendFactorDestinationAlpha,
        [mrhi_blendOneMinusDstAlpha] = MTLBlendFactorOneMinusDestinationAlpha,
        [mrhi_blendSrcAlphaSaturated] = MTLBlendFactorSourceAlphaSaturated,
        [mrhi_blendConstant] = MTLBlendFactorBlendColor,
        [mrhi_blendOneMinusConstant] = MTLBlendFactorOneMinusBlendColor,
        [mrhi_blendSrc1] = MTLBlendFactorSource1Color,
        [mrhi_blendOneMinusSrc1] = MTLBlendFactorOneMinusSource1Color,
        [mrhi_blendSrc1Alpha] = MTLBlendFactorSource1Alpha,
        [mrhi_blendOneMinusSrc1Alpha] = MTLBlendFactorOneMinusSource1Alpha,
    };
    MRHI_ASSERT(factor <= mrhi_blendOneMinusSrc1Alpha);
    if (alpha && factor == mrhi_blendConstant)
    {
        return MTLBlendFactorBlendAlpha;
    }
    if (alpha && factor == mrhi_blendOneMinusConstant)
    {
        return MTLBlendFactorOneMinusBlendAlpha;
    }
    return factors[factor];
}

static MTLBlendOperation OperationOf(mrhiBlendOperation operation)
{
    static const MTLBlendOperation operations[] = {
        [mrhi_blendAdd] = MTLBlendOperationAdd,
        [mrhi_blendSubtract] = MTLBlendOperationSubtract,
        [mrhi_blendReverseSubtract] = MTLBlendOperationReverseSubtract,
        [mrhi_blendMin] = MTLBlendOperationMin,
        [mrhi_blendMax] = MTLBlendOperationMax,
    };
    MRHI_ASSERT(operation <= mrhi_blendMax);
    return operations[operation];
}

static MTLColorWriteMask WritesOf(mrhiColorWrites writes)
{
    MTLColorWriteMask mask = MTLColorWriteMaskNone;
    mask |= (writes & mrhi_writeRed) != 0 ? MTLColorWriteMaskRed : 0;
    mask |= (writes & mrhi_writeGreen) != 0 ? MTLColorWriteMaskGreen : 0;
    mask |= (writes & mrhi_writeBlue) != 0 ? MTLColorWriteMaskBlue : 0;
    mask |= (writes & mrhi_writeAlpha) != 0 ? MTLColorWriteMaskAlpha : 0;
    return mask;
}

// The stride of a stride-0 buffer: its attributes' extent, in words.
static uint32_t SharedStride(const mrhiGraphicsPipelineDef* def, uint32_t buffer)
{
    uint32_t end = 4;
    for (uint32_t i = 0; i < def->vertexAttributeCount; ++i)
    {
        const mrhiVertexAttribute* attribute = &def->vertexAttributes[i];
        uint32_t reach = attribute->offset + mrhiGetVertexLayout(attribute->format).bytes;
        end = attribute->buffer == buffer && reach > end ? reach : end;
    }
    return (end + 3) & ~3u;
}

static MTLVertexDescriptor* DescribeVertices(const mrhiGraphicsPipelineDef* def)
{
    MTLVertexDescriptor* vertices = [MTLVertexDescriptor vertexDescriptor];
    for (uint32_t i = 0; i < def->vertexBufferCount; ++i)
    {
        const mrhiVertexBufferLayout* buffer = &def->vertexBuffers[i];
        MTLVertexBufferLayoutDescriptor* layout =
            vertices.layouts[MRHI_METAL_VERTEX_BUFFER_TOP - i];
        bool shared = buffer->stride == 0;
        layout.stride = shared ? SharedStride(def, i) : buffer->stride;
        layout.stepFunction = shared ? MTLVertexStepFunctionConstant
                              : buffer->stepMode == mrhi_stepInstance
                                  ? MTLVertexStepFunctionPerInstance
                                  : MTLVertexStepFunctionPerVertex;
        layout.stepRate = shared ? 0 : 1;
    }
    for (uint32_t i = 0; i < def->vertexAttributeCount; ++i)
    {
        const mrhiVertexAttribute* attribute = &def->vertexAttributes[i];
        MTLVertexAttributeDescriptor* described = vertices.attributes[attribute->location];
        MRHI_ASSERT(attribute->format <= mrhi_vertexUnorm8x4Bgra);
        described.format = s_vertexFormats[attribute->format];
        described.offset = attribute->offset;
        described.bufferIndex = MRHI_METAL_VERTEX_BUFFER_TOP - attribute->buffer;
    }
    return vertices;
}

static void DescribeTargets(const mrhiGraphicsPipelineDef* def,
                            MTLRenderPipelineDescriptor* descriptor)
{
    for (uint32_t i = 0; i < def->colorTargetCount; ++i)
    {
        const mrhiColorTargetState* target = &def->colorTargets[i];
        MTLRenderPipelineColorAttachmentDescriptor* color = descriptor.colorAttachments[i];
        color.pixelFormat = mrhiMetalFormat(target->format);
        color.writeMask = WritesOf(target->writeMask);
        color.blendingEnabled = target->blend;
        color.rgbBlendOperation = OperationOf(target->color.operation);
        color.sourceRGBBlendFactor = FactorOf(target->color.srcFactor, false);
        color.destinationRGBBlendFactor = FactorOf(target->color.dstFactor, false);
        color.alphaBlendOperation = OperationOf(target->alpha.operation);
        color.sourceAlphaBlendFactor = FactorOf(target->alpha.srcFactor, true);
        color.destinationAlphaBlendFactor = FactorOf(target->alpha.dstFactor, true);
    }
    MTLPixelFormat depth = mrhiMetalFormat(def->depthStencilFormat);
    descriptor.depthAttachmentPixelFormat = depth;
    descriptor.stencilAttachmentPixelFormat =
        def->depthStencilFormat == mrhi_formatDepthStencil ? depth : MTLPixelFormatInvalid;
}

static MTLPrimitiveTopologyClass TopologyClassOf(mrhiPrimitiveTopology topology)
{
    switch (topology)
    {
    case mrhi_topologyPointList:
        return MTLPrimitiveTopologyClassPoint;
    case mrhi_topologyLineList:
    case mrhi_topologyLineStrip:
        return MTLPrimitiveTopologyClassLine;
    default:
        return MTLPrimitiveTopologyClassTriangle;
    }
}

MTLRenderPipelineDescriptor* mrhiMetalDescribeRender(const mrhiGraphicsPipelineDef* def)
{
    MTLRenderPipelineDescriptor* descriptor =
        [[[MTLRenderPipelineDescriptor alloc] init] autorelease];
    if (def->vertexAttributeCount > 0)
    {
        descriptor.vertexDescriptor = DescribeVertices(def);
    }
    DescribeTargets(def, descriptor);
    descriptor.rasterSampleCount = def->sampleCount;
    descriptor.alphaToCoverageEnabled = def->alphaToCoverage;
    descriptor.inputPrimitiveTopology = TopologyClassOf(def->topology);
    if (def->viewCount > 1)
    {
        // Each view an amplification of the vertices (mrhi-0020).
        descriptor.maxVertexAmplificationCount = def->viewCount;
    }
    if (def->labelLength > 0)
    {
        descriptor.label = mrhiMetalLabel(def->label, def->labelLength);
    }
    return descriptor;
}

// A comparison for depth or stencil, where none means always.
static MTLCompareFunction TestOf(mrhiCompareFunction compare)
{
    return compare == mrhi_compareNone ? MTLCompareFunctionAlways : mrhiMetalCompare(compare);
}

static MTLStencilOperation StencilOperationOf(mrhiStencilOperation operation)
{
    static const MTLStencilOperation operations[] = {
        [mrhi_stencilKeep] = MTLStencilOperationKeep,
        [mrhi_stencilZero] = MTLStencilOperationZero,
        [mrhi_stencilReplace] = MTLStencilOperationReplace,
        [mrhi_stencilInvert] = MTLStencilOperationInvert,
        [mrhi_stencilIncrementClamp] = MTLStencilOperationIncrementClamp,
        [mrhi_stencilDecrementClamp] = MTLStencilOperationDecrementClamp,
        [mrhi_stencilIncrementWrap] = MTLStencilOperationIncrementWrap,
        [mrhi_stencilDecrementWrap] = MTLStencilOperationDecrementWrap,
    };
    MRHI_ASSERT(operation <= mrhi_stencilDecrementWrap);
    return operations[operation];
}

static MTLStencilDescriptor* StencilOf(const mrhiStencilFace* face,
                                       const mrhiGraphicsPipelineDef* def)
{
    MTLStencilDescriptor* stencil = [[[MTLStencilDescriptor alloc] init] autorelease];
    stencil.stencilCompareFunction = TestOf(face->compare);
    stencil.stencilFailureOperation = StencilOperationOf(face->failOp);
    stencil.depthFailureOperation = StencilOperationOf(face->depthFailOp);
    stencil.depthStencilPassOperation = StencilOperationOf(face->passOp);
    stencil.readMask = def->stencilReadMask;
    stencil.writeMask = def->stencilWriteMask;
    return stencil;
}

id<MTLDepthStencilState> mrhiMetalNewDepthStencil(id<MTLDevice> device,
                                                  const mrhiGraphicsPipelineDef* def)
{
    if (def->depthStencilFormat == mrhi_formatNone)
    {
        return nil;
    }
    id<MTLDepthStencilState> state = nil;
    @autoreleasepool
    {
        MTLDepthStencilDescriptor* descriptor =
            [[[MTLDepthStencilDescriptor alloc] init] autorelease];
        descriptor.depthCompareFunction = TestOf(def->depthCompare);
        descriptor.depthWriteEnabled = def->depthWrite;
        if (def->depthStencilFormat == mrhi_formatDepthStencil)
        {
            descriptor.frontFaceStencil = StencilOf(&def->stencilFront, def);
            descriptor.backFaceStencil = StencilOf(&def->stencilBack, def);
        }
        state = [device newDepthStencilStateWithDescriptor:descriptor];
    }
    return state;
}

mrhiMetalRaster mrhiMetalRasterOf(const mrhiGraphicsPipelineDef* def)
{
    static const MTLPrimitiveType primitives[] = {
        [mrhi_topologyPointList] = MTLPrimitiveTypePoint,
        [mrhi_topologyLineList] = MTLPrimitiveTypeLine,
        [mrhi_topologyLineStrip] = MTLPrimitiveTypeLineStrip,
        [mrhi_topologyTriangleList] = MTLPrimitiveTypeTriangle,
        [mrhi_topologyTriangleStrip] = MTLPrimitiveTypeTriangleStrip,
    };
    static const MTLCullMode culls[] = {
        [mrhi_cullNone] = MTLCullModeNone,
        [mrhi_cullFront] = MTLCullModeFront,
        [mrhi_cullBack] = MTLCullModeBack,
    };
    MRHI_ASSERT(def->topology <= mrhi_topologyTriangleStrip && def->cullMode <= mrhi_cullBack);
    return (mrhiMetalRaster){
        .primitive = primitives[def->topology],
        .cull = culls[def->cullMode],
        .winding = def->frontFace == mrhi_frontClockwise ? MTLWindingClockwise
                                                         : MTLWindingCounterClockwise,
        .clip = def->unclippedDepth ? MTLDepthClipModeClamp : MTLDepthClipModeClip,
        .depthBias = (float)def->depthBias,
        .depthBiasSlope = def->depthBiasSlopeScale,
        .depthBiasClamp = def->depthBiasClamp,
    };
}
