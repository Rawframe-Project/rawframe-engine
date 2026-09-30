// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's views and samplers (mrhi-0003). A view's record
// holds a shader resource view where it is sampled and an unordered
// access view of its first mip where it is stored to, written in its
// slot of the CPU-only heap when it is made. A 2D view of a texture's
// first layer is a 2D view, and one of a later layer a one-layer array,
// since only arrays name their first layer; cubes alike. A stencil view
// reads the stencil plane.

#include "d3d12_names.h"
#include "d3d12_resource.h"
#include "invariant.h"

static D3D12_CPU_DESCRIPTOR_HANDLE At(D3D12_CPU_DESCRIPTOR_HANDLE start, UINT step, uint32_t index)
{
    return (D3D12_CPU_DESCRIPTOR_HANDLE){.ptr = start.ptr + (SIZE_T)step * index};
}

D3D12_CPU_DESCRIPTOR_HANDLE mrhiD3d12ViewDescriptor(const mrhiD3d12Objects* objects, uint64_t view,
                                                    bool write)
{
    MRHI_ASSERT(view != 0);
    return At(objects->viewStart, objects->viewStep, 2 * (uint32_t)(view - 1) + (write ? 1 : 0));
}

D3D12_CPU_DESCRIPTOR_HANDLE mrhiD3d12SamplerDescriptor(const mrhiD3d12Objects* objects,
                                                       uint64_t sampler)
{
    MRHI_ASSERT(sampler != 0);
    return At(objects->samplerStart, objects->samplerStep, (uint32_t)(sampler - 1));
}

// The plane a view's aspect reads: the stencil's is the second.
static UINT PlaneOf(const mrhiViewDef* def)
{
    return def->aspect == mrhi_aspectStencilOnly ? 1 : 0;
}

D3D12_SHADER_RESOURCE_VIEW_DESC mrhiD3d12DescribeRead(const mrhiViewDef* def, uint32_t samples)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC desc = {
        .Format = mrhiD3d12ViewFormat(def->format, def->aspect),
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
    };
    bool first = def->baseLayer == 0;
    bool multisampled = samples > 1;
    if (def->kind == mrhi_texture2d && first)
    {
        desc.ViewDimension =
            multisampled ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D;
        if (!multisampled)
        {
            desc.Texture2D = (D3D12_TEX2D_SRV){.MostDetailedMip = def->baseMip,
                                               .MipLevels = def->mipCount,
                                               .PlaneSlice = PlaneOf(def)};
        }
    }
    else if (def->kind == mrhi_texture2d || def->kind == mrhi_texture2dArray)
    {
        desc.ViewDimension = multisampled ? D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY
                                          : D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        if (multisampled)
        {
            desc.Texture2DMSArray = (D3D12_TEX2DMS_ARRAY_SRV){.FirstArraySlice = def->baseLayer,
                                                              .ArraySize = def->layerCount};
        }
        else
        {
            desc.Texture2DArray = (D3D12_TEX2D_ARRAY_SRV){.MostDetailedMip = def->baseMip,
                                                          .MipLevels = def->mipCount,
                                                          .FirstArraySlice = def->baseLayer,
                                                          .ArraySize = def->layerCount,
                                                          .PlaneSlice = PlaneOf(def)};
        }
    }
    else if (def->kind == mrhi_textureCube && first)
    {
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        desc.TextureCube =
            (D3D12_TEXCUBE_SRV){.MostDetailedMip = def->baseMip, .MipLevels = def->mipCount};
    }
    else if (def->kind == mrhi_texture3d)
    {
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        desc.Texture3D =
            (D3D12_TEX3D_SRV){.MostDetailedMip = def->baseMip, .MipLevels = def->mipCount};
    }
    else
    {
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
        desc.TextureCubeArray = (D3D12_TEXCUBE_ARRAY_SRV){.MostDetailedMip = def->baseMip,
                                                          .MipLevels = def->mipCount,
                                                          .First2DArrayFace = def->baseLayer,
                                                          .NumCubes = def->layerCount / 6};
    }
    return desc;
}

D3D12_UNORDERED_ACCESS_VIEW_DESC mrhiD3d12DescribeWrite(const mrhiViewDef* def)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {.Format =
                                                 mrhiD3d12ViewFormat(def->format, def->aspect)};
    if (def->kind == mrhi_texture3d)
    {
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
        desc.Texture3D = (D3D12_TEX3D_UAV){.MipSlice = def->baseMip, .WSize = UINT32_MAX};
    }
    else if (def->kind == mrhi_texture2d && def->baseLayer == 0)
    {
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        desc.Texture2D = (D3D12_TEX2D_UAV){.MipSlice = def->baseMip};
    }
    else
    {
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray = (D3D12_TEX2D_ARRAY_UAV){.MipSlice = def->baseMip,
                                                      .FirstArraySlice = def->baseLayer,
                                                      .ArraySize = def->layerCount};
    }
    return desc;
}

mrhiResult mrhiD3d12CreateView(mrhiD3d12Objects* objects, uint64_t texture, const mrhiViewDef* def,
                               uint64_t* handleOut)
{
    *handleOut = 0;
    uint32_t handle = mrhiD3d12TakeSlot(&objects->viewSlots);
    if (handle == 0)
    {
        return mrhi_errorCapacity;
    }
    const mrhiD3d12Texture* source = &objects->textures[texture - 1];
    mrhiD3d12View* view = &objects->views[handle - 1];
    *view = (mrhiD3d12View){
        .texture = texture,
        .def = *def,
        .read = (def->usage & mrhi_textureSampled) != 0,
        .write = (def->usage & mrhi_textureStorage) != 0,
    };
    view->def.label = nullptr;
    view->def.labelLength = 0;
    if (view->read)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = mrhiD3d12DescribeRead(def, source->def.sampleCount);
        ID3D12Device_CreateShaderResourceView(objects->device, source->resource, &desc,
                                              mrhiD3d12ViewDescriptor(objects, handle, false));
    }
    if (view->write)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc = mrhiD3d12DescribeWrite(def);
        ID3D12Device_CreateUnorderedAccessView(objects->device, source->resource, nullptr, &desc,
                                               mrhiD3d12ViewDescriptor(objects, handle, true));
    }
    *handleOut = handle;
    return mrhi_success;
}

mrhiResult mrhiD3d12CreateSampler(mrhiD3d12Objects* objects, const mrhiSamplerDef* def,
                                  uint64_t* handleOut)
{
    *handleOut = 0;
    uint32_t handle = mrhiD3d12TakeSlot(&objects->samplerSlots);
    if (handle == 0)
    {
        return mrhi_errorCapacity;
    }
    D3D12_SAMPLER_DESC desc = {
        .Filter = mrhiD3d12Filter(def),
        .AddressU = mrhiD3d12Address(def->addressU),
        .AddressV = mrhiD3d12Address(def->addressV),
        .AddressW = mrhiD3d12Address(def->addressW),
        .MaxAnisotropy = def->maxAnisotropy,
        .ComparisonFunc = mrhiD3d12Compare(def->compare),
        .MinLOD = def->lodMin,
        .MaxLOD = def->lodMax,
    };
    ID3D12Device_CreateSampler(objects->device, &desc, mrhiD3d12SamplerDescriptor(objects, handle));
    *handleOut = handle;
    return mrhi_success;
}
