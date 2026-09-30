// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's copies (mrhi-0003): between buffers, buffers and
// textures, and textures, the frame's uploads and readbacks among them.
// Included by the driver's files only.

#ifndef MAUL_RHI_SRC_D3D12_COPY_H
#define MAUL_RHI_SRC_D3D12_COPY_H

#include "d3d12_state.h"

// Records a copy command with its payload.
void mrhiD3d12Copy(mrhiD3d12Recorder* recorder, const mrhiCommand* command);

#endif // MAUL_RHI_SRC_D3D12_COPY_H
