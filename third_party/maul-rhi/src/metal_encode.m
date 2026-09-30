// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Metal driver's pass encoding (mrhi-0003). A pass's bindings, root
// block and buffer sizes are kept as the stream sets them and applied
// before each draw or dispatch, through the pipeline's map, so their
// order against the pipeline does not matter. Copies and query resolves
// in a compute pass leave its compute encoder for a blit encoder, and
// the compute encoder that follows takes up the pipeline and state
// again; the pass's open debug groups close on each encoder and open on
// the next, as Metal keeps them per encoder. Metal tracks the hazards
// between encoders of private resources, so the core's barriers need no
// commands. A texture binding is a view of its texture unless it sees
// the whole of it as it is.

#include "metal_encode.h"

#include "capabilities_core.h"
#include "invariant.h"
#include "metal_copy.h"
#include "metal_names.h"
#include "metal_resource.h"

#include "maul-rhi/encoder.h"

#include <string.h>

// Where a pass's commands go.
typedef enum Work
{
    WORK_TRANSFER,
    WORK_RENDER,
    WORK_COMPUTE,
} Work;

static Work WorkOf(const mrhiDriverPass* pass)
{
    if (pass->passClass == mrhi_passTransfer)
    {
        return WORK_TRANSFER;
    }
    bool targets = pass->colorTargetCount > 0 || pass->depthTarget.resource.index1 != 0;
    return targets ? WORK_RENDER : WORK_COMPUTE;
}

static id<MTLCommandEncoder> Current(const mrhiMetalEncoder* encoder)
{
    if (encoder->render != nil)
    {
        return encoder->render;
    }
    return encoder->compute != nil ? (id<MTLCommandEncoder>)encoder->compute : encoder->blit;
}

// Ends the open encoder, closing the debug groups the next one opens.
static void EndEncoder(mrhiMetalEncoder* encoder)
{
    id<MTLCommandEncoder> current = Current(encoder);
    if (current == nil)
    {
        return;
    }
    for (uint32_t i = 0; i < encoder->labelCount; ++i)
    {
        [current popDebugGroup];
    }
    [current endEncoding];
    encoder->render = nil;
    encoder->compute = nil;
    encoder->blit = nil;
}

// Names a new encoder for its pass and opens the pass's debug groups.
static void Opened(const mrhiMetalEncoder* encoder, id<MTLCommandEncoder> opened)
{
    const mrhiDriverPass* pass = encoder->pass;
    if (pass->labelLength > 0)
    {
        opened.label = mrhiMetalLabel(pass->label, pass->labelLength);
    }
    for (uint32_t i = 0; i < encoder->labelCount; ++i)
    {
        [opened pushDebugGroup:encoder->labels[i]];
    }
}

static void StartBlit(mrhiMetalEncoder* encoder)
{
    encoder->blit = [encoder->commands blitCommandEncoder];
    Opened(encoder, encoder->blit);
}

static void StartCompute(mrhiMetalEncoder* encoder)
{
    encoder->compute = [encoder->commands computeCommandEncoder];
    Opened(encoder, encoder->compute);
    if (encoder->pipeline != nullptr)
    {
        [encoder->compute setComputePipelineState:encoder->pipeline->state];
    }
    encoder->dirty = true;
}

static MTLLoadAction LoadOf(mrhiLoadOp load)
{
    return load == mrhi_loadKeep    ? MTLLoadActionLoad
           : load == mrhi_loadClear ? MTLLoadActionClear
                                    : MTLLoadActionDontCare;
}

static id<MTLTexture> TargetOf(const mrhiMetalEncoder* encoder, mrhiResourceId resource)
{
    MRHI_ASSERT(resource.index1 != 0 && resource.index1 <= encoder->frame->resourceCount);
    return encoder->objects[resource.index1 - 1];
}

// Places an attachment on a mip and a layer, or a depth slice of a 3D
// texture.
static void Place(MTLRenderPassAttachmentDescriptor* attachment, id<MTLTexture> texture,
                  uint32_t mip, uint32_t layer)
{
    attachment.texture = texture;
    attachment.level = mip;
    if (texture.textureType == MTLTextureType3D)
    {
        attachment.depthPlane = layer;
    }
    else
    {
        attachment.slice = layer;
    }
}

