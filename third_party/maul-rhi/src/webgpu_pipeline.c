// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// WebGPU shaders and pipelines. A pipeline's descriptor is built on the
// JavaScript side one part at a time, the contract's enums named there
// from the values, so their strings stay out of the wasm. A uint32_t
// reaches JavaScript signed, so masks are read back unsigned. The layout is
// the whole container's, as on every driver: a bind group layout per
// table up to the last one used, empty where a table has no binding,
// and the root block's bytes as immediates. The pipeline's entry holds
// its bind group layouts for the bind groups frames make, and an empty
// bind group for each table without bindings.

#include "webgpu_pipeline.h"

#include "container.h"
#include "invariant.h"
#include "reflection.h"
#include "web_js.h"
#include "webgpu_names.h"

// clang-format off
// The contract's enums by value, as WebGPU names them.
EM_JS(void, mrhiJsDefineNames, (void), {
    const gpu = Module.mrhiGpu;
    if (gpu.names) {
        return;
    }
    gpu.names = {
        stages: ['VERTEX', 'FRAGMENT', 'COMPUTE'],
        samplers: [undefined, 'filtering', 'non-filtering', 'comparison'],
        samples: [undefined, 'float', 'unfilterable-float', 'depth', 'sint', 'uint'],
        dimensions: ['2d', '2d-array', 'cube', 'cube-array', '3d'],
        access: [undefined, 'read-only', 'write-only', 'read-write'],
        buffers: [undefined, 'uniform', 'storage', 'read-only-storage'],
        vertex: [undefined, 'uint8', 'uint8x2', 'uint8x4', 'sint8', 'sint8x2', 'sint8x4', 'unorm8',
                 'unorm8x2', 'unorm8x4', 'snorm8', 'snorm8x2', 'snorm8x4', 'uint16', 'uint16x2',
                 'uint16x4', 'sint16', 'sint16x2', 'sint16x4', 'unorm16', 'unorm16x2',
                 'unorm16x4', 'snorm16', 'snorm16x2', 'snorm16x4', 'float16', 'float16x2',
                 'float16x4', 'float32', 'float32x2', 'float32x3', 'float32x4', 'uint32',
                 'uint32x2', 'uint32x3', 'uint32x4', 'sint32', 'sint32x2', 'sint32x3',
                 'sint32x4', 'unorm10-10-10-2', 'unorm8x4-bgra'],
        steps: ['vertex', 'instance'],
        topologies: ['point-list', 'line-list', 'line-strip', 'triangle-list', 'triangle-strip'],
        indices: [undefined, 'uint16', 'uint32'],
        faces: ['ccw', 'cw'],
        culls: ['none', 'front', 'back'],
        compares: [undefined, 'never', 'less', 'equal', 'less-equal', 'greater', 'not-equal',
                   'greater-equal', 'always'],
        stencils: ['keep', 'zero', 'replace', 'invert', 'increment-clamp', 'decrement-clamp',
                   'increment-wrap', 'decrement-wrap'],
        factors: ['zero', 'one', 'src', 'one-minus-src', 'src-alpha', 'one-minus-src-alpha',
                  'dst', 'one-minus-dst', 'dst-alpha', 'one-minus-dst-alpha',
                  'src-alpha-saturated', 'constant', 'one-minus-constant', 'src1',
                  'one-minus-src1', 'src1-alpha', 'one-minus-src1-alpha'],
        operations: ['add', 'subtract', 'reverse-subtract', 'min', 'max'],
        aspects: ['all', 'depth-only', 'stencil-only'],
    };
});

EM_JS(int, mrhiJsCreateShader, (int state, const char* code, double bytes, const char* label,
                            int labelLength), {
    const gpu = Module.mrhiGpu;
    const self = gpu.states[state];
    return gpu.put(self, self.device.createShaderModule({
        label: UTF8ToString(label, labelLength),
        code: UTF8ToString(code, bytes),
    }));
});

// Starts a descriptor: the tables' layouts, the constants, and the
// render state, filled by the calls that follow.
EM_JS(void, mrhiJsBegin, (int state), {
    Module.mrhiGpu.states[state].building = {tables: [], constants: {}, buffers: [], targets: []};
});

