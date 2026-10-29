// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Graphics pipelines: every state of the def checked against its
// shader's reflection, the device and WebGPU's rules for render
// pipelines, before the driver sees it. State that contradicts itself
// is invalid input; state the shader's reflection refuses is bad data,
// format, since the shader may come from content; what the device cannot
// do is unsupported. Invalid input wins over format, and format over
// unsupported. Arrays longer than their limit are refused unsupported
// unread.

#include "capabilities_core.h"
#include "invariant.h"
#include "label.h"
#include "pipeline_core.h"

#include <math.h>

#define GRAPHICS_PIPELINE_DEF_COOKIE 0x6D726770u

// A stencil face that tests nothing and changes nothing.
static const mrhiStencilFace DEFAULT_STENCIL = {
    .compare = mrhi_compareAlways,
    .failOp = mrhi_stencilKeep,
    .depthFailOp = mrhi_stencilKeep,
    .passOp = mrhi_stencilKeep,
};

mrhiGraphicsPipelineDef mrhiDefaultGraphicsPipelineDef(void)
{
    mrhiGraphicsPipelineDef def = {0};
    def.cookie = GRAPHICS_PIPELINE_DEF_COOKIE;
    def.topology = mrhi_topologyTriangleList;
    def.depthCompare = mrhi_compareAlways;
    def.stencilFront = DEFAULT_STENCIL;
    def.stencilBack = DEFAULT_STENCIL;
    def.stencilReadMask = UINT32_MAX;
    def.stencilWriteMask = UINT32_MAX;
    def.sampleCount = 1;
    def.sampleMask = UINT32_MAX;
    for (uint32_t i = 0; i < MRHI_MAX_COLOR_TARGETS; ++i)
    {
        def.colorTargets[i].color =
            (mrhiBlendComponent){mrhi_blendOne, mrhi_blendZero, mrhi_blendAdd};
        def.colorTargets[i].alpha = def.colorTargets[i].color;
        def.colorTargets[i].writeMask = mrhiColorWritesKnown;
    }
    return def;
}

// The worse of two outcomes: invalid input over format over unsupported
// over a device limit the adapter's would hold (capacity) over success.
static mrhiResult Worse(mrhiResult a, mrhiResult b)
{
    static const mrhiResult ranked[] = {mrhi_errorInvalid, mrhi_errorFormat, mrhi_errorUnsupported};
    for (size_t i = 0; i < sizeof(ranked) / sizeof(ranked[0]); ++i)
    {
        if (a == ranked[i] || b == ranked[i])
        {
            return ranked[i];
        }
    }
    return a != mrhi_success ? a : b;
}

// Invalid unless the condition holds.
static mrhiResult Need(bool condition)
{
    return condition ? mrhi_success : mrhi_errorInvalid;
}

// Format unless the condition, one the shader's reflection decides, holds.
static mrhiResult Match(bool condition)
{
    return condition ? mrhi_success : mrhi_errorFormat;
}

// Unsupported unless the condition holds.
static mrhiResult Within(bool condition)
{
    return condition ? mrhi_success : mrhi_errorUnsupported;
}

// What a format can do on the device; nothing for an unknown value.
static mrhiFormatCaps CapsOf(const mrhiDevice* device, mrhiFormat format)
{
    uint32_t index = mrhiFormatIndex(format);
    return index < MRHI_KNOWN_FORMATS ? device->formatCaps[index] : (mrhiFormatCaps){0};
}

// The checked entries of a graphics pipeline, and its reflection.
typedef struct Stages
{
    const mrhiReflection* reflection;
    const mrhiShaderEntry* vertex;
    // NULL without a fragment entry.
    const mrhiShaderEntry* fragment;
} Stages;

// The vertex buffers: within the limits, strides multiples of 4, and
// known step modes.
static mrhiResult CheckBuffers(mrhiDevice* device, const mrhiGraphicsPipelineDef* def)
{
    if (def->vertexBuffers == nullptr && def->vertexBufferCount > 0)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = MRHI_DEVICE_LIMIT(device, vertexBuffers, def->vertexBufferCount);
    if (status != mrhi_success)
    {
        return status;
    }
    for (uint32_t i = 0; i < def->vertexBufferCount && status != mrhi_errorInvalid; ++i)
    {
        const mrhiVertexBufferLayout* buffer = &def->vertexBuffers[i];
        status =
            Worse(status, Need(buffer->stride % 4 == 0 && buffer->stepMode <= mrhi_stepInstance));
        status = Worse(status, MRHI_DEVICE_LIMIT(device, vertexStride, buffer->stride));
    }
    return status;
}

