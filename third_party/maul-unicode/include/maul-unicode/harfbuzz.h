// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Maul Unicode as HarfBuzz's source of Unicode data: general category,
// combining class, mirroring, script, and the composition and
// decomposition that HarfBuzz's normalizer uses while shaping. Built as
// the static library maul-unicode-harfbuzz when the build option
// MAUL_UNICODE_HARFBUZZ is on; it needs normalization.
//
// The header does not include <hb.h>: it repeats HarfBuzz's own
// declaration of hb_unicode_funcs_t, which C and C++ both allow.

#ifndef MAUL_UNICODE_HARFBUZZ_H
#define MAUL_UNICODE_HARFBUZZ_H

#include "maul-unicode/base.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct hb_unicode_funcs_t hb_unicode_funcs_t;

    /// Creates HarfBuzz Unicode functions backed by Maul Unicode. Pass them
    /// to hb_buffer_set_unicode_funcs; release them with
    /// hb_unicode_funcs_destroy.
    ///
    /// @return New immutable functions. Like hb_unicode_funcs_create, it
    ///         returns HarfBuzz's empty functions when HarfBuzz cannot
    ///         allocate, never NULL.
    /// @par Thread safety
    /// Safe from any thread. The functions it returns are immutable and may
    /// be shared between threads.
    extern hb_unicode_funcs_t* muniCreateHarfBuzzFunctions(void);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UNICODE_HARFBUZZ_H