static void DescribeColor(const mrhiMetalEncoder* encoder, MTLRenderPassDescriptor* descriptor,
                          uint32_t index)
{
    const mrhiDriverPass* pass = encoder->pass;
    const mrhiColorTarget* target = &pass->colorTargets[index];
    MTLRenderPassColorAttachmentDescriptor* color = descriptor.colorAttachments[index];
    Place(color, TargetOf(encoder, target->resource), target->mip, target->layer);
    color.loadAction = LoadOf(target->load);
    color.clearColor = MTLClearColorMake((double)target->clear.red, (double)target->clear.green,
                                         (double)target->clear.blue, (double)target->clear.alpha);
    bool keep = pass->colorStores[index] == mrhi_storeKeep;
    if (target->resolve.index1 == 0)
    {
        color.storeAction = keep ? MTLStoreActionStore : MTLStoreActionDontCare;
        return;
    }
    id<MTLTexture> resolve = TargetOf(encoder, target->resolve);
    color.resolveTexture = resolve;
    color.resolveLevel = target->resolveMip;
    if (resolve.textureType == MTLTextureType3D)
    {
        color.resolveDepthPlane = target->resolveLayer;
    }
    else
    {
        color.resolveSlice = target->resolveLayer;
    }
    color.storeAction =
        keep ? MTLStoreActionStoreAndMultisampleResolve : MTLStoreActionMultisampleResolve;
}

static void DescribeDepth(const mrhiMetalEncoder* encoder, MTLRenderPassDescriptor* descriptor)
{
    const mrhiDriverPass* pass = encoder->pass;
    const mrhiDepthTarget* target = &pass->depthTarget;
    id<MTLTexture> texture = TargetOf(encoder, target->resource);
    mrhiFormat format = encoder->frame->resources[target->resource.index1 - 1].texture->format;
    if (mrhiFormatHasDepth(format))
    {
        MTLRenderPassDepthAttachmentDescriptor* depth = descriptor.depthAttachment;
        Place(depth, texture, target->mip, target->layer);
        depth.loadAction = LoadOf(target->depthLoad);
        depth.clearDepth = (double)target->clearDepth;
        depth.storeAction =
            pass->depthStore == mrhi_storeKeep ? MTLStoreActionStore : MTLStoreActionDontCare;
    }
    if (mrhiFormatHasStencil(format))
    {
        MTLRenderPassStencilAttachmentDescriptor* stencil = descriptor.stencilAttachment;
        Place(stencil, texture, target->mip, target->layer);
        stencil.loadAction = LoadOf(target->stencilLoad);
        stencil.clearStencil = target->clearStencil;
        stencil.storeAction =
            pass->stencilStore == mrhi_storeKeep ? MTLStoreActionStore : MTLStoreActionDontCare;
    }
}

static void StartRender(mrhiMetalEncoder* encoder)
{
    const mrhiDriverPass* pass = encoder->pass;
    MTLRenderPassDescriptor* descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
    for (uint32_t i = 0; i < pass->colorTargetCount; ++i)
    {
        if (pass->colorTargets[i].resource.index1 != 0)
        {
            DescribeColor(encoder, descriptor, i);
        }
    }
    if (pass->depthTarget.resource.index1 != 0)
    {
        DescribeDepth(encoder, descriptor);
    }
    descriptor.renderTargetWidth = pass->width;
    descriptor.renderTargetHeight = pass->height;
    if (pass->occlusionSet != 0)
    {
        descriptor.visibilityResultBuffer = mrhiMetalObject(pass->occlusionSet);
    }
    encoder->render = [encoder->commands renderCommandEncoderWithDescriptor:descriptor];
    Opened(encoder, encoder->render);
}

