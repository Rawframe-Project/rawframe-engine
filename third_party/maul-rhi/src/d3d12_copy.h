// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's copies: between buffers, buffers and textures, and
// textures, the frame's uploads and readbacks among them. Included by
// the driver's files only.

#ifndef MAUL_RHI_SRC_D3D12_COPY_H
#define MAUL_RHI_SRC_D3D12_COPY_H

#include "d3d12_state.h"

// Records a copy command with its payload.
void mrhiD3d12Copy(mrhiD3d12Recorder* recorder, const mrhiCommand* command);

// Copies bytes of the device's zeros into a buffer in the copy
// destination state, in pieces of at most their size.
void mrhiD3d12CopyZeros(const mrhiD3d12Recorder* recorder, ID3D12Resource* target, uint64_t offset,
                        uint64_t bytes);

// Whether a new texture's clear is one only a direct list makes: a
// render target's or a depth texture's, through a view of the kind it
// was made to take.
bool mrhiD3d12ClearsOnDirect(const mrhiTextureDef* def);

#endif // MAUL_RHI_SRC_D3D12_COPY_H
