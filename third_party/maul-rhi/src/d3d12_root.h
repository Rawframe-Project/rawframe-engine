// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's root signatures (mrhi-0003): made from a
// container's layout and its D3D12 map. Included by the driver's files
// only.

#ifndef MAUL_RHI_SRC_D3D12_ROOT_H
#define MAUL_RHI_SRC_D3D12_ROOT_H

#include "allocator.h"
#include "d3d12_api.h"
#include "driver.h"

// The binding tables a container has, the classes of its heap ranges
// (mrhiD3d12HeapClass), and a root parameter a layout lacks.
#define MRHI_D3D12_TABLES       4
#define MRHI_D3D12_HEAP_CLASSES 3
#define MRHI_D3D12_NO_PARAMETER 0xFF

// Where a root signature's parameters lie: the root block's, the
// constants' and the vertex information's root constants, each table's
// descriptor tables of resources and of samplers, with how many
// descriptors each holds, and the heap tables by range class.
typedef struct mrhiD3d12Layout
{
    uint8_t rootParameter;
    uint8_t constantsParameter;
    uint8_t vertexInfoParameter;
    uint8_t resourceParameters[MRHI_D3D12_TABLES];
    uint8_t samplerParameters[MRHI_D3D12_TABLES];
    uint8_t heapParameters[MRHI_D3D12_HEAP_CLASSES];
    uint16_t resourceCounts[MRHI_D3D12_TABLES];
    uint16_t samplerCounts[MRHI_D3D12_TABLES];
    uint32_t rootWords;
    uint32_t constantWords;
} mrhiD3d12Layout;

// A binding as frames write it: its table, slot and kind, and its
// offset in its table's resource or sampler descriptors.
typedef struct mrhiD3d12Binding
{
    uint16_t slot;
    uint8_t table;
    uint8_t kind;
    uint32_t offset;
} mrhiD3d12Binding;

// Makes the root signature of a container with DXIL, the layout of its
// parameters and each binding's place in its table's descriptors:
// success, mrhi_errorUnsupported when it passes the root signature's 64
// words, mrhi_errorCapacity when the allocator fails, or
// mrhi_errorPlatform when D3D12 refuses it.
mrhiResult mrhiD3d12MakeRoot(const mrhiAllocator* allocator, const mrhiD3d12Api* api,
                             ID3D12Device* device, const mrhiContainer* container,
                             mrhiD3d12Layout* layoutOut, mrhiD3d12Binding* bindingsOut,
                             ID3D12RootSignature** rootOut);

#endif // MAUL_RHI_SRC_D3D12_ROOT_H