// The texture a binding sees: the texture itself when the binding sees
// all of it as it is, otherwise a view the frame's pool releases.
static id<MTLTexture> ViewOf(id<MTLTexture> texture, const mrhiCommandBinding* binding)
{
    MTLPixelFormat format =
        mrhiMetalAspectFormat(mrhiMetalFormat(binding->viewFormat), binding->aspect);
    MTLTextureType type = mrhiMetalTextureType(binding->viewKind, (uint32_t)texture.sampleCount);
    NSRange levels = NSMakeRange(binding->baseMip, binding->mipCount);
    NSRange slices = NSMakeRange((NSUInteger)binding->offset, (NSUInteger)binding->size);
    bool cube =
        texture.textureType == MTLTextureTypeCube || texture.textureType == MTLTextureTypeCubeArray;
    NSUInteger all = texture.arrayLength * (cube ? 6 : 1);
    if (format == texture.pixelFormat && type == texture.textureType && levels.location == 0 &&
        levels.length == texture.mipmapLevelCount && slices.location == 0 && slices.length == all)
    {
        return texture;
    }
    return [[texture newTextureViewWithPixelFormat:format
                                       textureType:type
                                            levels:levels
                                            slices:slices] autorelease];
}

static mrhiMetalBound BoundOf(const mrhiMetalEncoder* encoder, const mrhiCommandBinding* binding)
{
    mrhiMetalBound bound = {.slot = binding->slot, .kind = binding->kind};
    switch (binding->kind)
    {
    case mrhi_bindingUniformBuffer:
    case mrhi_bindingStorageBuffer:
    case mrhi_bindingReadOnlyStorageBuffer:
        bound.object = encoder->objects[binding->object - 1];
        bound.offset = binding->offset;
        bound.size = binding->size;
        break;
    case mrhi_bindingSampler:
        bound.object = mrhiMetalObject(binding->offset);
        break;
    default:
        bound.object = ViewOf(encoder->objects[binding->object - 1], binding);
        break;
    }
    return bound;
}

static void Bind(mrhiMetalEncoder* encoder, const mrhiCommand* command)
{
    uint32_t table = command->a;
    uint32_t count = (uint32_t)command->b;
    MRHI_ASSERT(table < MRHI_METAL_TABLES && count <= MRHI_TABLE_BINDINGS);
    for (uint32_t i = 0; i < count; ++i)
    {
        mrhiCommandBinding binding;
        memcpy(&binding, &command[1 + i], sizeof(binding));
        encoder->tables[table][i] = BoundOf(encoder, &binding);
    }
    encoder->tableCounts[table] = count;
    encoder->dirty = true;
}

static const mrhiMetalBound* FindBound(const mrhiMetalEncoder* encoder,
                                       const mrhiMetalBinding* binding)
{
    for (uint32_t i = 0; i < encoder->tableCounts[binding->table]; ++i)
    {
        const mrhiMetalBound* bound = &encoder->tables[binding->table][i];
        if (bound->slot == binding->slot)
        {
            return bound;
        }
    }
    return nullptr;
}

static void SetOnCompute(id<MTLComputeCommandEncoder> compute, const mrhiMetalBinding* binding,
                         const mrhiMetalBound* bound)
{
    if (binding->kind == mrhi_bindingSampler)
    {
        [compute setSamplerState:bound->object atIndex:binding->index];
    }
    else if (binding->kind <= mrhi_bindingReadOnlyStorageBuffer)
    {
        [compute setBuffer:bound->object offset:bound->offset atIndex:binding->index];
    }
    else
    {
        [compute setTexture:bound->object atIndex:binding->index];
    }
}

static void SetOnRender(id<MTLRenderCommandEncoder> render, const mrhiMetalBinding* binding,
                        const mrhiMetalBound* bound)
{
    bool vertex = (binding->stages & mrhi_stageVertex) != 0;
    bool fragment = (binding->stages & mrhi_stageFragment) != 0;
    bool sampler = binding->kind == mrhi_bindingSampler;
    bool buffer = binding->kind <= mrhi_bindingReadOnlyStorageBuffer;
    if (vertex && sampler)
    {
        [render setVertexSamplerState:bound->object atIndex:binding->index];
    }
    if (fragment && sampler)
    {
        [render setFragmentSamplerState:bound->object atIndex:binding->index];
    }
    if (vertex && buffer)
    {
        [render setVertexBuffer:bound->object offset:bound->offset atIndex:binding->index];
    }
    if (fragment && buffer)
    {
        [render setFragmentBuffer:bound->object offset:bound->offset atIndex:binding->index];
    }
    if (vertex && !sampler && !buffer)
    {
        [render setVertexTexture:bound->object atIndex:binding->index];
    }
    if (fragment && !sampler && !buffer)
    {
        [render setFragmentTexture:bound->object atIndex:binding->index];
    }
}

