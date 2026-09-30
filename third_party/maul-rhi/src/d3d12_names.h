// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's names (mrhi-0003): the contract's values as D3D12
// and DXGI take them, from values the core has checked. Included by the
// driver's files only.

#ifndef MAUL_RHI_SRC_D3D12_NAMES_H
#define MAUL_RHI_SRC_D3D12_NAMES_H

#include "d3d12_api.h"
#include "driver.h"

// A format's DXGI format; DXGI_FORMAT_UNKNOWN for one D3D12 lacks.
DXGI_FORMAT mrhiD3d12Format(mrhiFormat format);

// The typeless format a texture is made in when its views see it in
// other formats, or its depth is sampled: the format's family.
DXGI_FORMAT mrhiD3d12Typeless(mrhiFormat format);

// The format a shader resource view reads an aspect of a format in.
DXGI_FORMAT mrhiD3d12ViewFormat(mrhiFormat format, mrhiTextureAspect aspect);

// A sampler's filter, address mode and comparison.
D3D12_FILTER mrhiD3d12Filter(const mrhiSamplerDef* def);
D3D12_TEXTURE_ADDRESS_MODE mrhiD3d12Address(mrhiAddressMode mode);
D3D12_COMPARISON_FUNC mrhiD3d12Compare(mrhiCompareFunction compare);

// Names an object for debugging tools, from a label of labelLength bytes
// of UTF-8; an unnamed object works the same.
void mrhiD3d12Label(ID3D12Object* object, const char* label, size_t labelLength);

#endif // MAUL_RHI_SRC_D3D12_NAMES_H