// One vertex attribute: in a buffer, of a known format, aligned, within
// its buffer's stride, and at a location within the limit.
static mrhiResult CheckAttribute(mrhiDevice* device, const mrhiGraphicsPipelineDef* def,
                                 const mrhiVertexAttribute* attribute)
{
    mrhiVertexLayout layout = mrhiGetVertexLayout(attribute->format);
    if (attribute->buffer >= def->vertexBufferCount || layout.bytes == 0)
    {
        return mrhi_errorInvalid;
    }
    uint32_t alignment = layout.bytes < 4 ? layout.bytes : 4;
    uint64_t end = (uint64_t)attribute->offset + layout.bytes;
    uint32_t stride = def->vertexBuffers[attribute->buffer].stride;
    mrhiResult status = Need(attribute->offset % alignment == 0 && (stride == 0 || end <= stride));
    if (stride == 0)
    {
        status = Worse(status, MRHI_DEVICE_LIMIT(device, vertexStride, end));
    }
    return Worse(status,
                 MRHI_DEVICE_LIMIT(device, vertexAttributes, (uint64_t)attribute->location + 1));
}

// The binding tables the reflection's layout declares: one past the
// highest table any binding uses.
static uint32_t TablesOf(const mrhiReflection* reflection)
{
    uint32_t tables = 0;
    for (uint32_t i = 0; i < reflection->bindingCount; ++i)
    {
        uint32_t table = reflection->bindings[i].table + 1u;
        tables = table > tables ? table : tables;
    }
    return tables;
}

// The vertex state: buffers and attributes, one attribute per location,
// every vertex input fed by an attribute of its scalar class, and the
// tables and buffers together within their limit.
static mrhiResult CheckVertex(mrhiDevice* device, const mrhiGraphicsPipelineDef* def, Stages stages)
{
    // Buffers past their limit are refused unread, and so is the buffer
    // an attribute names among them.
    mrhiResult status = CheckBuffers(device, def);
    if (status == mrhi_errorInvalid || def->vertexBufferCount > device->limits.vertexBuffers)
    {
        return status;
    }
    if (def->vertexAttributes == nullptr && def->vertexAttributeCount > 0)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult count = MRHI_DEVICE_LIMIT(device, vertexAttributes, def->vertexAttributeCount);
    if (count != mrhi_success)
    {
        return Worse(status, count);
    }
    const mrhiVertexAttribute* attributes = def->vertexAttributes;
    for (uint32_t i = 0; i < def->vertexAttributeCount && status != mrhi_errorInvalid; ++i)
    {
        status = Worse(status, CheckAttribute(device, def, &attributes[i]));
        for (uint32_t j = 0; j < i; ++j)
        {
            status = Worse(status, Need(attributes[j].location != attributes[i].location));
        }
    }
    for (uint32_t i = 0; i < stages.vertex->inputCount && status != mrhi_errorInvalid; ++i)
    {
        const mrhiShaderVariable* input = &stages.reflection->inputs[stages.vertex->firstInput + i];
        uint32_t j = 0;
        while (j < def->vertexAttributeCount && attributes[j].location != input->location)
        {
            ++j;
        }
        status =
            Worse(status, Match(j < def->vertexAttributeCount &&
                                mrhiGetVertexLayout(attributes[j].format).scalar == input->type));
    }
    uint64_t slots = (uint64_t)TablesOf(stages.reflection) + def->vertexBufferCount;
    return Worse(status, MRHI_DEVICE_LIMIT(device, tablesPlusVertexBuffers, slots));
}