// Sets bytes on every stage of the open encoder at an index.
static void SetBytes(const mrhiMetalEncoder* encoder, const void* bytes, size_t length,
                     uint8_t index, bool vertex, bool fragment)
{
    if (encoder->compute != nil)
    {
        [encoder->compute setBytes:bytes length:length atIndex:index];
        return;
    }
    if (vertex)
    {
        [encoder->render setVertexBytes:bytes length:length atIndex:index];
    }
    if (fragment)
    {
        [encoder->render setFragmentBytes:bytes length:length atIndex:index];
    }
}

// Applies the bindings, the root block and the buffer sizes through the
// pipeline's map, when anything changed.
static void Flush(mrhiMetalEncoder* encoder)
{
    const mrhiMetalPipeline* pipeline = encoder->pipeline;
    MRHI_ASSERT(pipeline != nullptr);
    if (!encoder->dirty)
    {
        return;
    }
    for (uint32_t i = 0; i < pipeline->bindingCount; ++i)
    {
        const mrhiMetalBinding* binding = &pipeline->bindings[i];
        const mrhiMetalBound* bound = FindBound(encoder, binding);
        if (bound == nullptr)
        {
            continue;
        }
        if (binding->kind <= mrhi_bindingReadOnlyStorageBuffer)
        {
            encoder->sizes[binding->index] = (uint32_t)bound->size;
        }
        if (encoder->compute != nil)
        {
            SetOnCompute(encoder->compute, binding, bound);
        }
        else
        {
            SetOnRender(encoder->render, binding, bound);
        }
    }
    if (pipeline->rootBytes > 0 && pipeline->root != MRHI_METAL_NONE)
    {
        SetBytes(encoder, encoder->root, pipeline->rootBytes, pipeline->root, true, true);
    }
    for (int stage = 0; stage < 2; ++stage)
    {
        if (pipeline->sizes[stage] != MRHI_METAL_NONE)
        {
            SetBytes(encoder, encoder->sizes, sizeof(encoder->sizes), pipeline->sizes[stage],
                     stage == 0, stage == 1);
        }
    }
    encoder->dirty = false;
}

static void SetPipeline(mrhiMetalEncoder* encoder, const mrhiCommand* command)
{
    const mrhiMetalPipeline* pipeline = (const mrhiMetalPipeline*)(uintptr_t)command->b;
    encoder->pipeline = pipeline;
    encoder->dirty = true;
    if (pipeline->compute)
    {
        [encoder->compute setComputePipelineState:pipeline->state];
        return;
    }
    id<MTLRenderCommandEncoder> render = encoder->render;
    const mrhiMetalRaster* raster = &pipeline->raster;
    [render setRenderPipelineState:pipeline->state];
    [render setDepthStencilState:pipeline->depthStencil != nil ? pipeline->depthStencil
                                                               : encoder->noDepth];
    [render setCullMode:raster->cull];
    [render setFrontFacingWinding:raster->winding];
    [render setDepthClipMode:raster->clip];
    [render setDepthBias:raster->depthBias
              slopeScale:raster->depthBiasSlope
                   clamp:raster->depthBiasClamp];
}

// Sets the root block or render state from a command and its payload.
static void SetState(mrhiMetalEncoder* encoder, const mrhiCommand* command)
{
    switch (command->type)
    {
    case mrhiCommandRootBlock:
        MRHI_ASSERT(command->a + command->b <= MRHI_METAL_ROOT_BYTES);
        memcpy(encoder->root + command->a, &command[1], (size_t)command->b);
        encoder->dirty = true;
        break;
    case mrhiCommandViewport:
    {
        mrhiViewport viewport;
        memcpy(&viewport, &command[1], sizeof(viewport));
        [encoder->render
            setViewport:(MTLViewport){(double)viewport.x, (double)viewport.y,
                                      (double)viewport.width, (double)viewport.height,
                                      (double)viewport.minDepth, (double)viewport.maxDepth}];
        break;
    }
    case mrhiCommandScissor:
        [encoder->render
            setScissorRect:(MTLScissorRect){command->a, (NSUInteger)command->b,
                                            (NSUInteger)command->c, (NSUInteger)command->d}];
        break;
    case mrhiCommandBlendConstant:
    {
        mrhiClearColor color;
        memcpy(&color, &command[1], sizeof(color));
        [encoder->render setBlendColorRed:color.red
                                    green:color.green
                                     blue:color.blue
                                    alpha:color.alpha];
        break;
    }
    default:
        MRHI_ASSERT(command->type == mrhiCommandStencilReference);
        [encoder->render setStencilReferenceValue:command->a];
        break;
    }
}

