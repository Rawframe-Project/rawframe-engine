// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Metal driver's names for the contract's values (mrhi-0003):
// formats, texture shapes, sampler states and labels. Included by the
// driver's Objective-C files only.

#ifndef MAUL_RHI_SRC_METAL_NAMES_H
#define MAUL_RHI_SRC_METAL_NAMES_H

#include "maul-rhi/resources.h"

#import <Metal/Metal.h>

// A format's Metal pixel format; MTLPixelFormatInvalid for one the
// contract does not list. The depth and stencil format is always
// Depth32Float_Stencil8, which every Metal device renders.
MTLPixelFormat mrhiMetalFormat(mrhiFormat format);

// The pixel format a view of one aspect of a texture's format takes: the
// stencil of a depth and stencil format is X32_Stencil8; otherwise the
// format itself.
MTLPixelFormat mrhiMetalAspectFormat(MTLPixelFormat format, mrhiTextureAspect aspect);

// A shape's texture type, multisampled for 2D with more than one sample.
MTLTextureType mrhiMetalTextureType(mrhiTextureKind kind, uint32_t samples);

MTLCompareFunction mrhiMetalCompare(mrhiCompareFunction compare);

MTLSamplerAddressMode mrhiMetalAddress(mrhiAddressMode mode);

// A label as a string the caller's autorelease pool owns; nil for none,
// which Metal's validation refuses as a label: set one only when given.
NSString* mrhiMetalLabel(const char* label, size_t length);

#endif // MAUL_RHI_SRC_METAL_NAMES_H
