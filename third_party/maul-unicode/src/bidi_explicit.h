// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The paragraph and explicit phases of the bidi algorithm: rules P1 to
// P3 and X1 to X9.

#ifndef MAUL_UNICODE_SRC_BIDI_EXPLICIT_H
#define MAUL_UNICODE_SRC_BIDI_EXPLICIT_H

#include "bidi_core.h"

// The length of the first paragraph of text (rule P1): up to and
// including its first paragraph separator, a CR LF pair counting as one.
size_t muniBidiParagraphLength(const uint8_t* text, size_t length);

// Rules P2 and P3 from index, within the paragraph: 1 when the first
// strong class outside isolates is R or AL, 0 when it is L, and
// fallback when there is none. With stopAtPdi, the search also ends at
// the PDI that closes an isolate starting just before index.
uint8_t muniBidiFirstStrongLevel(const muniBidiParagraph* paragraph, size_t index, bool stopAtPdi,
                                 uint8_t fallback);

// Rules X1 to X9 over the paragraph: writes each code point's explicit
// level and class, and marks continuation bytes and removed code points.
// Returns false when the implicit phase can only give level 0 everywhere:
// the paragraph level is 0 and no code point is R, AL, AN or an explicit
// formatting character, so every class resolves to L.
bool muniBidiResolveExplicit(const muniBidiParagraph* paragraph);

#endif // MAUL_UNICODE_SRC_BIDI_EXPLICIT_H