static void Label(mrhiMetalEncoder* encoder, const mrhiCommand* command)
{
    id<MTLCommandEncoder> current = Current(encoder);
    if (command->type == mrhiCommandPopDebugGroup)
    {
        MRHI_ASSERT(encoder->labelCount > 0);
        --encoder->labelCount;
        [current popDebugGroup];
        return;
    }
    NSString* label = mrhiMetalLabel((const char*)&command[1], (size_t)command->b);
    label = label != nil ? label : @"";
    if (command->type == mrhiCommandDebugMarker)
    {
        [current insertDebugSignpost:label];
        return;
    }
    MRHI_ASSERT(encoder->labelCount < MRHI_METAL_LABELS);
    encoder->labels[encoder->labelCount++] = label;
    [current pushDebugGroup:label];
}

static void DrawIndexed(const mrhiMetalEncoder* encoder, const mrhiCommand* command)
{
    uint32_t first = (uint32_t)command->c;
    NSUInteger indexBytes = encoder->indexType == MTLIndexTypeUInt32 ? 4 : 2;
    [encoder->render drawIndexedPrimitives:encoder->pipeline->raster.primitive
                                indexCount:command->a
                                 indexType:encoder->indexType
                               indexBuffer:encoder->indexBuffer
                         indexBufferOffset:encoder->indexOffset + first * indexBytes
                             instanceCount:(NSUInteger)command->b
                                baseVertex:(int32_t)(uint32_t)(command->c >> 32)
                              baseInstance:(NSUInteger)command->d];
}

static void Indirect(mrhiMetalEncoder* encoder, const mrhiCommand* command)
{
    id<MTLBuffer> arguments = encoder->objects[command->a - 1];
    NSUInteger offset = (NSUInteger)command->c;
    Flush(encoder);
    switch (command->type)
    {
    case mrhiCommandDrawIndirect:
        [encoder->render drawPrimitives:encoder->pipeline->raster.primitive
                         indirectBuffer:arguments
                   indirectBufferOffset:offset];
        break;
    case mrhiCommandDrawIndexedIndirect:
        [encoder->render drawIndexedPrimitives:encoder->pipeline->raster.primitive
                                     indexType:encoder->indexType
                                   indexBuffer:encoder->indexBuffer
                             indexBufferOffset:encoder->indexOffset
                                indirectBuffer:arguments
                          indirectBufferOffset:offset];
        break;
    default:
        MRHI_ASSERT(command->type == mrhiCommandDispatchIndirect);
        [encoder->compute dispatchThreadgroupsWithIndirectBuffer:arguments
                                            indirectBufferOffset:offset
                                           threadsPerThreadgroup:encoder->pipeline->workgroup];
        break;
    }
}

static void Draw(mrhiMetalEncoder* encoder, const mrhiCommand* command)
{
    switch (command->type)
    {
    case mrhiCommandVertexBuffer:
        [encoder->render setVertexBuffer:encoder->objects[command->b - 1]
                                  offset:(NSUInteger)command->c
                                 atIndex:MRHI_METAL_VERTEX_BUFFER_TOP - command->a];
        break;
    case mrhiCommandIndexBuffer:
        encoder->indexBuffer = encoder->objects[command->b - 1];
        encoder->indexOffset = command->c;
        encoder->indexType =
            command->a == mrhi_indexUint32 ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16;
        break;
    case mrhiCommandDraw:
        Flush(encoder);
        [encoder->render drawPrimitives:encoder->pipeline->raster.primitive
                            vertexStart:(NSUInteger)command->c
                            vertexCount:command->a
                          instanceCount:(NSUInteger)command->b
                           baseInstance:(NSUInteger)command->d];
        break;
    case mrhiCommandDrawIndexed:
        Flush(encoder);
        DrawIndexed(encoder, command);
        break;
    case mrhiCommandDispatch:
        Flush(encoder);
        [encoder->compute dispatchThreadgroups:MTLSizeMake(command->a, (NSUInteger)command->b,
                                                           (NSUInteger)command->c)
                         threadsPerThreadgroup:encoder->pipeline->workgroup];
        break;
    default:
        Indirect(encoder, command);
        break;
    }
}

