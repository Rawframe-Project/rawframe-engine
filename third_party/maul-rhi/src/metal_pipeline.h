// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Metal driver's shaders and pipelines (mrhi-0003). A shader holds a
// library per entry point, compiled from the container's MSL or taken
// from its metallib, and the Metal map; a pipeline holds its state and
// what its frames bind, copied from the map, so that it outlives its
// shader. Pipelines are made at the call, with no thread, and answered
// at the next poll. Included by the driver's Objective-C files only.

#ifndef MAUL_RHI_SRC_METAL_PIPELINE_H
#define MAUL_RHI_SRC_METAL_PIPELINE_H

#include "driver.h"
#include "metal_graphics.h"

#import <Metal/Metal.h>

// A binding as a pipeline's frames set it: its table and slot, its kind,
// the stages that use it, and its index in its Metal class.
typedef struct mrhiMetalBinding
{
    uint16_t slot;
    uint8_t table;
    uint8_t kind;
    uint8_t stages;
    uint8_t index;
} mrhiMetalBinding;

// A pipeline: its state (a compute or render pipeline state), and for
// frames the workgroup size, the render state set at encoding, the root
// block's bytes and buffer index, each stage's buffer sizes index, and
// its bindings.
typedef struct mrhiMetalPipeline
{
    size_t bytes;
    bool compute;
    id state;
    id<MTLDepthStencilState> depthStencil;
    MTLSize workgroup;
    mrhiMetalRaster raster;
    uint32_t rootBytes;
    uint8_t root;
    // The vertex and fragment entries' buffer sizes, or the compute
    // entry's first.
    uint8_t sizes[2];
    uint32_t bindingCount;
    const mrhiMetalBinding* bindings;
} mrhiMetalPipeline;

// The pipelines made and not yet answered, at most one per pipeline the
// device may hold.
typedef struct mrhiMetalPipelines
{
    const mrhiAllocator* allocator;
    id<MTLDevice> device;
    mrhiDriverEvent* pending;
    uint64_t* pendingHandles;
    uint32_t pendingCount;
    uint32_t pendingLimit;
} mrhiMetalPipelines;

// Makes a shader: success with its handle, mrhi_errorUnsupported for a
// container without Metal code, mrhi_errorCapacity when the allocator
// fails, or mrhi_errorPlatform when Metal refuses the code.
mrhiResult mrhiMetalCreateShader(const mrhiMetalPipelines* pipelines, const mrhiShaderDef* def,
                                 const mrhiContainer* container, uint64_t* handleOut);
void mrhiMetalDestroyShader(const mrhiMetalPipelines* pipelines, uint64_t handle);

// Make pipelines answered at the next poll: success with the handle,
// mrhi_errorUnsupported for what Metal cannot do as asked (a partial
// sample mask, vertex buffers on the container's buffer indices, a
// workgroup past the pipeline's threads), mrhi_errorCapacity when the
// allocator fails, or mrhi_errorPlatform when Metal refuses.
mrhiResult mrhiMetalCreateCompute(mrhiMetalPipelines* pipelines,
                                  const mrhiDriverComputePipeline* pipeline, uint64_t tag,
                                  uint64_t* handleOut);
mrhiResult mrhiMetalCreateGraphics(mrhiMetalPipelines* pipelines,
                                   const mrhiDriverGraphicsPipeline* pipeline, uint64_t tag,
                                   uint64_t* handleOut);
// Drops a destroyed pipeline's answer, if it has not been polled yet, so
// that it is never given.
void mrhiMetalForgetPipeline(mrhiMetalPipelines* pipelines, uint64_t handle);

// Frees a destroyed pipeline once no frame can name it.
void mrhiMetalReleasePipeline(const mrhiMetalPipelines* pipelines, uint64_t handle);

// Moves up to capacity answers into events and returns how many.
size_t mrhiMetalPollPipelines(mrhiMetalPipelines* pipelines, mrhiDriverEvent* events,
                              size_t capacity);

#endif // MAUL_RHI_SRC_METAL_PIPELINE_H
