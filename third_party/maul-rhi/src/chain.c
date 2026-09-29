// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Walking a def's extension chain.

#include "chain.h"

static bool IsKnown(mrhiStructType type, const mrhiStructType* known, size_t knownCount)
{
    for (size_t i = 0; i < knownCount; ++i)
    {
        if (known[i] == type)
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
