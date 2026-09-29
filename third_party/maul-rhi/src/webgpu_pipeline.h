// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// WebGPU shaders and pipelines (mrhi-0003): shader modules from the
// container's WGSL; pipeline layouts from the whole container's
// reflection, a bind group layout per binding table and the root block
// as immediates; pipelines made asynchronously, each settling its handle
// into the device state's queue.

#ifndef MAUL_RHI_SRC_WEBGPU_PIPELINE_H
#define MAUL_RHI_SRC_WEBGPU_PIPELINE_H

#include "driver.h"

// Names the contract's enums on the JavaScript side, for pipelines and
// frames; once per module is enough.
void mrhiWebGpuDefineNames(void);

// Makes a shader module from a checked container's WGSL: its handle.
uint64_t mrhiWebGpuCreateShader(int state, const mrhiShaderDef* def,
                                const mrhiContainer* container);

// Starts a pipeline, which settles with success or mrhi_errorPlatform:
// its handle.
uint64_t mrhiWebGpuStartComputePipeline(int state, const mrhiDriverComputePipeline* pipeline);
uint64_t mrhiWebGpuStartGraphicsPipeline(int state, const mrhiDriverGraphicsPipeline* pipeline);

// Takes the next settled pipeline: false when none has settled.
bool mrhiWebGpuTakePipeline(int state, uint64_t* handleOut, mrhiResult* outcomeOut);

#endif // MAUL_RHI_SRC_WEBGPU_PIPELINE_H
