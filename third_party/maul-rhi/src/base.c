// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The library version. The result names are generated from the
// contract, in generated/result_names.c.

#include "maul-rhi/base.h"

mrhiVersion mrhiGetVersion(void)
{
    return (mrhiVersion){MRHI_VERSION_MAJOR, MRHI_VERSION_MINOR, MRHI_VERSION_PATCH};
}