EM_JS(void, mrhiJsBinding, (int state, uint32_t table, uint32_t slot, uint32_t kind, uint32_t stages,
                        uint32_t sampler, uint32_t sampleType, uint32_t dimension,
                        uint32_t access, const char* format, bool multisampled, double minSize), {
    const gpu = Module.mrhiGpu;
    const names = gpu.names;
    const building = gpu.states[state].building;
    while (building.tables.length <= table) {
        building.tables.push([]);
    }
    let visibility = 0;
    names.stages.forEach((name, bit) => {
        visibility |= (stages >> bit & 1) ? GPUShaderStage[name] : 0;
    });
    const entry = {binding: slot, visibility};
    if (kind <= 3) {
        entry.buffer = {type: names.buffers[kind], minBindingSize: minSize};
    } else if (kind === 4) {
        entry.sampler = {type: names.samplers[sampler]};
    } else if (kind === 5) {
        entry.texture = {sampleType: names.samples[sampleType],
                         viewDimension: names.dimensions[dimension], multisampled};
    } else {
        entry.storageTexture = {access: names.access[access], format: UTF8ToString(format),
                                viewDimension: names.dimensions[dimension]};
    }
    building.tables[table].push(entry);
});

EM_JS(void, mrhiJsConstant, (int state, uint32_t id, double value), {
    Module.mrhiGpu.states[state].building.constants[id] = value;
});

EM_JS(void, mrhiJsVertexBuffer, (int state, uint32_t stride, uint32_t step), {
    const gpu = Module.mrhiGpu;
    gpu.states[state].building.buffers.push({arrayStride: stride, stepMode: gpu.names.steps[step],
                                             attributes: []});
});

EM_JS(void, mrhiJsVertexAttribute, (int state, uint32_t buffer, uint32_t location, uint32_t format,
                                uint32_t offset), {
    const gpu = Module.mrhiGpu;
    gpu.states[state].building.buffers[buffer].attributes.push(
        {shaderLocation: location, format: gpu.names.vertex[format], offset});
});

EM_JS(void, mrhiJsPrimitive, (int state, uint32_t topology, uint32_t stripIndex, uint32_t face,
                          uint32_t cull, bool unclipped), {
    const names = Module.mrhiGpu.names;
    const primitive = {topology: names.topologies[topology], frontFace: names.faces[face],
                       cullMode: names.culls[cull]};
    if (stripIndex) {
        primitive.stripIndexFormat = names.indices[stripIndex];
    }
    if (unclipped) {
        primitive.unclippedDepth = true;
    }
    Module.mrhiGpu.states[state].building.primitive = primitive;
});

// A stencil face from its compare, fail, depth fail and pass values.
EM_JS(void, mrhiJsDepthStencil, (int state, const char* format, bool write, uint32_t compare,
                             const uint32_t* front, const uint32_t* back, uint32_t readMask,
                             uint32_t writeMask, int32_t bias, float slope, float clamp), {
    const names = Module.mrhiGpu.names;
    const face = at => ({
        compare: names.compares[HEAPU32[at >> 2]],
        failOp: names.stencils[HEAPU32[(at >> 2) + 1]],
        depthFailOp: names.stencils[HEAPU32[(at >> 2) + 2]],
        passOp: names.stencils[HEAPU32[(at >> 2) + 3]],
    });
    Module.mrhiGpu.states[state].building.depthStencil = {
        format: UTF8ToString(format),
        depthWriteEnabled: write,
        depthCompare: names.compares[compare],
        stencilFront: face(front),
        stencilBack: face(back),
        stencilReadMask: readMask >>> 0,
        stencilWriteMask: writeMask >>> 0,
        depthBias: bias,
        depthBiasSlopeScale: slope,
        depthBiasClamp: clamp,
    };
});

EM_JS(void, mrhiJsMultisample, (int state, uint32_t count, uint32_t mask, bool alphaToCoverage), {
    Module.mrhiGpu.states[state].building.multisample = {count, mask: mask >>> 0,
                                                          alphaToCoverageEnabled: alphaToCoverage};
});

// A color target, or a gap for a null format; its blend's factors and
// operations as color then alpha.
EM_JS(void, mrhiJsColorTarget, (int state, const char* format, bool blend, const uint32_t* factors,
                            uint32_t writeMask), {
    const names = Module.mrhiGpu.names;
    const building = Module.mrhiGpu.states[state].building;
    if (!format) {
        building.targets.push(null);
        return;
    }
    const target = {format: UTF8ToString(format), writeMask};
    if (blend) {
        const at = factors >> 2;
        const component = first => ({srcFactor: names.factors[HEAPU32[at + first]],
                                     dstFactor: names.factors[HEAPU32[at + first + 1]],
                                     operation: names.operations[HEAPU32[at + first + 2]]});
        target.blend = {color: component(0), alpha: component(3)};
    }
    building.targets.push(target);
});

