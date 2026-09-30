// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's passes (mrhi-0003): each kept pass of a frame
// recorded into its command list, with the barriers before it.
// Included by the driver's files only.

#ifndef MAUL_RHI_SRC_D3D12_RECORD_H
#define MAUL_RHI_SRC_D3D12_RECORD_H

#include "d3d12_state.h"

// Records a pass, its barriers first.
void mrhiD3d12RecordPass(mrhiD3d12Recorder* recorder, const mrhiDriverPass* pass);

#endif // MAUL_RHI_SRC_D3D12_RECORD_H
