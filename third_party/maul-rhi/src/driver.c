// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The SPI handshake (mrhi-0024): a vtable's version, its size, and its
// functions.

#include "driver.h"

// This SPI version, and at least the size the core knows.
static mrhiResult CheckHead(uint32_t spiVersion, uint32_t size, size_t known)
{
    if (spiVersion != MRHI_SPI_VERSION)
    {
        return mrhi_errorVersion;
    }
    return size >= known ? mrhi_success : mrhi_errorInvalid;
}

mrhiResult mrhiCheckInstanceVtable(const mrhiInstanceDriverVtable* vtable)
{
    if (vtable == nullptr)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = CheckHead(vtable->spiVersion, vtable->size, sizeof(*vtable));
    if (status != mrhi_success)
    {
        return status;
    }
    bool complete = vtable->requestAdapters != nullptr && vtable->poll != nullptr &&
                    vtable->getAdapters != nullptr && vtable->getFormatCaps != nullptr &&
                    vtable->createSurface != nullptr && vtable->destroySurface != nullptr &&
                    vtable->getSurfaceCaps != nullptr && vtable->createDevice != nullptr &&
                    vtable->destroy != nullptr;
    return complete ? mrhi_success : mrhi_errorInvalid;
}

// Whether a device vtable's resource functions are all there.
static bool HasResources(const mrhiDeviceDriverVtable* vtable)
{
    return vtable->createSampler != nullptr && vtable->destroySampler != nullptr &&
           vtable->createBuffer != nullptr && vtable->destroyBuffer != nullptr &&
           vtable->createTexture != nullptr && vtable->destroyTexture != nullptr &&
           vtable->createView != nullptr && vtable->destroyView != nullptr &&
           vtable->createShader != nullptr && vtable->destroyShader != nullptr &&
           vtable->createComputePipeline != nullptr && vtable->createGraphicsPipeline != nullptr &&
           vtable->destroyPipeline != nullptr && vtable->createQuerySet != nullptr &&
           vtable->destroyQuerySet != nullptr && vtable->createHeap != nullptr &&
           vtable->destroyHeap != nullptr && vtable->writeHeapEntry != nullptr &&
           vtable->writeHeapSampler != nullptr;
}

mrhiResult mrhiCheckDeviceVtable(const mrhiDeviceDriverVtable* vtable)
{
    if (vtable == nullptr)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = CheckHead(vtable->spiVersion, vtable->size, sizeof(*vtable));
    if (status != mrhi_success)
    {
        return status;
    }
    bool complete = vtable->destroy != nullptr && HasResources(vtable) &&
                    vtable->configureSurface != nullptr && vtable->unconfigureSurface != nullptr &&
                    vtable->lossReport != nullptr && vtable->acquireImage != nullptr &&
                    vtable->releaseImage != nullptr && vtable->timestampPeriod != nullptr &&
                    vtable->importPipelineCache != nullptr &&
                    vtable->exportPipelineCache != nullptr && vtable->textureMemory != nullptr &&
                    vtable->bufferMemory != nullptr && vtable->submitFrame != nullptr &&
                    vtable->poll != nullptr && vtable->waitFrame != nullptr;
    return complete ? mrhi_success : mrhi_errorInvalid;
}
