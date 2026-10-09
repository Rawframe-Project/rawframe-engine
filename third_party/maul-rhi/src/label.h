// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Debug labels: UTF-8 without NUL, within MRHI_LABEL_BYTES, so that a
// driver can copy one into a fixed, terminated buffer.

#ifndef MAUL_RHI_SRC_LABEL_H
#define MAUL_RHI_SRC_LABEL_H

#include "maul-rhi/base.h"

// Labels reach drivers and recorded commands only where the build keeps
// them (MAUL_RHI_LABELS, on by default; mrhi-0029). Without them every
// label is still checked, so that a call refused in one build is
// refused in every build, and then dropped: no driver names an object,
// no label is copied or recorded, and frames keep no label storage.
#ifndef MAUL_RHI_LABELS
#define MAUL_RHI_LABELS 1
#endif

// Drops a checked label the build does not keep.
static inline void mrhiDropLabel(const char** label, size_t* length)
{
    if (!MAUL_RHI_LABELS)
    {
        *label = nullptr;
        *length = 0;
    }
}

// Whether a label is valid: NULL only when empty, well-formed UTF-8
// (Unicode's table of well-formed byte sequences: no overlong forms,
// surrogates or code points past U+10FFFF), no NUL, at most
// MRHI_LABEL_BYTES.
bool mrhiIsLabelValid(const char* label, size_t length);

// Whether length bytes, at least 1, are well-formed UTF-8 without NUL.
bool mrhiIsTextValid(const char* text, size_t length);

#endif // MAUL_RHI_SRC_LABEL_H
