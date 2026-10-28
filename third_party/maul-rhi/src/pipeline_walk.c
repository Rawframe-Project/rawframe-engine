// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The walk of a pipeline the core hands a driver, each rule as WebGPU's
// pipeline validation states it for the shader's reflection. The core's
// checks in graphics.c and pipeline.c refuse a def that breaks one;
// this restates them from the driver's side, so the test driver can trap
// on a def the core let through.

#include "pipeline_walk.h"

#include "capabilities_core.h"

#include <float.h>

// The scalar class a type writes or reads: a 16-bit float is a float.
static mrhiScalarType ClassOf(mrhiScalarType type)
{
    return type == mrhi_scalarFloat16 ? mrhi_scalarFloat32 : type;
}

// Whether a constant's type holds a value exactly: false or true, a
// whole number in an integer's range, or a float a 32-bit float can
// hold. NaN is none of them.
static bool TypeHolds(mrhiConstantType type, double value)
{
    switch (type)
    {
    case mrhi_constantBool:
        return value == 0.0 || value == 1.0;
    case mrhi_constantInt32:
        return value >= (double)INT32_MIN && value <= (double)INT32_MAX &&
               (double)(int64_t)value == value;
    case mrhi_constantUint32:
        return value >= 0.0 && value <= (double)UINT32_MAX && (double)(int64_t)value == value;
    case mrhi_constantFloat32:
        return value >= -(double)FLT_MAX && value <= (double)FLT_MAX;
    default:
        return false;
    }
}

// Counts the rules constant values break: each names a constant of the
// reflection once with a value its type holds, and each required
// constant is named.
static uint32_t WalkConstants(const mrhiReflection* reflection, const mrhiConstantValue* values,
                              uint32_t count)
{
    uint32_t faults = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t named = 0;
        for (uint32_t j = 0; j < count; ++j)
        {
            named += values[j].id == values[i].id ? 1u : 0u;
        }
        const mrhiShaderConstant* constant = nullptr;
        for (uint32_t j = 0; j < reflection->constantCount && constant == nullptr; ++j)
        {
            constant =
                reflection->constants[j].id == values[i].id ? &reflection->constants[j] : nullptr;
        }
        faults += named == 1 && constant != nullptr && TypeHolds(constant->type, values[i].value)
                      ? 0u
                      : 1u;
    }
    for (uint32_t i = 0; i < reflection->constantCount; ++i)
    {
        bool named = false;
        for (uint32_t j = 0; j < count && !named; ++j)
        {
            named = values[j].id == reflection->constants[i].id;
        }
        faults += !reflection->constants[i].required || named ? 0u : 1u;
    }
    return faults;
}

uint32_t mrhiWalkComputePipeline(const mrhiDriverComputePipeline* pipeline)
{
    return WalkConstants(pipeline->reflection, pipeline->constants, pipeline->constantCount);
}

// An entry of a graphics pipeline, or NULL for none.
static const mrhiShaderEntry* EntryOf(const mrhiReflection* reflection, uint32_t entry)
{
    return entry < reflection->entryCount ? &reflection->entries[entry] : nullptr;
}

// Counts the rules the vertex state breaks: every input of the vertex
// entry is fed by an attribute at its location of its scalar class, and
// the shader's binding tables and the vertex buffers are within their
// joint limit.
static uint32_t WalkVertex(const mrhiDriverGraphicsPipeline* pipeline, const mrhiLimits* limits)
{
    const mrhiReflection* reflection = pipeline->reflection;
    const mrhiGraphicsPipelineDef* def = pipeline->def;
    const mrhiShaderEntry* vertex = EntryOf(reflection, pipeline->vertexEntry);
    uint32_t faults = 0;
    for (uint32_t i = 0; i < vertex->inputCount; ++i)
    {
        const mrhiShaderVariable* input = &reflection->inputs[vertex->firstInput + i];
        bool fed = false;
        for (uint32_t j = 0; j < def->vertexAttributeCount && !fed; ++j)
        {
            const mrhiVertexAttribute* attribute = &def->vertexAttributes[j];
            fed = attribute->location == input->location &&
                  ClassOf(mrhiGetVertexLayout(attribute->format).scalar) == ClassOf(input->type);
        }
        faults += fed ? 0u : 1u;
    }
    uint32_t tables = 0;
    for (uint32_t i = 0; i < reflection->bindingCount; ++i)
    {
        tables =
            reflection->bindings[i].table >= tables ? reflection->bindings[i].table + 1u : tables;
    }
    uint64_t slots = (uint64_t)tables + def->vertexBufferCount;
    return faults + (slots <= limits->tablesPlusVertexBuffers ? 0u : 1u);
}

// The fragment entry's output at a location from a blend source, or
// NULL.
static const mrhiShaderVariable* OutputOf(const mrhiReflection* reflection,
                                          const mrhiShaderEntry* fragment, uint32_t location,
                                          uint8_t source)
{
    for (uint32_t i = 0; fragment != nullptr && i < fragment->outputCount; ++i)
    {
        const mrhiShaderVariable* output = &reflection->outputs[fragment->firstOutput + i];
        if (output->location == location && output->blendSource == source)
        {
            return output;
        }
    }
    return nullptr;
}