// The primitive state: known values, a strip index format only for
// strips, unclipped depth only with its feature, and views past one only
// with multiview, within its limit.
static mrhiResult CheckPrimitive(mrhiDevice* device, const mrhiGraphicsPipelineDef* def)
{
    bool strip =
        def->topology == mrhi_topologyLineStrip || def->topology == mrhi_topologyTriangleStrip;
    mrhiResult status = Need(
        def->topology <= mrhi_topologyTriangleStrip && def->stripIndexFormat <= mrhi_indexUint32 &&
        (strip || def->stripIndexFormat == mrhi_indexNone) &&
        def->frontFace <= mrhi_frontClockwise && def->cullMode <= mrhi_cullBack);
    status = Worse(status, Within(!def->unclippedDepth || device->features.unclippedDepth));
    if (def->viewCount > 1)
    {
        status = Worse(status, device->features.multiview
                                   ? MRHI_DEVICE_LIMIT(device, multiviewViews, def->viewCount)
                                   : mrhi_errorUnsupported);
    }
    return status;
}

// Whether a comparison is a test: never through always.
static bool IsCompare(mrhiCompareFunction compare)
{
    return compare >= mrhi_compareNever && compare <= mrhi_compareAlways;
}

// Whether a stencil face holds known values.
static bool IsStencilValid(const mrhiStencilFace* face)
{
    return IsCompare(face->compare) && face->failOp <= mrhi_stencilDecrementWrap &&
           face->depthFailOp <= mrhi_stencilDecrementWrap &&
           face->passOp <= mrhi_stencilDecrementWrap;
}

static bool IsStencilDefault(const mrhiStencilFace* face)
{
    return face->compare == DEFAULT_STENCIL.compare && face->failOp == DEFAULT_STENCIL.failOp &&
           face->depthFailOp == DEFAULT_STENCIL.depthFailOp &&
           face->passOp == DEFAULT_STENCIL.passOp;
}

// The depth and stencil state: a depth or stencil format the device
// renders, or none; each aspect's fields at their defaults when the
// format lacks it; finite biases, and none for points and lines; and a
// depth format when the fragment entry writes depth.
static mrhiResult CheckDepthStencil(const mrhiDevice* device, const mrhiGraphicsPipelineDef* def,
                                    Stages stages)
{
    mrhiFormat format = def->depthStencilFormat;
    bool depth = mrhiFormatHasDepth(format);
    bool stencil = mrhiFormatHasStencil(format);
    bool biased =
        def->depthBias != 0 || def->depthBiasSlopeScale != 0.0f || def->depthBiasClamp != 0.0f;
    bool triangles =
        def->topology == mrhi_topologyTriangleList || def->topology == mrhi_topologyTriangleStrip;
    bool writesDepth =
        stages.fragment != nullptr && (stages.fragment->builtins & mrhi_builtinFragDepth) != 0;
    mrhiResult status =
        Need((format == mrhi_formatNone || depth || stencil) && IsCompare(def->depthCompare) &&
             IsStencilValid(&def->stencilFront) && IsStencilValid(&def->stencilBack) &&
             isfinite(def->depthBiasSlopeScale) && isfinite(def->depthBiasClamp) &&
             (triangles || !biased));
    status = Worse(status, Match(depth || !writesDepth));
    status = Worse(status, Need(depth || (!def->depthWrite &&
                                          def->depthCompare == mrhi_compareAlways && !biased)));
    status = Worse(
        status, Need(stencil ||
                     (IsStencilDefault(&def->stencilFront) && IsStencilDefault(&def->stencilBack) &&
                      def->stencilReadMask == UINT32_MAX && def->stencilWriteMask == UINT32_MAX)));
    return Worse(status, Within(format == mrhi_formatNone || CapsOf(device, format).rendering));
}

// Whether a blend component is known, and min and max weigh by one.
static bool IsBlendValid(const mrhiBlendComponent* component)
{
    bool ones = component->srcFactor == mrhi_blendOne && component->dstFactor == mrhi_blendOne;
    return component->srcFactor <= mrhi_blendOneMinusSrc1Alpha &&
           component->dstFactor <= mrhi_blendOneMinusSrc1Alpha &&
           component->operation <= mrhi_blendMax && (component->operation < mrhi_blendMin || ones);
}

// Whether a blend factor reads a source's alpha; the second source has
// the first's components.
static bool ReadsSourceAlpha(mrhiBlendFactor factor)
{
    return factor == mrhi_blendSrcAlpha || factor == mrhi_blendOneMinusSrcAlpha ||
           factor == mrhi_blendSrcAlphaSaturated || factor == mrhi_blendSrc1Alpha ||
           factor == mrhi_blendOneMinusSrc1Alpha;
}

