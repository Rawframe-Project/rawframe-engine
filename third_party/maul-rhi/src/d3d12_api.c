// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Opens d3d12.dll and dxgi.dll (mrhi-0003) and reads the driver's entry
// points. This file also defines the interface ids the headers declare
// (INITGUID), once for the whole driver.

#define INITGUID
#include "d3d12_api.h"

#include <string.h>

// Reads an entry point into the function pointer at pointerOut: false
// when the library lacks it.
static bool Read(HMODULE library, const char* name, void* pointerOut)
{
    FARPROC address = GetProcAddress(library, name);
    // A function pointer read from the platform's untyped symbol.
    static_assert(sizeof(address) == sizeof(PFN_D3D12_CREATE_DEVICE), "symbols are pointers");
    memcpy(pointerOut, (const void*)&address, sizeof(address));
    return address != nullptr;
}

bool mrhiOpenD3d12(mrhiD3d12Api* api)
{
    *api = (mrhiD3d12Api){
        .d3d12 = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        .dxgi = LoadLibraryExW(L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
    };
    bool read = api->d3d12 != nullptr && api->dxgi != nullptr &&
                Read(api->d3d12, "D3D12CreateDevice", (void*)&api->createDevice) &&
                Read(api->d3d12, "D3D12SerializeVersionedRootSignature",
                     (void*)&api->serializeRootSignature) &&
                Read(api->dxgi, "CreateDXGIFactory2", (void*)&api->createFactory);
    if (!read)
    {
        mrhiCloseD3d12(api);
    }
    return read;
}

void mrhiCloseD3d12(mrhiD3d12Api* api)
{
    if (api->d3d12 != nullptr)
    {
        FreeLibrary(api->d3d12);
    }
    if (api->dxgi != nullptr)
    {
        FreeLibrary(api->dxgi);
    }
    *api = (mrhiD3d12Api){0};
}
