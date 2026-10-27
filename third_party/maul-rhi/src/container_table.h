// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The container's header and section table, read before any section.

#ifndef MAUL_RHI_SRC_CONTAINER_TABLE_H
#define MAUL_RHI_SRC_CONTAINER_TABLE_H

#include "maul-rhi/base.h"

#include <stddef.h>
#include <stdint.h>

// The sections a table lists at most, and the section types the library
// knows, 1 to MRHI_CONTAINER_TYPES.
#define MRHI_CONTAINER_SECTIONS 64
#define MRHI_CONTAINER_TYPES    15

// A section as the table gives it.
typedef struct mrhiContainerSection
{
    uint32_t type;
    uint64_t offset;
    uint64_t size;
} mrhiContainerSection;

// Checks the header, its digest and the section table: success with the
// sections and their count; mrhi_errorVersion for another version or a
// section type the library does not know, which only a later version
// writes; or mrhi_errorFormat.
mrhiResult mrhiReadContainerTable(const uint8_t* bytes, size_t size, mrhiContainerSection* sections,
                                  uint32_t* countOut);

#endif // MAUL_RHI_SRC_CONTAINER_TABLE_H
