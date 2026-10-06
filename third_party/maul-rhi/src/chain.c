// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Walking a def's extension chain.

#include "chain.h"

#include <string.h>

// Whether a def accepts a type: one it lists, or any driver-defined
// type where it lists MRHI_STRUCT_DRIVER_DEFINED itself.
static bool IsKnown(mrhiStructType type, const mrhiStructType* known, size_t knownCount)
{
    for (size_t i = 0; i < knownCount; ++i)
    {
        if (known[i] == type ||
            (known[i] == MRHI_STRUCT_DRIVER_DEFINED && (type & MRHI_STRUCT_DRIVER_DEFINED) != 0))
        {
            return true;
        }
    }
    return false;
}

mrhiResult mrhiCheckChain(const mrhiChain* head, const mrhiStructType* known, size_t knownCount,
                          uint32_t depthLimit)
{
    uint32_t depth = 0;
    for (const mrhiChain* node = head; node != nullptr; node = node->next)
    {
        if (depth == depthLimit)
        {
            return mrhi_errorCapacity;
        }
        ++depth;
        if ((node->type & ~MRHI_CHAIN_HINT) == mrhi_structNone)
        {
            return mrhi_errorInvalid;
        }
        if ((node->type & MRHI_CHAIN_HINT) == 0 && !IsKnown(node->type, known, knownCount))
        {
            return mrhi_errorUnsupported;
        }
    }
    return mrhi_success;
}

bool mrhiIsNameList(const char* names, size_t bytes)
{
    if (names == nullptr)
    {
        return bytes == 0;
    }
    for (size_t at = 0; at < bytes;)
    {
        const char* end = memchr(names + at, 0, bytes - at);
        if (end == nullptr || end == names + at)
        {
            return false;
        }
        at = (size_t)(end - names) + 1;
    }
    return true;
}

const mrhiChain* mrhiFindStruct(const mrhiChain* head, mrhiStructType type)
{
    for (const mrhiChain* node = head; node != nullptr; node = node->next)
    {
        if (node->type == type)
        {
            return node;
        }
    }
    return nullptr;
}