// Whether a blend component reads the second source.
static bool ReadsSecondSource(const mrhiBlendComponent* component)
{
    return component->srcFactor >= mrhi_blendSrc1 || component->dstFactor >= mrhi_blendSrc1;
}

// The fragment entry's output at a location, or NULL.
static const mrhiShaderVariable* OutputAt(Stages stages, uint32_t location)
{
    for (uint32_t i = 0; stages.fragment != nullptr && i < stages.fragment->outputCount; ++i)
    {
        const mrhiShaderVariable* output =
            &stages.reflection->outputs[stages.fragment->firstOutput + i];
        if (output->location == location && output->blendSource == 0)
        {
            return output;
        }
    }
    return nullptr;
}

// Whether the fragment entry has a second blend source.
static bool HasSecondSource(Stages stages)
{
    for (uint32_t i = 0; stages.fragment != nullptr && i < stages.fragment->outputCount; ++i)
    {
        if (stages.reflection->outputs[stages.fragment->firstOutput + i].blendSource == 1)
        {
            return true;
        }
    }
    return false;
}

// Whether an output's scalar type writes a target's.
static bool Writes(mrhiScalarType output, mrhiScalarType target)
{
    return output == target || (output == mrhi_scalarFloat16 && target == mrhi_scalarFloat32);
}

// One color target: a color format the device renders (and blends when
// blending), a known write mask and blend, and the fragment output that
// feeds it matching its channels and scalar class, with alpha when
// blending reads it.
static mrhiResult CheckTarget(const mrhiDevice* device, const mrhiColorTargetState* target,
                              const mrhiShaderVariable* output)
{
    mrhiFormatTarget facts = mrhiGetFormatTarget(target->format);
    mrhiFormatCaps caps = CapsOf(device, target->format);
    bool readsAlpha =
        ReadsSourceAlpha(target->color.srcFactor) || ReadsSourceAlpha(target->color.dstFactor);
    mrhiResult status =
        Need(facts.channels > 0 && (target->writeMask & ~mrhiColorWritesKnown) == 0 &&
             (!target->blend || (IsBlendValid(&target->color) && IsBlendValid(&target->alpha))));
    status =
        Worse(status,
              Match((output != nullptr || target->writeMask == 0) &&
                    (output == nullptr ||
                     (output->components >= facts.channels && Writes(output->type, facts.scalar) &&
                      (!target->blend || !readsAlpha || output->components == 4)))));
    return Worse(status, Within(caps.rendering && (!target->blend || caps.blending)));
}

// The color targets, at most MRHI_MAX_COLOR_TARGETS: none without a
// fragment entry, each checked, their bytes per sample within the
// limit, and some attachment, color or depth.
static mrhiResult CheckTargets(mrhiDevice* device, const mrhiGraphicsPipelineDef* def,
                               Stages stages)
{
    MRHI_ASSERT(def->colorTargetCount <= MRHI_MAX_COLOR_TARGETS);
    if (stages.fragment == nullptr && def->colorTargetCount > 0)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = mrhi_success;
    uint32_t bytes = 0;
    bool attached = def->depthStencilFormat != mrhi_formatNone;
    for (uint32_t i = 0; i < def->colorTargetCount; ++i)
    {
        const mrhiColorTargetState* target = &def->colorTargets[i];
        if (target->format == mrhi_formatNone)
        {
            status = Worse(status, Need(!target->blend));
            continue;
        }
        attached = true;
        status = Worse(status, CheckTarget(device, target, OutputAt(stages, i)));
        mrhiFormatTarget facts = mrhiGetFormatTarget(target->format);
        if (facts.alignment > 0)
        {
            bytes = (bytes + facts.alignment - 1) / facts.alignment * facts.alignment + facts.bytes;
        }
    }
    status = Worse(status, Need(attached));
    // Dual-source blending: the feature, one color target, and a second
    // source at location 0.
    bool second = false;
    for (uint32_t i = 0; i < def->colorTargetCount; ++i)
    {
        const mrhiColorTargetState* target = &def->colorTargets[i];
        second = second || (target->blend && (ReadsSecondSource(&target->color) ||
                                              ReadsSecondSource(&target->alpha)));
    }
    status = Worse(status, Within(!second || (device->features.dualSourceBlending &&
                                              def->colorTargetCount == 1)));
    status = Worse(status, Match(!second || HasSecondSource(stages)));
    return Worse(status, MRHI_DEVICE_LIMIT(device, colorBytesPerSample, bytes));
}

