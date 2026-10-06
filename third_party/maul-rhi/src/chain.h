// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The extension chain a def carries (mrhi-0005): critical structs the
// library does not know are refused, hints it does not know are
// skipped, and the chain's depth is bounded, which also stops a cycle.

#ifndef MAUL_RHI_SRC_CHAIN_H
#define MAUL_RHI_SRC_CHAIN_H

#include "maul-rhi/base.h"

// The bit of a struct type that marks a hint an older library may skip.
#define MRHI_CHAIN_HINT 0x80000000u

// Checks a chain against the struct types its def accepts: success, or
// mrhi_errorInvalid for a node without a type, mrhi_errorUnsupported
// for an unknown critical type, mrhi_errorCapacity for more than
// depthLimit nodes. known may be NULL when knownCount is 0; listing
// MRHI_STRUCT_DRIVER_DEFINED accepts every driver-defined type
// (mrhi-0024).
mrhiResult mrhiCheckChain(const mrhiChain* head, const mrhiStructType* known, size_t knownCount,
                          uint32_t depthLimit);

// Whether a list of names is well formed: each not empty and ended by a
// NUL within the bytes; NULL only with no bytes.
bool mrhiIsNameList(const char* names, size_t bytes);

// The chain's first struct of a type, or NULL.
const mrhiChain* mrhiFindStruct(const mrhiChain* head, mrhiStructType type);

#endif // MAUL_RHI_SRC_CHAIN_H
