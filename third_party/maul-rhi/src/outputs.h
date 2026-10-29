// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a call on an object leaves in its outputs when it fails: zeros,
// in a struct or value it writes and in an array it fills and its count
// or size, but the size an array too small needed, which the failure
// itself defines.

#ifndef MRHI_OUTPUTS_H
#define MRHI_OUTPUTS_H

#include "maul-rhi/base.h"

#include <string.h>

// Clears an output a call writes, unless it is NULL, before the call
// checks anything: a failure leaves the zeros, and a success overwrites
// them.
static inline void mrhiClearOutput(void* out, size_t bytes)
{
    if (out != nullptr)
    {
        memset(out, 0, bytes);
    }
}

// Clears a native handle a call writes, unless out is NULL, before the
// call checks anything.
static inline void mrhiClearHandle(void** out)
{
    if (out != nullptr)
    {
        *out = nullptr;
    }
}

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
