// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The walk of a pipeline the core hands a driver: the rules its
// shader's reflection decides, written again from WebGPU apart from the
// core's checks, so that a slip in one does not hide in the other. The
// test driver traps on what it finds.

#ifndef MAUL_RHI_SRC_PIPELINE_WALK_H
#define MAUL_RHI_SRC_PIPELINE_WALK_H

#include "driver.h"

// Counts the rules a compute pipeline breaks: each constant value names
// a constant of the reflection, at most once, with a value its type
// holds, and every required constant is given.
uint32_t mrhiWalkComputePipeline(const mrhiDriverComputePipeline* pipeline);

// Counts the rules a graphics pipeline breaks on a device with limits:
// its constants, as a compute pipeline's, and the vertex inputs, color
// outputs, blend sources, sample mask and inter-stage variables its def
// must agree with.
uint32_t mrhiWalkGraphicsPipeline(const mrhiDriverGraphicsPipeline* pipeline,
                                  const mrhiLimits* limits);

#endif // MAUL_RHI_SRC_PIPELINE_WALK_H
