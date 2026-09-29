// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The implicit phase of the bidi algorithm: rules X10, W1 to W7, N0 to
// N2 and I1 to I2, after muniBidiResolveExplicit.

#ifndef MAUL_UNICODE_SRC_BIDI_IMPLICIT_H
#define MAUL_UNICODE_SRC_BIDI_IMPLICIT_H

#include "bidi_core.h"

// Resolves every isolating run sequence of the paragraph and writes the
// final level of every byte. A code point rule X9 removed takes the level
// of the code point before it, or the paragraph level at the start.
void muniBidiResolveImplicit(const muniBidiParagraph* paragraph);

#endif // MAUL_UNICODE_SRC_BIDI_IMPLICIT_H