static void Occlusion(const mrhiMetalEncoder* encoder, const mrhiCommand* command)
{
    bool begin = command->type == mrhiCommandBeginOcclusionQuery;
    [encoder->render setVisibilityResultMode:begin ? MTLVisibilityResultModeBoolean
                                                   : MTLVisibilityResultModeDisabled
                                      offset:(NSUInteger)command->a * 8];
}

static bool IsLabel(const mrhiCommand* command)
{
    return command->type >= mrhiCommandPushDebugGroup && command->type <= mrhiCommandDebugMarker;
}

// Opens the encoder a compute pass's command needs: a blit encoder for a
// copy or a resolve, a compute encoder for the rest but labels.
static void Switch(mrhiMetalEncoder* encoder, const mrhiCommand* command)
{
    bool outside = command->type >= mrhiCommandResolveQueries;
    if (outside && encoder->blit == nil)
    {
        EndEncoder(encoder);
        StartBlit(encoder);
    }
    else if (!outside && !IsLabel(command) && encoder->compute == nil)
    {
        EndEncoder(encoder);
        StartCompute(encoder);
    }
}

static void Encode(mrhiMetalEncoder* encoder, Work work, const mrhiCommand* command)
{
    if (work == WORK_COMPUTE)
    {
        Switch(encoder, command);
    }
    switch (command->type)
    {
    case mrhiCommandGraphicsPipeline:
    case mrhiCommandComputePipeline:
        SetPipeline(encoder, command);
        break;
    case mrhiCommandRootBlock:
    case mrhiCommandViewport:
    case mrhiCommandScissor:
    case mrhiCommandBlendConstant:
    case mrhiCommandStencilReference:
        SetState(encoder, command);
        break;
    case mrhiCommandPushDebugGroup:
    case mrhiCommandPopDebugGroup:
    case mrhiCommandDebugMarker:
        Label(encoder, command);
        break;
    case mrhiCommandBindings:
        Bind(encoder, command);
        break;
    case mrhiCommandBeginOcclusionQuery:
    case mrhiCommandEndOcclusionQuery:
        Occlusion(encoder, command);
        break;
    case mrhiCommandResolveQueries:
        mrhiMetalResolve(encoder, command);
        break;
    default:
        if (command->type >= mrhiCommandCopyBuffer)
        {
            mrhiMetalCopy(encoder, command);
        }
        else
        {
            Draw(encoder, command);
        }
        break;
    }
}

void mrhiMetalEncodePass(mrhiMetalEncoder* encoder, const mrhiDriverPass* pass)
{
    // Heaps and timestamps are not granted on Metal yet, so no pass names
    // one.
    MRHI_ASSERT(pass->heap == 0 && pass->timestampSet == 0);
    encoder->pass = pass;
    encoder->pipeline = nullptr;
    encoder->dirty = false;
    encoder->indexBuffer = nil;
    encoder->labelCount = 0;
    memset(encoder->tableCounts, 0, sizeof(encoder->tableCounts));
    Work work = WorkOf(pass);
    if (work == WORK_RENDER)
    {
        StartRender(encoder);
    }
    else if (work == WORK_COMPUTE)
    {
        StartCompute(encoder);
    }
    else
    {
        StartBlit(encoder);
    }
    for (uint32_t chunk = pass->firstChunk; chunk != 0;
         chunk = encoder->frame->chunks[chunk - 1].next)
    {
        const mrhiCommandChunk* at = &encoder->frame->chunks[chunk - 1];
        for (uint32_t i = 0; i < at->count; i += 1u + at->commands[i].payload)
        {
            Encode(encoder, work, &at->commands[i]);
        }
    }
    EndEncoder(encoder);
}