// The multisample state: a power of two every target format takes, and
// alpha to coverage only multisampled, with a blendable first target
// that has alpha, and without a fragment entry writing the sample mask.
static mrhiResult CheckMultisample(const mrhiDevice* device, const mrhiGraphicsPipelineDef* def,
                                   Stages stages)
{
    uint32_t count = def->sampleCount;
    mrhiResult status = Need(count > 0 && count <= 16 && (count & (count - 1)) == 0);
    bool taken = def->depthStencilFormat == mrhi_formatNone ||
                 (CapsOf(device, def->depthStencilFormat).sampleCounts & count) != 0;
    for (uint32_t i = 0; i < def->colorTargetCount && i < MRHI_MAX_COLOR_TARGETS; ++i)
    {
        mrhiFormat format = def->colorTargets[i].format;
        taken = taken &&
                (format == mrhi_formatNone || (CapsOf(device, format).sampleCounts & count) != 0);
    }
    status = Worse(status, Within(count == 1 || taken));
    if (!def->alphaToCoverage)
    {
        return status;
    }
    bool first = def->colorTargetCount > 0 && stages.fragment != nullptr;
    mrhiFormat format = first ? def->colorTargets[0].format : mrhi_formatNone;
    bool masks = first && (stages.fragment->builtins & mrhi_builtinSampleMaskOut) != 0;
    status = Worse(status, Need(first && count > 1 && mrhiGetFormatTarget(format).channels == 4));
    status = Worse(status, Match(!masks));
    return Worse(status, Within(CapsOf(device, format).blending));
}

// The inter-stage interface: each fragment input matched by a vertex
// output of the same location, type and interpolation, and a point list
// leaving room for its point size among the variables.
static mrhiResult CheckInterface(mrhiDevice* device, const mrhiGraphicsPipelineDef* def,
                                 Stages stages)
{
    const mrhiShaderVariable* outputs = &stages.reflection->variables[stages.vertex->firstVariable];
    mrhiResult status = def->topology == mrhi_topologyPointList
                            ? MRHI_DEVICE_LIMIT(device, interStageVariables,
                                                (uint64_t)stages.vertex->variableCount + 1)
                            : mrhi_success;
    for (uint32_t i = 0; stages.fragment != nullptr && i < stages.fragment->variableCount; ++i)
    {
        const mrhiShaderVariable* input =
            &stages.reflection->variables[stages.fragment->firstVariable + i];
        bool matched = false;
        for (uint32_t j = 0; j < stages.vertex->variableCount && !matched; ++j)
        {
            matched = outputs[j].location == input->location && outputs[j].type == input->type &&
                      outputs[j].components == input->components &&
                      outputs[j].interpolation == input->interpolation &&
                      outputs[j].sampling == input->sampling;
        }
        status = Worse(status, Match(matched));
    }
    return status;
}

// Whether a stencil face changes the stencil.
static bool ChangesStencil(const mrhiStencilFace* face)
{
    return face->failOp != mrhi_stencilKeep || face->depthFailOp != mrhi_stencilKeep ||
           face->passOp != mrhi_stencilKeep;
}

// What a checked def expects of a render pass's targets. Stencil is
// written when the write mask lets it and a face that is not culled
// changes it, as WebGPU derives it.
static mrhiRenderLayout LayoutOf(const mrhiGraphicsPipelineDef* def)
{
    mrhiRenderLayout layout = {
        .depth = def->depthStencilFormat,
        .samples = def->sampleCount,
        .views = def->viewCount > 1 ? def->viewCount : 1,
        .writesDepth = def->depthWrite,
        .writesStencil = def->stencilWriteMask != 0 &&
                         ((def->cullMode != mrhi_cullFront && ChangesStencil(&def->stencilFront)) ||
                          (def->cullMode != mrhi_cullBack && ChangesStencil(&def->stencilBack))),
    };
    for (uint32_t i = 0; i < def->colorTargetCount; ++i)
    {
        layout.colors[i] = def->colorTargets[i].format;
    }
    return layout;
}

