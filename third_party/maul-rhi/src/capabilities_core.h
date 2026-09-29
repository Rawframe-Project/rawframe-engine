// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The core's checks of features and limits, generated from the
// contract in generated/capabilities.c.

#ifndef MAUL_RHI_SRC_CAPABILITIES_CORE_H
#define MAUL_RHI_SRC_CAPABILITIES_CORE_H

#include "maul-rhi/pipeline.h"

// Whether every feature asked for is granted.
bool mrhiFeaturesWithin(const mrhiFeatures* asked, const mrhiFeatures* granted);

// Whether every limit asked for is within the grant: at most it, or at
// least it for a limit that is better lower (the alignments).
bool mrhiLimitsWithin(const mrhiLimits* asked, const mrhiLimits* granted);

// Clears the features a driver's API cannot grant: the rows the
// contract classes absent-rejected.
void mrhiMaskFeatures(mrhiFeatures* features, mrhiDriverKind driver);

// The formats the contract lists, in order.
#define MRHI_KNOWN_FORMATS 70
extern const mrhiFormat mrhiKnownFormats[MRHI_KNOWN_FORMATS];

// Whether a value is a format the contract lists.
bool mrhiIsFormatKnown(mrhiFormat format);

// What every adapter can do with a format: WebGPU's guaranteed
// capabilities; nothing for the compressed families.
mrhiFormatCaps mrhiFloorFormatCaps(mrhiFormat format);

// Whether the feature a format's family needs is granted; true for a
// format without a family.
bool mrhiFormatFamilyGranted(mrhiFormat format, const mrhiFeatures* features);

// What a color format is as a render target: its channels, the scalar
// type of the outputs that write it, and its bytes per sample and their
// alignment toward the color bytes limit. All zero for other formats.
typedef struct mrhiFormatTarget
{
    uint8_t channels;
    mrhiScalarType scalar;
    uint8_t bytes;
    uint8_t alignment;
} mrhiFormatTarget;

mrhiFormatTarget mrhiGetFormatTarget(mrhiFormat format);

// What a vertex format feeds: the scalar type of the inputs it suits,
// its components, and its bytes. All zero for no format.
typedef struct mrhiVertexLayout
{
    mrhiScalarType scalar;
    uint8_t components;
    uint8_t bytes;
} mrhiVertexLayout;

mrhiVertexLayout mrhiGetVertexLayout(mrhiVertexFormat format);

// A format's block in texels: 1 by 1 but for the compressed families.
typedef struct mrhiFormatBlock
{
    uint32_t width;
    uint32_t height;
} mrhiFormatBlock;

mrhiFormatBlock mrhiGetFormatBlock(mrhiFormat format);

// What a texel copy moves of a format's aspect (mrhi_aspectAll for a
// color format, depth or stencil only for a depth format): the bytes of
// one block, and whether the aspect may be copied from and to, as
// WebGPU allows. All zero for an aspect that is never copied.
typedef struct mrhiFormatCopy
{
    uint8_t bytes;
    bool source;
    bool destination;
} mrhiFormatCopy;

mrhiFormatCopy mrhiGetFormatCopy(mrhiFormat format, mrhiTextureAspect aspect);

// The sRGB or linear twin of a format, the one reinterpretation a view
// may make, or mrhi_formatNone.
mrhiFormat mrhiFormatSrgbPair(mrhiFormat format);

// Whether a format has a depth aspect, and a stencil aspect.
bool mrhiFormatHasDepth(mrhiFormat format);
bool mrhiFormatHasStencil(mrhiFormat format);

// A known format's position in mrhiKnownFormats, or MRHI_KNOWN_FORMATS.
uint32_t mrhiFormatIndex(mrhiFormat format);

// Whether every capability asked for is granted.
bool mrhiFormatCapsWithin(const mrhiFormatCaps* asked, const mrhiFormatCaps* granted);

// The bits of each bitflags type the contract lists.
extern const mrhiBufferUsage mrhiBufferUsageKnown;
extern const mrhiTextureUsage mrhiTextureUsageKnown;
extern const mrhiPresentModes mrhiPresentModesKnown;
extern const mrhiAlphaModes mrhiAlphaModesKnown;
extern const mrhiShaderStages mrhiShaderStagesKnown;
extern const mrhiShaderBuiltins mrhiShaderBuiltinsKnown;
extern const mrhiShaderHeapUses mrhiShaderHeapUsesKnown;
extern const mrhiColorWrites mrhiColorWritesKnown;

#endif // MAUL_RHI_SRC_CAPABILITIES_CORE_H
