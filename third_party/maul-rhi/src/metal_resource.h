// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Metal driver's objects (mrhi-0003): buffers, textures, views,
// samplers and query sets, each a retained Metal object whose pointer is
// its handle. Metal's command buffers retain what they use, so an object
// is released as soon as the core destroys it. Included by the driver's
// Objective-C files only.

#ifndef MAUL_RHI_SRC_METAL_RESOURCE_H
#define MAUL_RHI_SRC_METAL_RESOURCE_H

#include "driver.h"

#import <Metal/Metal.h>

// The object a handle holds, not retained again.
id mrhiMetalObject(uint64_t handle);

// Releases the object a handle holds.
void mrhiMetalRelease(uint64_t handle);

// Each maker answers success with the handle, never zero, or
// mrhi_errorCapacity when Metal makes nothing, out of memory.
mrhiResult mrhiMetalCreateBuffer(id<MTLDevice> device, const mrhiBufferDef* def,
                                 uint64_t* handleOut);
mrhiResult mrhiMetalCreateTexture(id<MTLDevice> device, const mrhiTextureDef* def,
                                  uint64_t* handleOut);
mrhiResult mrhiMetalCreateView(uint64_t texture, const mrhiViewDef* def, uint64_t* handleOut);
mrhiResult mrhiMetalCreateSampler(id<MTLDevice> device, const mrhiSamplerDef* def,
                                  uint64_t* handleOut);
// An occlusion query set is a buffer of 8 bytes per query, which render
// passes take as their visibility results.
mrhiResult mrhiMetalCreateQuerySet(id<MTLDevice> device, const mrhiQuerySetDef* def,
                                   uint64_t* handleOut);

// The bytes and alignment a texture or buffer takes in a Metal heap; 0
// bytes for a transient texture on a device that keeps it in tile memory.
void mrhiMetalTextureMemory(id<MTLDevice> device, const mrhiTextureDef* def, uint64_t* bytesOut,
                            uint64_t* alignmentOut);
void mrhiMetalBufferMemory(id<MTLDevice> device, const mrhiBufferDef* def, uint64_t* bytesOut,
                           uint64_t* alignmentOut);

#endif // MAUL_RHI_SRC_METAL_RESOURCE_H