// Keeps what draws need of a checked def's vertex buffers: each one's
// stride and step, and the bytes its attributes reach in an element.
static void KeepVertexFacts(mrhiDevice* device, uint32_t index1, const mrhiGraphicsPipelineDef* def)
{
    mrhiVertexFacts* facts =
        &device->pipelineVertex[(size_t)(index1 - 1) * device->limits.vertexBuffers];
    for (uint32_t i = 0; i < def->vertexBufferCount; ++i)
    {
        facts[i] = (mrhiVertexFacts){
            .stride = def->vertexBuffers[i].stride,
            .stepMode = def->vertexBuffers[i].stepMode,
        };
    }
    for (uint32_t i = 0; i < def->vertexAttributeCount; ++i)
    {
        const mrhiVertexAttribute* attribute = &def->vertexAttributes[i];
        uint32_t end = attribute->offset + mrhiGetVertexLayout(attribute->format).bytes;
        mrhiVertexFacts* buffer = &facts[attribute->buffer];
        buffer->lastStride = end > buffer->lastStride ? end : buffer->lastStride;
    }
}

// Finds the def's entries in the shader: success, mrhi_errorInvalid for a
// NULL vertex entry or a NULL fragment entry with a length, or
// mrhi_errorFormat when the shader lacks either.
static mrhiResult FindStages(const mrhiShaderSlot* shader, const mrhiGraphicsPipelineDef* def,
                             Stages* stagesOut)
{
    if (def->vertexEntry == nullptr ||
        (def->fragmentEntry == nullptr && def->fragmentEntryLength > 0))
    {
        return mrhi_errorInvalid;
    }
    const mrhiReflection* reflection = shader->reflection;
    uint32_t vertex =
        mrhiFindEntry(reflection, def->vertexEntry, def->vertexEntryLength, mrhi_stageVertex);
    uint32_t fragment = def->fragmentEntry == nullptr
                            ? reflection->entryCount
                            : mrhiFindEntry(reflection, def->fragmentEntry,
                                            def->fragmentEntryLength, mrhi_stageFragment);
    *stagesOut = (Stages){
        .reflection = reflection,
        .vertex = vertex < reflection->entryCount ? &reflection->entries[vertex] : nullptr,
        .fragment = fragment < reflection->entryCount ? &reflection->entries[fragment] : nullptr,
    };
    bool fragmentFound = def->fragmentEntry == nullptr || stagesOut->fragment != nullptr;
    return stagesOut->vertex != nullptr && fragmentFound ? mrhi_success : mrhi_errorFormat;
}

// Checks a graphics pipeline def: its shader, with its stages, or NULL
// with the refusal, invalid input counted as misuse and entries or
// constants the reflection lacks not.
static mrhiShaderSlot* CheckGraphicsDef(mrhiDevice* device, const mrhiGraphicsPipelineDef* def,
                                        Stages* stagesOut, mrhiResult* statusOut)
{
    mrhiShaderSlot* shader = mrhiCheckPipelineHead(
        device, MRHI_DEF_HEAD(def), GRAPHICS_PIPELINE_DEF_COOKIE, def->shader, statusOut);
    if (shader == nullptr)
    {
        return nullptr;
    }
    // More color targets than the def holds are refused first, unread.
    if (def->colorTargetCount > MRHI_MAX_COLOR_TARGETS)
    {
        *statusOut = mrhiDeviceOverLimit(device, mrhi_diagnosticGraphicsTargets);
        return nullptr;
    }
    mrhiResult found = FindStages(shader, def, stagesOut);
    if (found != mrhi_success)
    {
        *statusOut = found == mrhi_errorInvalid
                         ? mrhiDeviceMisuse(device, mrhi_diagnosticPipelineEntry)
                         : mrhiDeviceBadData(device, mrhi_diagnosticPipelineEntry);
        return nullptr;
    }
    *statusOut = mrhiCheckConstants(device, shader->reflection, def->constants, def->constantCount);
    if (*statusOut != mrhi_success)
    {
        return nullptr;
    }
    // Every state is checked; invalid input outranks format, which
    // outranks what the device cannot do, and the first state of the worst
    // outcome names the refusal.
    const mrhiResult states[] = {
        CheckVertex(device, def, *stagesOut),       CheckPrimitive(device, def),
        CheckDepthStencil(device, def, *stagesOut), CheckTargets(device, def, *stagesOut),
        CheckMultisample(device, def, *stagesOut),  CheckInterface(device, def, *stagesOut),
    };
    static const mrhiDiagnosticCode codes[] = {
        mrhi_diagnosticGraphicsVertex,       mrhi_diagnosticGraphicsPrimitive,
        mrhi_diagnosticGraphicsDepthStencil, mrhi_diagnosticGraphicsTargets,
        mrhi_diagnosticGraphicsMultisample,  mrhi_diagnosticGraphicsInterface,
    };
    mrhiResult status = mrhi_success;
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); ++i)
    {
        status = Worse(status, states[i]);
    }
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); ++i)
    {
        if (states[i] == status && status == mrhi_errorInvalid)
        {
            *statusOut = mrhiDeviceMisuse(device, codes[i]);
            return nullptr;
        }
        if (states[i] == status && status == mrhi_errorFormat)
        {
            *statusOut = mrhiDeviceBadData(device, codes[i]);
            return nullptr;
        }
    }
    *statusOut = status;
    return status == mrhi_success ? shader : nullptr;
}

