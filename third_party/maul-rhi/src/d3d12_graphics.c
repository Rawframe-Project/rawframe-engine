// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's graphics state (d3d12_graphics.h), as the
// contract's D3D12 rows map it. A vertex attribute is the TEXCOORD
// semantic of its location, as SPIRV-Cross names vertex inputs. An alpha
// blend factor reads the alpha of the color factor named, as D3D12
// requires, and a minimum or maximum blends with factors of one, which
// the contract ignores there. Viewports, scissors, the blend constant
// and the stencil reference are set while recording.

#include "d3d12_graphics.h"

#include "d3d12_names.h"
#include "invariant.h"

static const DXGI_FORMAT s_vertexFormats[] = {
    [mrhi_vertexUint8] = DXGI_FORMAT_R8_UINT,
    [mrhi_vertexUint8x2] = DXGI_FORMAT_R8G8_UINT,
    [mrhi_vertexUint8x4] = DXGI_FORMAT_R8G8B8A8_UINT,
    [mrhi_vertexSint8] = DXGI_FORMAT_R8_SINT,
    [mrhi_vertexSint8x2] = DXGI_FORMAT_R8G8_SINT,
    [mrhi_vertexSint8x4] = DXGI_FORMAT_R8G8B8A8_SINT,
    [mrhi_vertexUnorm8] = DXGI_FORMAT_R8_UNORM,
    [mrhi_vertexUnorm8x2] = DXGI_FORMAT_R8G8_UNORM,
    [mrhi_vertexUnorm8x4] = DXGI_FORMAT_R8G8B8A8_UNORM,
    [mrhi_vertexSnorm8] = DXGI_FORMAT_R8_SNORM,
    [mrhi_vertexSnorm8x2] = DXGI_FORMAT_R8G8_SNORM,
    [mrhi_vertexSnorm8x4] = DXGI_FORMAT_R8G8B8A8_SNORM,
    [mrhi_vertexUint16] = DXGI_FORMAT_R16_UINT,
    [mrhi_vertexUint16x2] = DXGI_FORMAT_R16G16_UINT,
    [mrhi_vertexUint16x4] = DXGI_FORMAT_R16G16B16A16_UINT,
    [mrhi_vertexSint16] = DXGI_FORMAT_R16_SINT,
    [mrhi_vertexSint16x2] = DXGI_FORMAT_R16G16_SINT,
    [mrhi_vertexSint16x4] = DXGI_FORMAT_R16G16B16A16_SINT,
    [mrhi_vertexUnorm16] = DXGI_FORMAT_R16_UNORM,
    [mrhi_vertexUnorm16x2] = DXGI_FORMAT_R16G16_UNORM,
    [mrhi_vertexUnorm16x4] = DXGI_FORMAT_R16G16B16A16_UNORM,
    [mrhi_vertexSnorm16] = DXGI_FORMAT_R16_SNORM,
    [mrhi_vertexSnorm16x2] = DXGI_FORMAT_R16G16_SNORM,
    [mrhi_vertexSnorm16x4] = DXGI_FORMAT_R16G16B16A16_SNORM,
    [mrhi_vertexFloat16] = DXGI_FORMAT_R16_FLOAT,
    [mrhi_vertexFloat16x2] = DXGI_FORMAT_R16G16_FLOAT,
    [mrhi_vertexFloat16x4] = DXGI_FORMAT_R16G16B16A16_FLOAT,
    [mrhi_vertexFloat32] = DXGI_FORMAT_R32_FLOAT,
    [mrhi_vertexFloat32x2] = DXGI_FORMAT_R32G32_FLOAT,
    [mrhi_vertexFloat32x3] = DXGI_FORMAT_R32G32B32_FLOAT,
    [mrhi_vertexFloat32x4] = DXGI_FORMAT_R32G32B32A32_FLOAT,
    [mrhi_vertexUint32] = DXGI_FORMAT_R32_UINT,
    [mrhi_vertexUint32x2] = DXGI_FORMAT_R32G32_UINT,
    [mrhi_vertexUint32x3] = DXGI_FORMAT_R32G32B32_UINT,
    [mrhi_vertexUint32x4] = DXGI_FORMAT_R32G32B32A32_UINT,
    [mrhi_vertexSint32] = DXGI_FORMAT_R32_SINT,
    [mrhi_vertexSint32x2] = DXGI_FORMAT_R32G32_SINT,
    [mrhi_vertexSint32x3] = DXGI_FORMAT_R32G32B32_SINT,
    [mrhi_vertexSint32x4] = DXGI_FORMAT_R32G32B32A32_SINT,
    [mrhi_vertexUnorm1010102] = DXGI_FORMAT_R10G10B10A2_UNORM,
    [mrhi_vertexUnorm8x4Bgra] = DXGI_FORMAT_B8G8R8A8_UNORM,
};

