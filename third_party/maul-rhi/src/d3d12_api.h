// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's view of the platform (mrhi-0003): the DirectX
// headers kept as published in directx/ with their C interfaces
// (COBJMACROS), DXGI's from the Windows SDK, and the entry points of
// d3d12.dll and dxgi.dll, opened at run time. Included by the driver's
// files only.

#ifndef MAUL_RHI_SRC_D3D12_API_H
#define MAUL_RHI_SRC_D3D12_API_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define COBJMACROS
#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <windows.h>

typedef HRESULT(WINAPI* mrhiCreateDxgiFactory2)(UINT flags, REFIID riid, void** factoryOut);

// The two libraries and what the driver reads from them.
typedef struct mrhiD3d12Api
{
    HMODULE d3d12;
    HMODULE dxgi;
    PFN_D3D12_CREATE_DEVICE createDevice;
    PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE serializeRootSignature;
    mrhiCreateDxgiFactory2 createFactory;
} mrhiD3d12Api;

// Opens d3d12.dll and dxgi.dll from the system directory and reads their
// entry points: false, with nothing left open, where one is missing.
bool mrhiOpenD3d12(mrhiD3d12Api* api);
void mrhiCloseD3d12(mrhiD3d12Api* api);

#endif // MAUL_RHI_SRC_D3D12_API_H