mrhiResult mrhiCreateGraphicsPipeline(mrhiDevice* device, const mrhiGraphicsPipelineDef* def,
                                      mrhiGraphicsPipelineId* pipelineOut,
                                      mrhiRequestId* requestOut)
{
    if (pipelineOut != nullptr)
    {
        *pipelineOut = (mrhiGraphicsPipelineId){0};
    }
    if (requestOut != nullptr)
    {
        *requestOut = (mrhiRequestId){0};
    }
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (device->allocatorGuard.busy)
    {
        return mrhiDeviceBusy(device);
    }
    if (def == nullptr || pipelineOut == nullptr || requestOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    Stages stages;
    mrhiResult status = mrhi_success;
    mrhiShaderSlot* shader = CheckGraphicsDef(device, def, &stages, &status);
    if (shader == nullptr)
    {
        return status;
    }
    uint32_t index1 = 0;
    uint32_t generation = 0;
    status = mrhiTakePipelineSlot(device, mrhiPipelineGraphics, shader->reflection, &index1,
                                  &generation);
    if (status != mrhi_success)
    {
        return status;
    }
    const mrhiReflection* reflection = shader->reflection;
    mrhiPipelineSlot* slot = &device->pipelineSlots[index1 - 1];
    slot->layout = LayoutOf(def);
    slot->vertexBufferCount = def->vertexBufferCount;
    slot->stripIndexFormat = def->stripIndexFormat;
    KeepVertexFacts(device, index1, def);
    slot->entries[0] = (uint32_t)(stages.vertex - reflection->entries);
    slot->entries[1] = stages.fragment == nullptr
                           ? reflection->entryCount
                           : (uint32_t)(stages.fragment - reflection->entries);
    slot->heapUses =
        stages.vertex->heapUses | (stages.fragment == nullptr ? 0 : stages.fragment->heapUses);
    mrhiGraphicsPipelineDef driverDef = *def;
    mrhiDropLabel(&driverDef.label, &driverDef.labelLength);
    mrhiDriverGraphicsPipeline pipeline = {
        .shader = shader->handle,
        .reflection = reflection,
        .vertexEntry = slot->entries[0],
        .fragmentEntry = slot->entries[1],
        .def = &driverDef,
    };
    status = mrhiDriverStatus(device, device->driver.vtable->createGraphicsPipeline(
                                          device->driver.self, &pipeline,
                                          mrhiPipelineTag(device, index1), &slot->handle));
    if (status != mrhi_success)
    {
        mrhiUntakePipelineSlot(device, index1);
        return status;
    }
    *pipelineOut = (mrhiGraphicsPipelineId){index1, generation};
    *requestOut = mrhiStartPipeline(device, index1);
    return mrhi_success;
}

mrhiResult mrhiDestroyGraphicsPipeline(mrhiDevice* device, mrhiGraphicsPipelineId pipeline)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (device->allocatorGuard.busy)
    {
        return mrhiDeviceBusy(device);
    }
    return mrhiDestroyPipeline(device, mrhiPipelineGraphics, pipeline.index1, pipeline.generation);
}
