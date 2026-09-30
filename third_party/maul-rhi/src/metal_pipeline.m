// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Metal driver's shaders and pipelines (mrhi-0003). MSL is compiled
// without fast math, which the family's floating-point rules forbid; a
// metallib carries the choices its cook made. Each entry point is a
// function of its own name, specialized with the pipeline's constants as
// function constants of their ids; the ones a pipeline leaves out keep
// the defaults written in the code.

#include "metal_pipeline.h"

#include "allocator.h"
#include "container.h"
#include "invariant.h"
#include "metal_names.h"
#include "reflection.h"

#include <stdalign.h>
#include <string.h>

// A shader: a library per entry, the root block's buffer index, each
// entry's buffer sizes index and each binding's index in its class.
typedef struct MetalShader
{
    size_t bytes;
    uint32_t entryCount;
    uint32_t bindingCount;
    uint8_t root;
    id<MTLLibrary>* libraries;
    uint8_t* sizes;
    uint8_t* indices;
} MetalShader;

static uint64_t HandleOf(const void* pointer)
{
    return (uint64_t)(uintptr_t)pointer;
}

static void* PointerOf(uint64_t handle)
{
    MRHI_ASSERT(handle != 0);
    return (void*)(uintptr_t)handle;
}

static MTLCompileOptions* CompileOptions(void)
{
    MTLCompileOptions* options = [[[MTLCompileOptions alloc] init] autorelease];
    if (@available(macOS 15.0, iOS 18.0, *))
    {
        options.mathMode = MTLMathModeSafe;
    }
    else
    {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        options.fastMathEnabled = NO;
#pragma clang diagnostic pop
    }
    return options;
}

