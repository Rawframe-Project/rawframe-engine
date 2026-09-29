// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// WebGPU frames (mrhi-0003): recorded at submission into one command
// encoder, one call into JavaScript per command, the browser's
// synchronization in place of barriers; finished when their readbacks
// are mapped and copied into the ring, or when the queue has done their
// work.

#ifndef MAUL_RHI_SRC_WEBGPU_FRAME_H
#define MAUL_RHI_SRC_WEBGPU_FRAME_H

#include "driver.h"

// Records a frame and submits it as the device's frame numbered serial,
// frames numbered from 1 in the order they are submitted.
void mrhiWebGpuSubmitFrame(int state, const mrhiDriverFrame* frame, uint64_t serial);

// The frames finished: every one up to the first still running.
uint64_t mrhiWebGpuFinishedFrames(int state);

#endif // MAUL_RHI_SRC_WEBGPU_FRAME_H