// Starts the pipeline the descriptor describes and returns its handle;
// it settles into the state's queue unless destroyed first.
EM_JS(int, mrhiJsStart, (int state, bool compute, int shader, const char* vertex, int vertexLength,
                     const char* fragment, int fragmentLength, uint32_t immediates,
                     const char* label, int labelLength, int failure), {
    const gpu = Module.mrhiGpu;
    const self = gpu.states[state];
    const building = self.building;
    self.building = null;
    const device = self.device;
    const layouts = building.tables.map(entries => device.createBindGroupLayout({entries}));
    const layout = device.createPipelineLayout({bindGroupLayouts: layouts,
                                                immediateSize: immediates});
    const module = self.objects[shader];
    const stage = (entry, length) => ({module, entryPoint: UTF8ToString(entry, length),
                                       constants: building.constants});
    // A table without bindings takes an empty bind group, set with the
    // pipeline, since WebGPU wants every group of the layout.
    const empty = building.tables.map((entries, table) => entries.length > 0 ? null :
        device.createBindGroup({layout: layouts[table], entries: []}));
    const entry = {pipeline: null, layouts, empty};
    const handle = gpu.put(self, entry);
    const descriptor = {label: UTF8ToString(label, labelLength), layout};
    let started;
    if (compute) {
        descriptor.compute = stage(vertex, vertexLength);
        started = device.createComputePipelineAsync(descriptor);
    } else {
        descriptor.vertex = Object.assign(stage(vertex, vertexLength), {buffers: building.buffers});
        descriptor.primitive = building.primitive;
        descriptor.multisample = building.multisample;
        if (building.depthStencil) {
            descriptor.depthStencil = building.depthStencil;
        }
        if (fragment) {
            descriptor.fragment = Object.assign(stage(fragment, fragmentLength),
                                                {targets: building.targets});
        }
        started = device.createRenderPipelineAsync(descriptor);
    }
    const settle = (pipeline, outcome) => {
        if (gpu.states[state] === self && self.objects[handle] === entry) {
            entry.pipeline = pipeline;
            self.pipelines.push([handle, outcome]);
        }
    };
    // The core checked the pipeline, so the browser refusing it is kept
    // with the device's errors.
    started.then(pipeline => settle(pipeline, 0), error => {
        gpu.errors.push(error.message);
        settle(null, failure);
    });
    return handle;
});

// The next settled pipeline, or -1; its outcome through outcomeOut.
EM_JS(int, mrhiJsTakePipeline, (int state, int32_t* outcomeOut), {
    const next = Module.mrhiGpu.states[state].pipelines.shift();
    if (!next) {
        return -1;
    }
    HEAP32[outcomeOut >> 2] = next[1];
    return next[0];
});
// clang-format on

EM_JS_DEPS(mrhi_webgpu_pipeline, "$UTF8ToString");

void mrhiWebGpuDefineNames(void)
{
    mrhiJsDefineNames();
}

uint64_t mrhiWebGpuCreateShader(int state, const mrhiShaderDef* def, const mrhiContainer* container)
{
    return (uint64_t)mrhiJsCreateShader(state, (const char*)container->wgsl,
                                        (double)container->wgslBytes, def->label,
                                        (int)def->labelLength);
}

// Describes the whole container's bindings, table by table.
static void DescribeLayout(int state, const mrhiReflection* reflection)
{
    mrhiJsBegin(state);
    for (uint32_t i = 0; i < reflection->bindingCount; ++i)
    {
        const mrhiShaderBinding* binding = &reflection->bindings[i];
        const char* format = binding->kind == mrhi_bindingStorageTexture
                                 ? mrhiWebGpuFormat(binding->format)
                                 : nullptr;
        mrhiJsBinding(state, binding->table, binding->slot, binding->kind, binding->stages,
                      binding->sampler, binding->sampleType, binding->viewDimension,
                      binding->access, format, binding->multisampled, (double)binding->minSize);
    }
}

static void DescribeConstants(int state, const mrhiConstantValue* constants, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        mrhiJsConstant(state, constants[i].id, constants[i].value);
    }
}