// Whether a blend factor reads a source's alpha; the second source has
// the first's components.
static bool ReadsAlpha(mrhiBlendFactor factor)
{
    return factor == mrhi_blendSrcAlpha || factor == mrhi_blendOneMinusSrcAlpha ||
           factor == mrhi_blendSrcAlphaSaturated || factor == mrhi_blendSrc1Alpha ||
           factor == mrhi_blendOneMinusSrc1Alpha;
}

// Whether a blend factor reads the second source.
static bool ReadsSecond(mrhiBlendFactor factor)
{
    return factor == mrhi_blendSrc1 || factor == mrhi_blendOneMinusSrc1 ||
           factor == mrhi_blendSrc1Alpha || factor == mrhi_blendOneMinusSrc1Alpha;
}

// Counts the rules one color target breaks: written, it has an output at
// its location with its channels and its scalar class, and with alpha
// when its color blend reads alpha; blending from a second source, it
// has that output too.
static uint32_t WalkTarget(const mrhiDriverGraphicsPipeline* pipeline, uint32_t location)
{
    const mrhiColorTargetState* target = &pipeline->def->colorTargets[location];
    const mrhiShaderEntry* fragment = EntryOf(pipeline->reflection, pipeline->fragmentEntry);
    const mrhiShaderVariable* output = OutputOf(pipeline->reflection, fragment, location, 0);
    mrhiFormatTarget facts = mrhiGetFormatTarget(target->format);
    uint32_t faults = output != nullptr || target->writeMask == 0 ? 0u : 1u;
    if (output != nullptr)
    {
        bool alpha = target->blend &&
                     (ReadsAlpha(target->color.srcFactor) || ReadsAlpha(target->color.dstFactor));
        faults += output->components >= facts.channels &&
                          ClassOf(output->type) == ClassOf(facts.scalar) &&
                          (!alpha || output->components == 4)
                      ? 0u
                      : 1u;
    }
    bool second = target->blend &&
                  (ReadsSecond(target->color.srcFactor) || ReadsSecond(target->color.dstFactor) ||
                   ReadsSecond(target->alpha.srcFactor) || ReadsSecond(target->alpha.dstFactor));
    return faults +
           (!second || OutputOf(pipeline->reflection, fragment, location, 1) != nullptr ? 0u : 1u);
}

// Counts the rules the fragment state breaks: no color targets without a
// fragment entry, each target's own, and alpha to coverage only when the
// fragment entry leaves the sample mask alone.
static uint32_t WalkFragment(const mrhiDriverGraphicsPipeline* pipeline)
{
    const mrhiGraphicsPipelineDef* def = pipeline->def;
    const mrhiShaderEntry* fragment = EntryOf(pipeline->reflection, pipeline->fragmentEntry);
    uint32_t faults = fragment != nullptr || def->colorTargetCount == 0 ? 0u : 1u;
    for (uint32_t i = 0; i < def->colorTargetCount && i < MRHI_MAX_COLOR_TARGETS; ++i)
    {
        faults += def->colorTargets[i].format == mrhi_formatNone ? 0u : WalkTarget(pipeline, i);
    }
    bool masks = fragment != nullptr && (fragment->builtins & mrhi_builtinSampleMaskOut) != 0;
    return faults + (!def->alphaToCoverage || !masks ? 0u : 1u);
}

// Counts the rules the inter-stage interface breaks: each fragment
// input has a vertex output of its location, type, components,
// interpolation and sampling, and the vertex outputs, with a point
// list's point size, are within the limit.
static uint32_t WalkInterface(const mrhiDriverGraphicsPipeline* pipeline, const mrhiLimits* limits)
{
    const mrhiReflection* reflection = pipeline->reflection;
    const mrhiShaderEntry* vertex = EntryOf(reflection, pipeline->vertexEntry);
    const mrhiShaderEntry* fragment = EntryOf(reflection, pipeline->fragmentEntry);
    uint32_t faults = 0;
    for (uint32_t i = 0; fragment != nullptr && i < fragment->variableCount; ++i)
    {
        const mrhiShaderVariable* input = &reflection->variables[fragment->firstVariable + i];
        bool matched = false;
        for (uint32_t j = 0; j < vertex->variableCount && !matched; ++j)
        {
            const mrhiShaderVariable* output = &reflection->variables[vertex->firstVariable + j];
            matched = output->location == input->location && output->type == input->type &&
                      output->components == input->components &&
                      output->interpolation == input->interpolation &&
                      output->sampling == input->sampling;
        }
        faults += matched ? 0u : 1u;
    }
    uint32_t pointSize = pipeline->def->topology == mrhi_topologyPointList ? 1u : 0u;
    return faults +
           ((uint64_t)vertex->variableCount + pointSize <= limits->interStageVariables ? 0u : 1u);
}

uint32_t mrhiWalkGraphicsPipeline(const mrhiDriverGraphicsPipeline* pipeline,
                                  const mrhiLimits* limits)
{
    const mrhiGraphicsPipelineDef* def = pipeline->def;
    return WalkConstants(pipeline->reflection, def->constants, def->constantCount) +
           WalkVertex(pipeline, limits) + WalkFragment(pipeline) + WalkInterface(pipeline, limits);
}
