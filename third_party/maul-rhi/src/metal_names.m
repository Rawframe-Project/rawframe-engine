// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Metal driver's names (mrhi-0003): tables indexed by the
// contract's values, which the core has checked.

#include "metal_names.h"

#include "capabilities_core.h"
#include "invariant.h"

static const MTLPixelFormat s_formats[MRHI_KNOWN_FORMATS + 1] = {
    [mrhi_formatNone] = MTLPixelFormatInvalid,
    [mrhi_formatRgba8Unorm] = MTLPixelFormatRGBA8Unorm,
    [mrhi_formatRgba8UnormSrgb] = MTLPixelFormatRGBA8Unorm_sRGB,
    [mrhi_formatBgra8Unorm] = MTLPixelFormatBGRA8Unorm,
    [mrhi_formatBgra8UnormSrgb] = MTLPixelFormatBGRA8Unorm_sRGB,
    [mrhi_formatR8Unorm] = MTLPixelFormatR8Unorm,
    [mrhi_formatRg8Unorm] = MTLPixelFormatRG8Unorm,
    [mrhi_formatR16Float] = MTLPixelFormatR16Float,
    [mrhi_formatRg16Float] = MTLPixelFormatRG16Float,
    [mrhi_formatRgba16Float] = MTLPixelFormatRGBA16Float,
    [mrhi_formatR32Float] = MTLPixelFormatR32Float,
    [mrhi_formatRg32Float] = MTLPixelFormatRG32Float,
    [mrhi_formatRgba32Float] = MTLPixelFormatRGBA32Float,
    [mrhi_formatR32Uint] = MTLPixelFormatR32Uint,
    [mrhi_formatR32Sint] = MTLPixelFormatR32Sint,
    [mrhi_formatRgb10a2Unorm] = MTLPixelFormatRGB10A2Unorm,
    [mrhi_formatRg11b10Ufloat] = MTLPixelFormatRG11B10Float,
    [mrhi_formatDepth32Float] = MTLPixelFormatDepth32Float,
    [mrhi_formatDepthStencil] = MTLPixelFormatDepth32Float_Stencil8,
// The BC formats' values exist on iOS from 16.4 only. They are only
// numbers here: a texture of one is made only where the device reports
// BC support, which iOS before 16.4 never does (driver_metal.m).
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunguarded-availability-new"
    [mrhi_formatBc1RgbaUnorm] = MTLPixelFormatBC1_RGBA,
    [mrhi_formatBc1RgbaUnormSrgb] = MTLPixelFormatBC1_RGBA_sRGB,
    [mrhi_formatBc2RgbaUnorm] = MTLPixelFormatBC2_RGBA,
    [mrhi_formatBc2RgbaUnormSrgb] = MTLPixelFormatBC2_RGBA_sRGB,
    [mrhi_formatBc3RgbaUnorm] = MTLPixelFormatBC3_RGBA,
    [mrhi_formatBc3RgbaUnormSrgb] = MTLPixelFormatBC3_RGBA_sRGB,
    [mrhi_formatBc4RUnorm] = MTLPixelFormatBC4_RUnorm,
    [mrhi_formatBc4RSnorm] = MTLPixelFormatBC4_RSnorm,
    [mrhi_formatBc5RgUnorm] = MTLPixelFormatBC5_RGUnorm,
    [mrhi_formatBc5RgSnorm] = MTLPixelFormatBC5_RGSnorm,
    [mrhi_formatBc6hRgbUfloat] = MTLPixelFormatBC6H_RGBUfloat,
    [mrhi_formatBc6hRgbFloat] = MTLPixelFormatBC6H_RGBFloat,
    [mrhi_formatBc7RgbaUnorm] = MTLPixelFormatBC7_RGBAUnorm,
    [mrhi_formatBc7RgbaUnormSrgb] = MTLPixelFormatBC7_RGBAUnorm_sRGB,
#pragma clang diagnostic pop
    [mrhi_formatEtc2Rgb8Unorm] = MTLPixelFormatETC2_RGB8,
    [mrhi_formatEtc2Rgb8UnormSrgb] = MTLPixelFormatETC2_RGB8_sRGB,
    [mrhi_formatEtc2Rgb8a1Unorm] = MTLPixelFormatETC2_RGB8A1,
    [mrhi_formatEtc2Rgb8a1UnormSrgb] = MTLPixelFormatETC2_RGB8A1_sRGB,
    [mrhi_formatEtc2Rgba8Unorm] = MTLPixelFormatEAC_RGBA8,
    [mrhi_formatEtc2Rgba8UnormSrgb] = MTLPixelFormatEAC_RGBA8_sRGB,
    [mrhi_formatEacR11Unorm] = MTLPixelFormatEAC_R11Unorm,
    [mrhi_formatEacR11Snorm] = MTLPixelFormatEAC_R11Snorm,
    [mrhi_formatEacRg11Unorm] = MTLPixelFormatEAC_RG11Unorm,
    [mrhi_formatEacRg11Snorm] = MTLPixelFormatEAC_RG11Snorm,
    [mrhi_formatAstc4x4Unorm] = MTLPixelFormatASTC_4x4_LDR,
    [mrhi_formatAstc4x4UnormSrgb] = MTLPixelFormatASTC_4x4_sRGB,
    [mrhi_formatAstc5x4Unorm] = MTLPixelFormatASTC_5x4_LDR,
    [mrhi_formatAstc5x4UnormSrgb] = MTLPixelFormatASTC_5x4_sRGB,
    [mrhi_formatAstc5x5Unorm] = MTLPixelFormatASTC_5x5_LDR,
    [mrhi_formatAstc5x5UnormSrgb] = MTLPixelFormatASTC_5x5_sRGB,
    [mrhi_formatAstc6x5Unorm] = MTLPixelFormatASTC_6x5_LDR,
    [mrhi_formatAstc6x5UnormSrgb] = MTLPixelFormatASTC_6x5_sRGB,
    [mrhi_formatAstc6x6Unorm] = MTLPixelFormatASTC_6x6_LDR,
    [mrhi_formatAstc6x6UnormSrgb] = MTLPixelFormatASTC_6x6_sRGB,
    [mrhi_formatAstc8x5Unorm] = MTLPixelFormatASTC_8x5_LDR,
    [mrhi_formatAstc8x5UnormSrgb] = MTLPixelFormatASTC_8x5_sRGB,
    [mrhi_formatAstc8x6Unorm] = MTLPixelFormatASTC_8x6_LDR,
    [mrhi_formatAstc8x6UnormSrgb] = MTLPixelFormatASTC_8x6_sRGB,
    [mrhi_formatAstc8x8Unorm] = MTLPixelFormatASTC_8x8_LDR,
    [mrhi_formatAstc8x8UnormSrgb] = MTLPixelFormatASTC_8x8_sRGB,
    [mrhi_formatAstc10x5Unorm] = MTLPixelFormatASTC_10x5_LDR,
    [mrhi_formatAstc10x5UnormSrgb] = MTLPixelFormatASTC_10x5_sRGB,
    [mrhi_formatAstc10x6Unorm] = MTLPixelFormatASTC_10x6_LDR,
    [mrhi_formatAstc10x6UnormSrgb] = MTLPixelFormatASTC_10x6_sRGB,
    [mrhi_formatAstc10x8Unorm] = MTLPixelFormatASTC_10x8_LDR,
    [mrhi_formatAstc10x8UnormSrgb] = MTLPixelFormatASTC_10x8_sRGB,
    [mrhi_formatAstc10x10Unorm] = MTLPixelFormatASTC_10x10_LDR,
    [mrhi_formatAstc10x10UnormSrgb] = MTLPixelFormatASTC_10x10_sRGB,
    [mrhi_formatAstc12x10Unorm] = MTLPixelFormatASTC_12x10_LDR,
    [mrhi_formatAstc12x10UnormSrgb] = MTLPixelFormatASTC_12x10_sRGB,
    [mrhi_formatAstc12x12Unorm] = MTLPixelFormatASTC_12x12_LDR,
    [mrhi_formatAstc12x12UnormSrgb] = MTLPixelFormatASTC_12x12_sRGB,
};