// An entry point's name in the reflection, and its bytes.
static const char* EntryName(const mrhiReflection* reflection, uint32_t entry, int* lengthOut)
{
    const mrhiShaderEntry* found = &reflection->entries[entry];
    *lengthOut = (int)found->nameLength;
    return reflection->names + found->nameOffset;
}

uint64_t mrhiWebGpuStartComputePipeline(int state, const mrhiDriverComputePipeline* pipeline)
{
    DescribeLayout(state, pipeline->reflection);
    DescribeConstants(state, pipeline->constants, pipeline->constantCount);
    int length = 0;
    const char* entry = EntryName(pipeline->reflection, pipeline->entry, &length);
    return (uint64_t)mrhiJsStart(state, true, (int)pipeline->shader, entry, length, nullptr, 0,
                                 pipeline->reflection->rootBlockBytes, pipeline->label,
                                 (int)pipeline->labelLength, mrhi_errorPlatform);
}

// The render state of a graphics def.
static void DescribeRender(int state, const mrhiGraphicsPipelineDef* def)
{
    for (uint32_t i = 0; i < def->vertexBufferCount; ++i)
    {
        mrhiJsVertexBuffer(state, def->vertexBuffers[i].stride, def->vertexBuffers[i].stepMode);
    }
    for (uint32_t i = 0; i < def->vertexAttributeCount; ++i)
    {
        const mrhiVertexAttribute* attribute = &def->vertexAttributes[i];
        mrhiJsVertexAttribute(state, attribute->buffer, attribute->location, attribute->format,
                              attribute->offset);
    }
    mrhiJsPrimitive(state, def->topology, def->stripIndexFormat, def->frontFace, def->cullMode,
                    def->unclippedDepth);
    if (def->depthStencilFormat != mrhi_formatNone)
    {
        const uint32_t front[4] = {def->stencilFront.compare, def->stencilFront.failOp,
                                   def->stencilFront.depthFailOp, def->stencilFront.passOp};
        const uint32_t back[4] = {def->stencilBack.compare, def->stencilBack.failOp,
                                  def->stencilBack.depthFailOp, def->stencilBack.passOp};
        mrhiJsDepthStencil(state, mrhiWebGpuFormat(def->depthStencilFormat), def->depthWrite,
                           def->depthCompare, front, back, def->stencilReadMask,
                           def->stencilWriteMask, def->depthBias, def->depthBiasSlopeScale,
                           def->depthBiasClamp);
    }
    mrhiJsMultisample(state, def->sampleCount, def->sampleMask, def->alphaToCoverage);
    for (uint32_t i = 0; i < def->colorTargetCount; ++i)
    {
        const mrhiColorTargetState* target = &def->colorTargets[i];
        const uint32_t factors[6] = {target->color.srcFactor, target->color.dstFactor,
                                     target->color.operation, target->alpha.srcFactor,
                                     target->alpha.dstFactor, target->alpha.operation};
        const char* format =
            target->format == mrhi_formatNone ? nullptr : mrhiWebGpuFormat(target->format);
        mrhiJsColorTarget(state, format, target->blend, factors, target->writeMask);
    }
}

uint64_t mrhiWebGpuStartGraphicsPipeline(int state, const mrhiDriverGraphicsPipeline* pipeline)
{
    const mrhiReflection* reflection = pipeline->reflection;
    const mrhiGraphicsPipelineDef* def = pipeline->def;
    DescribeLayout(state, reflection);
    DescribeConstants(state, def->constants, def->constantCount);
    DescribeRender(state, def);
    int vertexLength = 0;
    const char* vertex = EntryName(reflection, pipeline->vertexEntry, &vertexLength);
    int fragmentLength = 0;
    const char* fragment = pipeline->fragmentEntry < reflection->entryCount
                               ? EntryName(reflection, pipeline->fragmentEntry, &fragmentLength)
                               : nullptr;
    return (uint64_t)mrhiJsStart(state, false, (int)pipeline->shader, vertex, vertexLength,
                                 fragment, fragmentLength, reflection->rootBlockBytes, def->label,
                                 (int)def->labelLength, mrhi_errorPlatform);
}

bool mrhiWebGpuTakePipeline(int state, uint64_t* handleOut, mrhiResult* outcomeOut)
{
    int32_t outcome = 0;
    int handle = mrhiJsTakePipeline(state, &outcome);
    if (handle < 0)
    {
        return false;
    }
    *handleOut = (uint64_t)handle;
    *outcomeOut = outcome;
    return true;
}
