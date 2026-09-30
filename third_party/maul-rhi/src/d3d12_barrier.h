// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's barriers (mrhi-0003): the core's barriers of
// textures as transitions of their subresources, and buffers moved to
// the state each use needs, since the core's plan only orders a
// buffer's writes. Included by the driver's files only.

#ifndef MAUL_RHI_SRC_D3D12_BARRIER_H
#define MAUL_RHI_SRC_D3D12_BARRIER_H

#include "d3d12_state.h"

// The D3D12 state a texture is in for a resource state.
D3D12_RESOURCE_STATES mrhiD3d12TextureState(mrhiResourceState state);

// The D3D12 state a buffer is in for a resource state; the common state
// for one no buffer is in. A sealed buffer is in every read it allows.
D3D12_RESOURCE_STATES mrhiD3d12BufferState(mrhiResourceState state);

// Moves the buffers a pass declares to the states it declares them in,
// since a heap may reach them unbound, and records the moves.
void mrhiD3d12UseDeclared(mrhiD3d12Recorder* recorder, const mrhiDriverPass* pass);

// Records the frame's barriers that come before a pass, or with a null
// id those at the frame's end.
void mrhiD3d12RecordBarriers(mrhiD3d12Recorder* recorder, mrhiPassId pass);

// Moves a frame's buffer, by slot plus one, to a state a use needs: to
// that state, or to the read states it holds and that one together.
void mrhiD3d12Use(mrhiD3d12Recorder* recorder, uint32_t object, D3D12_RESOURCE_STATES needed);

// Queues a transition of a resource's subresource, and records the
// queued ones.
void mrhiD3d12Transition(mrhiD3d12Recorder* recorder, ID3D12Resource* resource, UINT subresource,
                         D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
void mrhiD3d12FlushBarriers(mrhiD3d12Recorder* recorder);

#endif // MAUL_RHI_SRC_D3D12_BARRIER_H
