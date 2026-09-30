// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Metal driver's copies (mrhi-0003): copies between buffers and
// textures, uploads from the frame's staging buffer, readbacks into its
// readback buffer, and query resolves, on the encoder's open blit
// encoder. Included by the driver's Objective-C files only.

#ifndef MAUL_RHI_SRC_METAL_COPY_H
#define MAUL_RHI_SRC_METAL_COPY_H

#include "metal_state.h"

// Encodes a copy, upload or readback command; a readback widens the
// range of the readback buffer the frame writes.
void mrhiMetalCopy(mrhiMetalEncoder* encoder, const mrhiCommand* command);

// Encodes a query resolve: the set's results copied into a buffer.
void mrhiMetalResolve(mrhiMetalEncoder* encoder, const mrhiCommand* command);

#endif // MAUL_RHI_SRC_METAL_COPY_H
