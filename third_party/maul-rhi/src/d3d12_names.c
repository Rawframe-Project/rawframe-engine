// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's names (mrhi-0003): tables indexed by the
// contract's values, which the core has checked. D3D12 has no ETC2 or
// ASTC formats, which the contract maps as absent.

#include "d3d12_names.h"

#include "capabilities_core.h"
#include "invariant.h"

// Each format and its typeless family; the family is the format itself
// where it has none other.
typedef struct Name
{
    DXGI_FORMAT format;
    DXGI_FORMAT typeless;
} Name;

static const Name s_names[MRHI_KNOWN_FORMATS + 1] = {
    [mrhi_formatRgba8Unorm] = {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_TYPELESS},
    [mrhi_formatRgba8UnormSrgb] = {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_TYPELESS},
    [mrhi_formatBgra8Unorm] = {DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_TYPELESS},
    [mrhi_formatBgra8UnormSrgb] = {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_TYPELESS},
    [mrhi_formatR8Unorm] = {DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_TYPELESS},
    [mrhi_formatRg8Unorm] = {DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_TYPELESS},
    [mrhi_formatR16Float] = {DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_R16_TYPELESS},
    [mrhi_formatRg16Float] = {DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16_TYPELESS},
    [mrhi_formatRgba16Float] = {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_TYPELESS},
    [mrhi_formatR32Float] = {DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_TYPELESS},
    [mrhi_formatRg32Float] = {DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32_TYPELESS},
    [mrhi_formatRgba32Float] = {DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32A32_TYPELESS},
    [mrhi_formatR32Uint] = {DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32_TYPELESS},
    [mrhi_formatR32Sint] = {DXGI_FORMAT_R32_SINT, DXGI_FORMAT_R32_TYPELESS},
    [mrhi_formatRgb10a2Unorm] = {DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R10G10B10A2_TYPELESS},
    [mrhi_formatRg11b10Ufloat] = {DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R11G11B10_FLOAT},
    [mrhi_formatDepth32Float] = {DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_TYPELESS},
    [mrhi_formatDepthStencil] = {DXGI_FORMAT_D32_FLOAT_S8X24_UINT, DXGI_FORMAT_R32G8X24_TYPELESS},
    [mrhi_formatBc1RgbaUnorm] = {DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC1_TYPELESS},
    [mrhi_formatBc1RgbaUnormSrgb] = {DXGI_FORMAT_BC1_UNORM_SRGB, DXGI_FORMAT_BC1_TYPELESS},
    [mrhi_formatBc2RgbaUnorm] = {DXGI_FORMAT_BC2_UNORM, DXGI_FORMAT_BC2_TYPELESS},
    [mrhi_formatBc2RgbaUnormSrgb] = {DXGI_FORMAT_BC2_UNORM_SRGB, DXGI_FORMAT_BC2_TYPELESS},
    [mrhi_formatBc3RgbaUnorm] = {DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_BC3_TYPELESS},
    [mrhi_formatBc3RgbaUnormSrgb] = {DXGI_FORMAT_BC3_UNORM_SRGB, DXGI_FORMAT_BC3_TYPELESS},
    [mrhi_formatBc4RUnorm] = {DXGI_FORMAT_BC4_UNORM, DXGI_FORMAT_BC4_TYPELESS},
    [mrhi_formatBc4RSnorm] = {DXGI_FORMAT_BC4_SNORM, DXGI_FORMAT_BC4_TYPELESS},
    [mrhi_formatBc5RgUnorm] = {DXGI_FORMAT_BC5_UNORM, DXGI_FORMAT_BC5_TYPELESS},
    [mrhi_formatBc5RgSnorm] = {DXGI_FORMAT_BC5_SNORM, DXGI_FORMAT_BC5_TYPELESS},
    [mrhi_formatBc6hRgbUfloat] = {DXGI_FORMAT_BC6H_UF16, DXGI_FORMAT_BC6H_TYPELESS},
    [mrhi_formatBc6hRgbFloat] = {DXGI_FORMAT_BC6H_SF16, DXGI_FORMAT_BC6H_TYPELESS},
    [mrhi_formatBc7RgbaUnorm] = {DXGI_FORMAT_BC7_UNORM, DXGI_FORMAT_BC7_TYPELESS},
    [mrhi_formatBc7RgbaUnormSrgb] = {DXGI_FORMAT_BC7_UNORM_SRGB, DXGI_FORMAT_BC7_TYPELESS},
};

