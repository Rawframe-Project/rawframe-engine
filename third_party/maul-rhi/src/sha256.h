// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// SHA-256 (the Secure Hash Standard, FIPS 180), for shader container
// digests.

#ifndef MAUL_RHI_SRC_SHA256_H
#define MAUL_RHI_SRC_SHA256_H

#include <stddef.h>
#include <stdint.h>

// The digest of length bytes; bytes may be NULL when length is 0.
void mrhiSha256(const void* bytes, size_t length, uint8_t digestOut[32]);

#endif // MAUL_RHI_SRC_SHA256_H