// Fills every entry's library from the metallib, which holds them all.
static bool TakeMetallib(id<MTLDevice> device, const mrhiContainer* container, NSString* label,
                         id<MTLLibrary>* libraries)
{
    dispatch_data_t data = dispatch_data_create(container->metallib, container->metallibBytes,
                                                nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    NSError* error = nil;
    id<MTLLibrary> library = [device newLibraryWithData:data error:&error];
    dispatch_release(data);
    if (library == nil)
    {
        return false;
    }
    if (label != nil)
    {
        library.label = label;
    }
    for (uint32_t i = 0; i < container->entryCount; ++i)
    {
        libraries[i] = [library retain];
    }
    [library release];
    return true;
}

// Fills each entry's library from its MSL, releasing those made when
// Metal refuses one.
static bool CompileMsl(id<MTLDevice> device, const mrhiContainer* container, NSString* label,
                       id<MTLLibrary>* libraries)
{
    MTLCompileOptions* options = CompileOptions();
    for (uint32_t i = 0; i < container->entryCount; ++i)
    {
        mrhiMetalEntry entry = mrhiContainerMetalEntry(container, i);
        NSString* source = [[[NSString alloc] initWithBytes:container->msl + entry.mslOffset
                                                     length:entry.mslLength
                                                   encoding:NSUTF8StringEncoding] autorelease];
        NSError* error = nil;
        libraries[i] =
            source == nil ? nil : [device newLibraryWithSource:source options:options error:&error];
        if (libraries[i] == nil)
        {
            for (uint32_t made = 0; made < i; ++made)
            {
                [libraries[made] release];
            }
            return false;
        }
        if (label != nil)
        {
            libraries[i].label = label;
        }
    }
    return true;
}

mrhiResult mrhiMetalCreateShader(const mrhiMetalPipelines* pipelines, const mrhiShaderDef* def,
                                 const mrhiContainer* container, uint64_t* handleOut)
{
    if (container->metalMap == nullptr)
    {
        return mrhi_errorUnsupported;
    }
    mrhiLayout layout = {.size = sizeof(MetalShader)};
    size_t librariesAt = mrhiLayoutAdd(&layout, container->entryCount, sizeof(id), alignof(id));
    size_t sizesAt = mrhiLayoutAdd(&layout, container->entryCount, 1, 1);
    size_t indicesAt = mrhiLayoutAdd(&layout, container->bindingCount, 1, 1);
    MetalShader* shader =
        layout.overflow ? nullptr
                        : mrhiAllocate(pipelines->allocator, layout.size, alignof(MetalShader));
    if (shader == nullptr)
    {
        return mrhi_errorCapacity;
    }
    unsigned char* block = (unsigned char*)shader;
    *shader = (MetalShader){
        .bytes = layout.size,
        .entryCount = container->entryCount,
        .bindingCount = container->bindingCount,
        .root = mrhiContainerMetalRoot(container),
        .libraries = (id<MTLLibrary>*)(void*)(block + librariesAt),
        .sizes = block + sizesAt,
        .indices = block + indicesAt,
    };
    for (uint32_t i = 0; i < container->entryCount; ++i)
    {
        shader->sizes[i] = mrhiContainerMetalEntry(container, i).sizesIndex;
    }
    for (uint32_t i = 0; i < container->bindingCount; ++i)
    {
        shader->indices[i] = mrhiContainerMetalIndex(container, i);
    }
    bool made = false;
    @autoreleasepool
    {
        NSString* label = mrhiMetalLabel(def->label, def->labelLength);
        made = container->metallib != nullptr
                   ? TakeMetallib(pipelines->device, container, label, shader->libraries)
                   : CompileMsl(pipelines->device, container, label, shader->libraries);
    }
    if (!made)
    {
        mrhiRelease(pipelines->allocator, shader, shader->bytes, alignof(MetalShader));
        return mrhi_errorPlatform;
    }
    *handleOut = HandleOf(shader);
    return mrhi_success;
}

void mrhiMetalDestroyShader(const mrhiMetalPipelines* pipelines, uint64_t handle)
{
    MetalShader* shader = PointerOf(handle);
    for (uint32_t i = 0; i < shader->entryCount; ++i)
    {
        [shader->libraries[i] release];
    }
    mrhiRelease(pipelines->allocator, shader, shader->bytes, alignof(MetalShader));
}

// Sets one constant, in its type, on the values a function is made with.
static void SetConstant(MTLFunctionConstantValues* values, mrhiConstantType type, uint32_t id,
                        double value)
{
    switch (type)
    {
    case mrhi_constantBool:
    {
        bool flag = value != 0.0;
        [values setConstantValue:&flag type:MTLDataTypeBool atIndex:id];
        break;
    }
    case mrhi_constantInt32:
    {
        int32_t number = (int32_t)value;
        [values setConstantValue:&number type:MTLDataTypeInt atIndex:id];
        break;
    }
    case mrhi_constantUint32:
    {
        uint32_t number = (uint32_t)value;
        [values setConstantValue:&number type:MTLDataTypeUInt atIndex:id];
        break;
    }
    default:
    {
        float number = (float)value;
        [values setConstantValue:&number type:MTLDataTypeFloat atIndex:id];
        break;
    }
    }
}

// An entry's function with a pipeline's constants, retained; nil when
// Metal refuses.
static id<MTLFunction> NewFunction(const MetalShader* shader, const mrhiReflection* reflection,
                                   uint32_t entry, const mrhiConstantValue* constants,
                                   uint32_t count)
{
    const mrhiShaderEntry* found = &reflection->entries[entry];
    NSString* name = [[[NSString alloc] initWithBytes:reflection->names + found->nameOffset
                                               length:found->nameLength
                                             encoding:NSUTF8StringEncoding] autorelease];
    MTLFunctionConstantValues* values = [[[MTLFunctionConstantValues alloc] init] autorelease];
    for (uint32_t i = 0; i < count; ++i)
    {
        mrhiConstantType type = mrhi_constantNone;
        for (uint32_t c = 0; c < reflection->constantCount && type == mrhi_constantNone; ++c)
        {
            type = reflection->constants[c].id == constants[i].id ? reflection->constants[c].type
                                                                  : type;
        }
        MRHI_ASSERT(type != mrhi_constantNone);
        SetConstant(values, type, constants[i].id, constants[i].value);
    }
    NSError* error = nil;
    return [shader->libraries[entry] newFunctionWithName:name constantValues:values error:&error];
}

// A pipeline of a shader, with its bindings taken from the reflection
// and the shader's indices; NULL when the allocator fails.
static mrhiMetalPipeline* NewPipeline(const mrhiMetalPipelines* pipelines,
                                      const MetalShader* shader, const mrhiReflection* reflection,
                                      bool compute)
{
    static_assert(alignof(mrhiMetalBinding) <= alignof(mrhiMetalPipeline), "bindings follow");
    size_t bytes = sizeof(mrhiMetalPipeline) + shader->bindingCount * sizeof(mrhiMetalBinding);
    mrhiMetalPipeline* pipeline =
        mrhiAllocate(pipelines->allocator, bytes, alignof(mrhiMetalPipeline));
    if (pipeline == nullptr)
    {
        return nullptr;
    }
    mrhiMetalBinding* bindings = (mrhiMetalBinding*)(void*)(pipeline + 1);
    for (uint32_t i = 0; i < shader->bindingCount; ++i)
    {
        const mrhiShaderBinding* binding = &reflection->bindings[i];
        bindings[i] = (mrhiMetalBinding){
            .slot = binding->slot,
            .table = binding->table,
            .kind = binding->kind,
            .stages = (uint8_t)binding->stages,
            .index = shader->indices[i],
        };
    }
    *pipeline = (mrhiMetalPipeline){
        .bytes = bytes,
        .compute = compute,
        .rootBytes = reflection->rootBlockBytes,
        .root = shader->root,
        .sizes = {MRHI_METAL_NONE, MRHI_METAL_NONE},
        .bindingCount = shader->bindingCount,
        .bindings = bindings,
    };
    return pipeline;
}

static void FreePipeline(const mrhiMetalPipelines* pipelines, mrhiMetalPipeline* pipeline)
{
    [pipeline->state release];
    [pipeline->depthStencil release];
    mrhiRelease(pipelines->allocator, pipeline, pipeline->bytes, alignof(mrhiMetalPipeline));
}

// Queues a made pipeline's answer for the next poll: one per pipeline the
// device holds at most, so the queue never fills.
static void Answer(mrhiMetalPipelines* pipelines, const mrhiMetalPipeline* pipeline, uint64_t tag,
                   uint64_t* handleOut)
{
    MRHI_ASSERT(pipelines->pendingCount < pipelines->pendingLimit);
    *handleOut = HandleOf(pipeline);
    pipelines->pending[pipelines->pendingCount] =
        (mrhiDriverEvent){.tag = tag, .outcome = mrhi_success};
    pipelines->pendingHandles[pipelines->pendingCount] = *handleOut;
    ++pipelines->pendingCount;
}

mrhiResult mrhiMetalCreateCompute(mrhiMetalPipelines* pipelines,
                                  const mrhiDriverComputePipeline* pipeline, uint64_t tag,
                                  uint64_t* handleOut)
{
    const MetalShader* shader = PointerOf(pipeline->shader);
    mrhiMetalPipeline* made = NewPipeline(pipelines, shader, pipeline->reflection, true);
    if (made == nullptr)
    {
        return mrhi_errorCapacity;
    }
    const uint32_t* size = pipeline->reflection->entries[pipeline->entry].workgroup;
    made->workgroup = MTLSizeMake(size[0], size[1], size[2]);
    made->sizes[0] = shader->sizes[pipeline->entry];
    mrhiResult status = mrhi_errorPlatform;
    @autoreleasepool
    {
        id<MTLFunction> function = NewFunction(shader, pipeline->reflection, pipeline->entry,
                                               pipeline->constants, pipeline->constantCount);
        MTLComputePipelineDescriptor* descriptor =
            [[[MTLComputePipelineDescriptor alloc] init] autorelease];
        descriptor.computeFunction = function;
        if (pipeline->labelLength > 0)
        {
            descriptor.label = mrhiMetalLabel(pipeline->label, pipeline->labelLength);
        }
        NSError* error = nil;
        id<MTLComputePipelineState> state =
            function == nil
                ? nil
                : [pipelines->device newComputePipelineStateWithDescriptor:descriptor
                                                                   options:MTLPipelineOptionNone
                                                                reflection:nil
                                                                     error:&error];
        [function release];
        made->state = state;
        uint64_t threads = (uint64_t)size[0] * size[1] * size[2];
        status = state == nil                                    ? mrhi_errorPlatform
                 : threads > state.maxTotalThreadsPerThreadgroup ? mrhi_errorUnsupported
                                                                 : mrhi_success;
    }
    if (status != mrhi_success)
    {
        FreePipeline(pipelines, made);
        return status;
    }
    Answer(pipelines, made, tag, handleOut);
    return mrhi_success;
}

// Whether vertex buffers reach an index the container's vertex entry
// uses: its root block, a buffer binding, or its buffer sizes.
static bool VertexBuffersCollide(const MetalShader* shader, const mrhiReflection* reflection,
                                 uint32_t vertexEntry, uint32_t vertexBuffers)
{
    for (uint32_t n = 0; n < vertexBuffers; ++n)
    {
        uint32_t index = MRHI_METAL_VERTEX_BUFFER_TOP - n;
        bool taken = index == shader->root || index == shader->sizes[vertexEntry];
        for (uint32_t i = 0; i < shader->bindingCount && !taken; ++i)
        {
            mrhiBindingKind kind = reflection->bindings[i].kind;
            bool buffer = kind == mrhi_bindingUniformBuffer || kind == mrhi_bindingStorageBuffer ||
                          kind == mrhi_bindingReadOnlyStorageBuffer;
            taken = buffer && shader->indices[i] == index;
        }
        if (taken)
        {
            return true;
        }
    }
    return false;
}

// Whether Metal draws a def as asked: every sample in the mask, since
// Metal has no pipeline sample mask, and vertex buffers clear of the
// container's buffers.
static bool IsDrawable(const MetalShader* shader, const mrhiDriverGraphicsPipeline* pipeline)
{
    const mrhiGraphicsPipelineDef* def = pipeline->def;
    uint32_t samples = (1u << def->sampleCount) - 1u;
    return (def->sampleMask & samples) == samples &&
           !VertexBuffersCollide(shader, pipeline->reflection, pipeline->vertexEntry,
                                 def->vertexBufferCount);
}

// Makes a render pipeline state and its depth and stencil state.
static mrhiResult MakeRender(const mrhiMetalPipelines* pipelines, const MetalShader* shader,
                             const mrhiDriverGraphicsPipeline* pipeline, mrhiMetalPipeline* made)
{
    const mrhiGraphicsPipelineDef* def = pipeline->def;
    bool fragment = pipeline->fragmentEntry < pipeline->reflection->entryCount;
    mrhiResult status = mrhi_errorPlatform;
    @autoreleasepool
    {
        id<MTLFunction> vertex = NewFunction(shader, pipeline->reflection, pipeline->vertexEntry,
                                             def->constants, def->constantCount);
        id<MTLFunction> shading =
            fragment ? NewFunction(shader, pipeline->reflection, pipeline->fragmentEntry,
                                   def->constants, def->constantCount)
                     : nil;
        MTLRenderPipelineDescriptor* descriptor = mrhiMetalDescribeRender(def);
        descriptor.vertexFunction = vertex;
        descriptor.fragmentFunction = shading;
        NSError* error = nil;
        bool functions = vertex != nil && (shading != nil || !fragment);
        made->state = functions ? [pipelines->device newRenderPipelineStateWithDescriptor:descriptor
                                                                                    error:&error]
                                : nil;
        [vertex release];
        [shading release];
        made->depthStencil = mrhiMetalNewDepthStencil(pipelines->device, def);
        bool depth = def->depthStencilFormat == mrhi_formatNone || made->depthStencil != nil;
        status = made->state != nil && depth ? mrhi_success : mrhi_errorPlatform;
    }
    return status;
}

mrhiResult mrhiMetalCreateGraphics(mrhiMetalPipelines* pipelines,
                                   const mrhiDriverGraphicsPipeline* pipeline, uint64_t tag,
                                   uint64_t* handleOut)
{
    const MetalShader* shader = PointerOf(pipeline->shader);
    if (!IsDrawable(shader, pipeline))
    {
        return mrhi_errorUnsupported;
    }
    mrhiMetalPipeline* made = NewPipeline(pipelines, shader, pipeline->reflection, false);
    if (made == nullptr)
    {
        return mrhi_errorCapacity;
    }
    bool fragment = pipeline->fragmentEntry < pipeline->reflection->entryCount;
    made->raster = mrhiMetalRasterOf(pipeline->def);
    made->sizes[0] = shader->sizes[pipeline->vertexEntry];
    made->sizes[1] = fragment ? shader->sizes[pipeline->fragmentEntry] : MRHI_METAL_NONE;
    mrhiResult status = MakeRender(pipelines, shader, pipeline, made);
    if (status != mrhi_success)
    {
        FreePipeline(pipelines, made);
        return status;
    }
    Answer(pipelines, made, tag, handleOut);
    return mrhi_success;
}

void mrhiMetalForgetPipeline(mrhiMetalPipelines* pipelines, uint64_t handle)
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

void mrhiMetalReleasePipeline(const mrhiMetalPipelines* pipelines, uint64_t handle)
{
    FreePipeline(pipelines, PointerOf(handle));
}

size_t mrhiMetalPollPipelines(mrhiMetalPipelines* pipelines, mrhiDriverEvent* events,
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
