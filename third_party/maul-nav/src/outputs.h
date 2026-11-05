// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a public call leaves in its outputs. A call whose first parameter is
// an owner object clears them before it checks anything, and again when it
// fails, so that a failure never leaves an earlier call's values, or part of
// its own, for the caller to read. A call that can be refused as busy, by a
// call back in from host code the object runs, fills copies of its outputs
// instead and gives them only when it was not (mnavGiveOutput): a busy
// refusal writes none of them. A call that answers "no" without failing
// writes nothing past the cleared outputs but the answer. A caller's array
// is zeroed over its capacity on failure (mnavClearArray). The outputs a
// failure defines (the size or count a buffer too small needed with the
// first capacity items, a bake's report) are left to the call
// (docs/internals/overview.md).

#ifndef MAUL_NAV_SRC_OUTPUTS_H
#define MAUL_NAV_SRC_OUTPUTS_H

#include "maul-nav/base.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Sets an output's bytes to 0; NULL is no output.
static inline void mnavClearOutput(void* output, size_t size)
{
    if (output != nullptr)
    {
        memset(output, 0, size);
    }
}

// Clears an output when the call failed, and returns its result.
static inline mnavResult mnavClearOnFailure(mnavResult result, void* output, size_t size)
{
    if (result != mnav_success)
    {
        mnavClearOutput(output, size);
    }
    return result;
}

// Clears a count or size when the call failed for another reason than a
// buffer too small, which defines it.
static inline mnavResult mnavClearUnlessCapacity(mnavResult result, void* output, size_t size)
{
    if (result != mnav_success && result != mnav_errorCapacity)
    {
        mnavClearOutput(output, size);
    }
    return result;
}

// Gives the caller an output a call made in its own copy: the copy when
// the call succeeded or its failure defines the output, zeros on any other
// failure, and nothing when the call was refused as busy, which only a call
// back in from host code the object runs can be: the output may be one the
// call in progress has written or is filling. NULL is no output.
static inline void mnavGiveOutput(mnavResult result, bool defined, void* output, const void* made,
                                  size_t size)
{
    if (output == nullptr || result == mnav_errorBusy)
    {
        return;
    }
    if (result == mnav_success || defined)
    {
        memcpy(output, made, size);
    }
    else
    {
        memset(output, 0, size);
    }
}

// Zeroes a caller's array over its capacity when the call failed and the
// failure defines none of it, so that no earlier call's items are left in
// it. The array is left when the call succeeded, when its failure defines
// the items written (a buffer too small keeps the first capacity), when it
// was refused as busy, as mnavGiveOutput leaves an output, and when the
// span itself is refused (no array, a capacity below 1, or more bytes than
// memory holds).
static inline void mnavClearArray(mnavResult result, bool defined, void* array, int64_t capacity,
                                  size_t itemSize)
{
    if (result == mnav_success || defined || result == mnav_errorBusy || array == nullptr ||
        capacity < 1 || (uint64_t)capacity > SIZE_MAX / itemSize)
    {
        return;
    }
    memset(array, 0, (size_t)capacity * itemSize);
}

// A byte buffer's size as mnavClearArray's capacity: -1, a span refused,
// for one past INT64_MAX, which no memory holds. A size_t of 32 bits never
// is, so the test is compiled only where one could be.
static inline int64_t mnavByteCapacity(size_t bytes)
{
#if SIZE_MAX > INT64_MAX
    if (bytes > (size_t)INT64_MAX)
    {
        return -1;
    }
#endif
    return (int64_t)bytes;
}

#endif // MAUL_NAV_SRC_OUTPUTS_H
