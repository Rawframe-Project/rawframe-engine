// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a call that fills a caller's array leaves when it fails: zeros in
// the array and its count or size, but the size an array too small
// needed, which the failure itself defines.

#ifndef MRHI_OUTPUTS_H
#define MRHI_OUTPUTS_H

#include "maul-rhi/base.h"

#include <string.h>

// Returns status, after clearing the outputs when it is a failure:
// arrayBytes bytes of arrayOut, and *sizeOut. Either may be NULL.
static inline mrhiResult mrhiClearFailed(mrhiResult status, void* arrayOut, size_t arrayBytes,
                                         size_t* sizeOut)
{
    if (status == mrhi_success)
    {
        return status;
    }
    if (arrayOut != nullptr)
    {
        memset(arrayOut, 0, arrayBytes);
    }
    if (sizeOut != nullptr && status != mrhi_errorCapacity)
    {
        *sizeOut = 0;
    }
    return status;
}

#endif // MRHI_OUTPUTS_H
