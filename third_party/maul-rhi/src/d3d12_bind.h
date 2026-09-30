// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's bindings (mrhi-0003): pipelines, tables written
// into a frame slot's shader-visible descriptor rings, heap tables
// pointing at a pass's heap (mrhi-0015), root blocks, and
// vertex and index buffers, with the buffers they use moved to the
// states each draw or dispatch needs. Included by the driver's files
// only.

#ifndef MAUL_RHI_SRC_D3D12_BIND_H
#define MAUL_RHI_SRC_D3D12_BIND_H

#include "command.h"
#include "d3d12_state.h"

// Points the heap tables of the pipelines set after at a pass's heap's
// regions in the frame's slot; a heap of 0 leaves them.
void mrhiD3d12EnterHeap(mrhiD3d12Recorder* recorder, uint64_t heap);

// Sets a pipeline, by its handle, with its root signature where that
// changes, its constants, its heap tables and its topology.
void mrhiD3d12SetPipeline(mrhiD3d12Recorder* recorder, uint64_t handle);

// Sets a table from a bindings command with its payload.
void mrhiD3d12BindTable(mrhiD3d12Recorder* recorder, const mrhiCommand* command);

// Sets root block bytes from a command with its payload.
void mrhiD3d12SetRootBlock(const mrhiD3d12Recorder* recorder, const mrhiCommand* command);

// Sets a vertex or index buffer.
void mrhiD3d12BindBuffer(mrhiD3d12Recorder* recorder, const mrhiCommand* command);

// Readies a draw or dispatch: the buffers the bound tables, vertex and
// index buffers use in the states they need, and the vertex buffers set
// with the pipeline's strides.
void mrhiD3d12Ready(mrhiD3d12Recorder* recorder);

#endif // MAUL_RHI_SRC_D3D12_BIND_H
