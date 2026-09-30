// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Metal driver's pass encoding (mrhi-0003): a kept pass into the
// frame's command buffer. Included by the driver's Objective-C files
// only.

#ifndef MAUL_RHI_SRC_METAL_ENCODE_H
#define MAUL_RHI_SRC_METAL_ENCODE_H

#include "metal_state.h"

// Encodes a pass: a render encoder for a pass with targets, a compute
// encoder for one without, a blit encoder for a transfer pass.
void mrhiMetalEncodePass(mrhiMetalEncoder* encoder, const mrhiDriverPass* pass);

#endif // MAUL_RHI_SRC_METAL_ENCODE_H
