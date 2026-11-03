// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A caller's array or buffer that a call on an owner object writes. The
// call zeroes it over its whole capacity whenever it answers anything but
// mui_success, a failure or a "no" alike, so nothing an earlier call left
// there is read as its answer. A call refused as busy leaves it: host code
// the owner runs made that call, and the buffer may be the one the call in
// progress is filling (maul-conventions.md, Errors and assertions).

#ifndef MAUL_UI_SRC_OUTPUTS_H
#define MAUL_UI_SRC_OUTPUTS_H

#include "maul-ui/base.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Zeroes capacity items of size bytes at array when the call answered
// result, unless it answered mui_success or mui_errorBusy; NULL, which only
// a capacity of 0 may come with, is left alone, as is a capacity no array
// could hold.
static inline void muiZeroOutputs(muiResult result, void* array, size_t capacity, size_t size)
{
    if (result != mui_success && result != mui_errorBusy && array != nullptr && capacity != 0 &&
        capacity <= SIZE_MAX / size)
    {
        memset(array, 0, capacity * size);
    }
}

#endif