static const D3D12_STENCIL_OP s_stencilOps[] = {
    [mrhi_stencilKeep] = D3D12_STENCIL_OP_KEEP,
    [mrhi_stencilZero] = D3D12_STENCIL_OP_ZERO,
    [mrhi_stencilReplace] = D3D12_STENCIL_OP_REPLACE,
    [mrhi_stencilInvert] = D3D12_STENCIL_OP_INVERT,
    [mrhi_stencilIncrementClamp] = D3D12_STENCIL_OP_INCR_SAT,
    [mrhi_stencilDecrementClamp] = D3D12_STENCIL_OP_DECR_SAT,
    [mrhi_stencilIncrementWrap] = D3D12_STENCIL_OP_INCR,
    [mrhi_stencilDecrementWrap] = D3D12_STENCIL_OP_DECR,
};

// Each factor for color, and for alpha, where D3D12 takes the alpha
// factor of the same source.
static const D3D12_BLEND s_blendFactors[][2] = {
    [mrhi_blendZero] = {D3D12_BLEND_ZERO, D3D12_BLEND_ZERO},
    [mrhi_blendOne] = {D3D12_BLEND_ONE, D3D12_BLEND_ONE},
    [mrhi_blendSrc] = {D3D12_BLEND_SRC_COLOR, D3D12_BLEND_SRC_ALPHA},
    [mrhi_blendOneMinusSrc] = {D3D12_BLEND_INV_SRC_COLOR, D3D12_BLEND_INV_SRC_ALPHA},
    [mrhi_blendSrcAlpha] = {D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_SRC_ALPHA},
    [mrhi_blendOneMinusSrcAlpha] = {D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA},
    [mrhi_blendDst] = {D3D12_BLEND_DEST_COLOR, D3D12_BLEND_DEST_ALPHA},
    [mrhi_blendOneMinusDst] = {D3D12_BLEND_INV_DEST_COLOR, D3D12_BLEND_INV_DEST_ALPHA},
    [mrhi_blendDstAlpha] = {D3D12_BLEND_DEST_ALPHA, D3D12_BLEND_DEST_ALPHA},
    [mrhi_blendOneMinusDstAlpha] = {D3D12_BLEND_INV_DEST_ALPHA, D3D12_BLEND_INV_DEST_ALPHA},
    [mrhi_blendSrcAlphaSaturated] = {D3D12_BLEND_SRC_ALPHA_SAT, D3D12_BLEND_SRC_ALPHA_SAT},
    [mrhi_blendConstant] = {D3D12_BLEND_BLEND_FACTOR, D3D12_BLEND_BLEND_FACTOR},
    [mrhi_blendOneMinusConstant] = {D3D12_BLEND_INV_BLEND_FACTOR, D3D12_BLEND_INV_BLEND_FACTOR},
    [mrhi_blendSrc1] = {D3D12_BLEND_SRC1_COLOR, D3D12_BLEND_SRC1_ALPHA},
    [mrhi_blendOneMinusSrc1] = {D3D12_BLEND_INV_SRC1_COLOR, D3D12_BLEND_INV_SRC1_ALPHA},
    [mrhi_blendSrc1Alpha] = {D3D12_BLEND_SRC1_ALPHA, D3D12_BLEND_SRC1_ALPHA},
    [mrhi_blendOneMinusSrc1Alpha] = {D3D12_BLEND_INV_SRC1_ALPHA, D3D12_BLEND_INV_SRC1_ALPHA},
};

