// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text from other programs, made well-formed.

#ifndef MAUL_WINDOW_SRC_UTF8_H
#define MAUL_WINDOW_SRC_UTF8_H

#include <stddef.h>

// The length of text with each maximal ill-formed subpart replaced with
// U+FFFD, written to out unless it is NULL. It is never shorter.
size_t mwinRepairUtf8(const char* bytes, size_t length, char* out);

#endif // MAUL_WINDOW_SRC_UTF8_H