DXGI_FORMAT mrhiD3d12Format(mrhiFormat format)
{
    return mrhiIsFormatKnown(format) ? s_names[format].format : DXGI_FORMAT_UNKNOWN;
}

DXGI_FORMAT mrhiD3d12Typeless(mrhiFormat format)
{
    return mrhiIsFormatKnown(format) ? s_names[format].typeless : DXGI_FORMAT_UNKNOWN;
}

DXGI_FORMAT mrhiD3d12ViewFormat(mrhiFormat format, mrhiTextureAspect aspect)
{
    switch (format)
    {
    case mrhi_formatDepth32Float:
        return DXGI_FORMAT_R32_FLOAT;
    case mrhi_formatDepthStencil:
        return aspect == mrhi_aspectStencilOnly ? DXGI_FORMAT_X32_TYPELESS_G8X24_UINT
                                                : DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    default:
        return mrhiD3d12Format(format);
    }
}

static D3D12_FILTER_TYPE FilterType(mrhiFilter mode)
{
    return mode == mrhi_filterLinear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;
}

D3D12_FILTER mrhiD3d12Filter(const mrhiSamplerDef* def)
{
    D3D12_FILTER_REDUCTION_TYPE reduction = def->compare != mrhi_compareNone
                                                ? D3D12_FILTER_REDUCTION_TYPE_COMPARISON
                                                : D3D12_FILTER_REDUCTION_TYPE_STANDARD;
    if (def->maxAnisotropy > 1)
    {
        return D3D12_ENCODE_ANISOTROPIC_FILTER(reduction);
    }
    return D3D12_ENCODE_BASIC_FILTER(FilterType(def->minFilter), FilterType(def->magFilter),
                                     FilterType(def->mipFilter), reduction);
}

D3D12_TEXTURE_ADDRESS_MODE mrhiD3d12Address(mrhiAddressMode mode)
{
    static const D3D12_TEXTURE_ADDRESS_MODE modes[] = {
        [mrhi_addressClampToEdge] = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        [mrhi_addressRepeat] = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        [mrhi_addressMirrorRepeat] = D3D12_TEXTURE_ADDRESS_MODE_MIRROR,
    };
    MRHI_ASSERT(mode <= mrhi_addressMirrorRepeat);
    return modes[mode];
}

D3D12_COMPARISON_FUNC mrhiD3d12Compare(mrhiCompareFunction compare)
{
    static const D3D12_COMPARISON_FUNC functions[] = {
        [mrhi_compareNone] = D3D12_COMPARISON_FUNC_NEVER,
        [mrhi_compareNever] = D3D12_COMPARISON_FUNC_NEVER,
        [mrhi_compareLess] = D3D12_COMPARISON_FUNC_LESS,
        [mrhi_compareEqual] = D3D12_COMPARISON_FUNC_EQUAL,
        [mrhi_compareLessEqual] = D3D12_COMPARISON_FUNC_LESS_EQUAL,
        [mrhi_compareGreater] = D3D12_COMPARISON_FUNC_GREATER,
        [mrhi_compareNotEqual] = D3D12_COMPARISON_FUNC_NOT_EQUAL,
        [mrhi_compareGreaterEqual] = D3D12_COMPARISON_FUNC_GREATER_EQUAL,
        [mrhi_compareAlways] = D3D12_COMPARISON_FUNC_ALWAYS,
    };
    MRHI_ASSERT(compare <= mrhi_compareAlways);
    return functions[compare];
}

void mrhiD3d12Label(ID3D12Object* object, const char* label, size_t labelLength)
{
    WCHAR name[MRHI_LABEL_BYTES + 1];
    int length = labelLength == 0 ? 0
                                  : MultiByteToWideChar(CP_UTF8, 0, label, (int)labelLength, name,
                                                        MRHI_LABEL_BYTES);
    if (length > 0)
    {
        name[length] = 0;
        (void)ID3D12Object_SetName(object, name);
    }
}