static const D3D12_BLEND_OP s_blendOps[] = {
    [mrhi_blendAdd] = D3D12_BLEND_OP_ADD,
    [mrhi_blendSubtract] = D3D12_BLEND_OP_SUBTRACT,
    [mrhi_blendReverseSubtract] = D3D12_BLEND_OP_REV_SUBTRACT,
    [mrhi_blendMin] = D3D12_BLEND_OP_MIN,
    [mrhi_blendMax] = D3D12_BLEND_OP_MAX,
};

// The contract's color writes are D3D12's bits.
static_assert((int)mrhi_writeRed == (int)D3D12_COLOR_WRITE_ENABLE_RED &&
                  (int)mrhi_writeAlpha == (int)D3D12_COLOR_WRITE_ENABLE_ALPHA,
              "shared values");

static D3D12_RENDER_TARGET_BLEND_DESC BlendOf(const mrhiColorTargetState* target)
{
    D3D12_RENDER_TARGET_BLEND_DESC blend = {
        .BlendEnable = target->blend,
        .SrcBlend = D3D12_BLEND_ONE,
        .DestBlend = D3D12_BLEND_ZERO,
        .BlendOp = D3D12_BLEND_OP_ADD,
        .SrcBlendAlpha = D3D12_BLEND_ONE,
        .DestBlendAlpha = D3D12_BLEND_ZERO,
        .BlendOpAlpha = D3D12_BLEND_OP_ADD,
        .LogicOp = D3D12_LOGIC_OP_NOOP,
        .RenderTargetWriteMask = (UINT8)target->writeMask,
    };
    if (!target->blend)
    {
        return blend;
    }
    const mrhiBlendComponent* color = &target->color;
    const mrhiBlendComponent* alpha = &target->alpha;
    bool colorFixed = color->operation == mrhi_blendMin || color->operation == mrhi_blendMax;
    bool alphaFixed = alpha->operation == mrhi_blendMin || alpha->operation == mrhi_blendMax;
    blend.SrcBlend = colorFixed ? D3D12_BLEND_ONE : s_blendFactors[color->srcFactor][0];
    blend.DestBlend = colorFixed ? D3D12_BLEND_ONE : s_blendFactors[color->dstFactor][0];
    blend.BlendOp = s_blendOps[color->operation];
    blend.SrcBlendAlpha = alphaFixed ? D3D12_BLEND_ONE : s_blendFactors[alpha->srcFactor][1];
    blend.DestBlendAlpha = alphaFixed ? D3D12_BLEND_ONE : s_blendFactors[alpha->dstFactor][1];
    blend.BlendOpAlpha = s_blendOps[alpha->operation];
    return blend;
}

static D3D12_DEPTH_STENCILOP_DESC StencilOf(const mrhiStencilFace* face)
{
    return (D3D12_DEPTH_STENCILOP_DESC){
        .StencilFailOp = s_stencilOps[face->failOp],
        .StencilDepthFailOp = s_stencilOps[face->depthFailOp],
        .StencilPassOp = s_stencilOps[face->passOp],
        .StencilFunc = face->compare == mrhi_compareNone ? D3D12_COMPARISON_FUNC_ALWAYS
                                                         : mrhiD3d12Compare(face->compare),
    };
}

static D3D12_DEPTH_STENCIL_DESC DepthStencilOf(const mrhiGraphicsPipelineDef* def)
{
    bool depth = def->depthStencilFormat != mrhi_formatNone;
    return (D3D12_DEPTH_STENCIL_DESC){
        .DepthEnable = depth,
        .DepthWriteMask =
            def->depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO,
        .DepthFunc = def->depthCompare == mrhi_compareNone ? D3D12_COMPARISON_FUNC_ALWAYS
                                                           : mrhiD3d12Compare(def->depthCompare),
        .StencilEnable = def->depthStencilFormat == mrhi_formatDepthStencil,
        .StencilReadMask = (UINT8)def->stencilReadMask,
        .StencilWriteMask = (UINT8)def->stencilWriteMask,
        .FrontFace = StencilOf(&def->stencilFront),
        .BackFace = StencilOf(&def->stencilBack),
    };
}

