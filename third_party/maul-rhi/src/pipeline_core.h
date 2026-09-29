// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What compute and graphics pipelines share (mrhi-0010): constant
// values checked against the reflection, and slots that hold their
// reflection, reserve their answer and carry their tag.

#ifndef MAUL_RHI_SRC_PIPELINE_CORE_H
#define MAUL_RHI_SRC_PIPELINE_CORE_H

#include "device_core.h"

// Whether a pipeline's constant values suit its reflection: each id
// known, given once, with a value its type holds, and every constant
// without a default given.
bool mrhiAreConstantsValid(const mrhiReflection* reflection, const mrhiConstantValue* values,
                           uint32_t count);

// Checks a pipeline def's cookie, chain and label, the device, and its
// shader: the shader, or NULL with the refusal, invalid input counted as
// misuse.
mrhiShaderSlot* mrhiCheckPipelineHead(mrhiDevice* device, mrhiDefHead head, uint32_t cookie,
                                      mrhiShaderId shader, mrhiResult* statusOut);

// Takes a pipeline slot for a checked def, holding the reflection and
// reserving the answer's room: success with the slot, or
// mrhi_errorCapacity.
mrhiResult mrhiTakePipelineSlot(mrhiDevice* device, mrhiPipelineKind kind,
                                mrhiReflection* reflection, uint32_t* index1Out,
                                uint32_t* generationOut);

// Frees a pipeline slot the driver refused, and its hold on the
// reflection.
void mrhiFreePipelineSlot(mrhiDevice* device, uint32_t index1);

// The driver tag of a slot's creation: its slot above its request.
uint64_t mrhiPipelineTag(const mrhiDevice* device, uint32_t index1);

// Counts a started pipeline pending and gives its request.
mrhiRequestId mrhiStartPipeline(mrhiDevice* device, uint32_t index1);

// Destroys a live pipeline of a kind: its pending request answered
// stale, its driver object and its slot; mrhi_errorStale for an id that
// is not a live pipeline of the kind.
mrhiResult mrhiDestroyPipeline(mrhiDevice* device, mrhiPipelineKind kind, uint32_t index1,
                               uint32_t generation);

#endif // MAUL_RHI_SRC_PIPELINE_CORE_H