MTLPixelFormat mrhiMetalFormat(mrhiFormat format)
{
    return mrhiIsFormatKnown(format) ? s_formats[format] : MTLPixelFormatInvalid;
}

MTLPixelFormat mrhiMetalAspectFormat(MTLPixelFormat format, mrhiTextureAspect aspect)
{
    return format == MTLPixelFormatDepth32Float_Stencil8 && aspect == mrhi_aspectStencilOnly
               ? MTLPixelFormatX32_Stencil8
               : format;
}

MTLTextureType mrhiMetalTextureType(mrhiTextureKind kind, uint32_t samples)
{
    switch (kind)
    {
    case mrhi_texture2dArray:
        return MTLTextureType2DArray;
    case mrhi_textureCube:
        return MTLTextureTypeCube;
    case mrhi_textureCubeArray:
        return MTLTextureTypeCubeArray;
    case mrhi_texture3d:
        return MTLTextureType3D;
    default:
        MRHI_ASSERT(kind == mrhi_texture2d);
        return samples > 1 ? MTLTextureType2DMultisample : MTLTextureType2D;
    }
}

MTLCompareFunction mrhiMetalCompare(mrhiCompareFunction compare)
{
    static const MTLCompareFunction functions[] = {
        [mrhi_compareNone] = MTLCompareFunctionNever,
        [mrhi_compareNever] = MTLCompareFunctionNever,
        [mrhi_compareLess] = MTLCompareFunctionLess,
        [mrhi_compareEqual] = MTLCompareFunctionEqual,
        [mrhi_compareLessEqual] = MTLCompareFunctionLessEqual,
        [mrhi_compareGreater] = MTLCompareFunctionGreater,
        [mrhi_compareNotEqual] = MTLCompareFunctionNotEqual,
        [mrhi_compareGreaterEqual] = MTLCompareFunctionGreaterEqual,
        [mrhi_compareAlways] = MTLCompareFunctionAlways,
    };
    MRHI_ASSERT(compare <= mrhi_compareAlways);
    return functions[compare];
}

MTLSamplerAddressMode mrhiMetalAddress(mrhiAddressMode mode)
{
    static const MTLSamplerAddressMode modes[] = {
        [mrhi_addressClampToEdge] = MTLSamplerAddressModeClampToEdge,
        [mrhi_addressRepeat] = MTLSamplerAddressModeRepeat,
        [mrhi_addressMirrorRepeat] = MTLSamplerAddressModeMirrorRepeat,
    };
    MRHI_ASSERT(mode <= mrhi_addressMirrorRepeat);
    return modes[mode];
}

NSString* mrhiMetalLabel(const char* label, size_t length)
{
    if (length == 0)
    {
        return nil;
    }
    return [[[NSString alloc] initWithBytes:label length:length
                                   encoding:NSUTF8StringEncoding] autorelease];
}