static D3D12_RASTERIZER_DESC RasterOf(const mrhiGraphicsPipelineDef* def)
{
    static const D3D12_CULL_MODE culls[] = {
        [mrhi_cullNone] = D3D12_CULL_MODE_NONE,
        [mrhi_cullFront] = D3D12_CULL_MODE_FRONT,
        [mrhi_cullBack] = D3D12_CULL_MODE_BACK,
    };
    return (D3D12_RASTERIZER_DESC){
        .FillMode = D3D12_FILL_MODE_SOLID,
        .CullMode = culls[def->cullMode],
        .FrontCounterClockwise = def->frontFace != mrhi_frontClockwise,
        .DepthBias = def->depthBias,
        .DepthBiasClamp = def->depthBiasClamp,
        .SlopeScaledDepthBias = def->depthBiasSlopeScale,
        .DepthClipEnable = !def->unclippedDepth,
        .MultisampleEnable = def->sampleCount > 1,
    };
}

// The topology type a pipeline draws, and the topology its draws set.
static D3D12_PRIMITIVE_TOPOLOGY_TYPE TopologyOf(mrhiPrimitiveTopology topology,
                                                D3D_PRIMITIVE_TOPOLOGY* drawOut)
{
    switch (topology)
    {
    case mrhi_topologyPointList:
        *drawOut = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    case mrhi_topologyLineList:
        *drawOut = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    case mrhi_topologyLineStrip:
        *drawOut = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    case mrhi_topologyTriangleStrip:
        *drawOut = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    default:
        *drawOut = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }
}

static D3D12_INDEX_BUFFER_STRIP_CUT_VALUE CutOf(mrhiIndexFormat format)
{
    switch (format)
    {
    case mrhi_indexUint16:
        return D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF;
    case mrhi_indexUint32:
        return D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;
    default:
        return D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    }
}

void mrhiD3d12DescribeGraphics(const mrhiGraphicsPipelineDef* def, mrhiD3d12Graphics* graphicsOut)
{
    MRHI_ASSERT(def->vertexAttributeCount <= D3D12_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT);
    for (uint32_t i = 0; i < def->vertexAttributeCount; ++i)
    {
        const mrhiVertexAttribute* attribute = &def->vertexAttributes[i];
        bool instanced = def->vertexBuffers[attribute->buffer].stepMode == mrhi_stepInstance;
        graphicsOut->elements[i] = (D3D12_INPUT_ELEMENT_DESC){
            .SemanticName = "TEXCOORD",
            .SemanticIndex = attribute->location,
            .Format = s_vertexFormats[attribute->format],
            .InputSlot = attribute->buffer,
            .AlignedByteOffset = attribute->offset,
            .InputSlotClass = instanced ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                                        : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            .InstanceDataStepRate = instanced ? 1 : 0,
        };
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc = &graphicsOut->desc;
    *desc = (D3D12_GRAPHICS_PIPELINE_STATE_DESC){
        .BlendState = {.AlphaToCoverageEnable = def->alphaToCoverage,
                       .IndependentBlendEnable = TRUE},
        .SampleMask = def->sampleMask,
        .RasterizerState = RasterOf(def),
        .DepthStencilState = DepthStencilOf(def),
        .InputLayout = {.pInputElementDescs = graphicsOut->elements,
                        .NumElements = def->vertexAttributeCount},
        .IBStripCutValue = CutOf(def->stripIndexFormat),
        .PrimitiveTopologyType = TopologyOf(def->topology, &graphicsOut->topology),
        .NumRenderTargets = def->colorTargetCount,
        .DSVFormat = mrhiD3d12Format(def->depthStencilFormat),
        .SampleDesc = {.Count = def->sampleCount},
    };
    for (uint32_t i = 0; i < def->colorTargetCount; ++i)
    {
        desc->BlendState.RenderTarget[i] = BlendOf(&def->colorTargets[i]);
        desc->RTVFormats[i] = mrhiD3d12Format(def->colorTargets[i].format);
    }
}
