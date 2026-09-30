// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's draws (mrhi-0003): dynamic state, draws and
// dispatches, direct and indirect, and occlusion queries. Included by
// the driver's files only.

#ifndef MAUL_RHI_SRC_D3D12_DRAW_H
#define MAUL_RHI_SRC_D3D12_DRAW_H

#include "command.h"
#include "d3d12_state.h"

// Sets a viewport, scissor, blend constant or stencil reference from a
// command with its payload.
void mrhiD3d12SetState(const mrhiD3d12Recorder* recorder, const mrhiCommand* command);

// Records a draw or dispatch, direct or indirect.
void mrhiD3d12Draw(mrhiD3d12Recorder* recorder, const mrhiCommand* command);

// Records an occlusion query's begin or end, or a resolve of queries.
void mrhiD3d12Query(mrhiD3d12Recorder* recorder, const mrhiCommand* command);

// Writes the pass's timestamp at its start or its end, where it has one.
void mrhiD3d12PassTimestamp(const mrhiD3d12Recorder* recorder, bool end);

#endif // MAUL_RHI_SRC_D3D12_DRAW_H
