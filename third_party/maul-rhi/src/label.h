// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Debug labels: UTF-8 without NUL, within MRHI_LABEL_BYTES, so that a
// driver can copy one into a fixed, terminated buffer.

#ifndef MAUL_RHI_SRC_LABEL_H
#define MAUL_RHI_SRC_LABEL_H

#include "maul-rhi/base.h"

// Whether a label is valid: NULL only when empty, well-formed UTF-8
// (Unicode's table of well-formed byte sequences: no overlong forms,
// surrogates or code points past U+10FFFF), no NUL, at most
// MRHI_LABEL_BYTES.
bool mrhiIsLabelValid(const char* label, size_t length);

// Whether length bytes, at least 1, are well-formed UTF-8 without NUL.
bool mrhiIsTextValid(const char* text, size_t length);

#endif // MAUL_RHI_SRC_LABEL_H
