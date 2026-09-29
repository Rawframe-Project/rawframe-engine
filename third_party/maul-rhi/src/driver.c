// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The SPI handshake.

#include "driver.h"

bool mrhiIsDriverVtableValid(const mrhiInstanceDriverVtable* vtable)
{
    return vtable != nullptr && vtable->spiVersion == MRHI_SPI_VERSION &&
           vtable->size >= sizeof(mrhiInstanceDriverVtable) && vtable->requestAdapters != nullptr &&
           vtable->poll != nullptr && vtable->getAdapters != nullptr &&
           vtable->getFormatCaps != nullptr && vtable->createDevice != nullptr &&
           vtable->destroy != nullptr;
}
