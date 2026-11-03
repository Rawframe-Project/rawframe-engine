// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The kind of word a Word_Break=Other code point makes, kept out of line
// so that the word rules' per-code-point step stays small enough to
// inline.

#ifndef MAUL_UNICODE_SRC_WORD_KIND_H
#define MAUL_UNICODE_SRC_WORD_KIND_H

#include <stdint.h>

// The first code point of Word_Break=Other that makes a word: U+0E01, the
// first Thai letter (Line_Break=SA). Ideographs and hiragana come later.
// test_segment checks it against the properties of every code point.
#define MUNI_FIRST_WORD_OTHER 0x0E01u

// The muniWordKind of a Word_Break=Other code point: ideographs, hiragana
// and the letters of SA scripts, which a caller's segmenter splits into
// words, make words; anything else is muni_wordNone.
uint8_t muniWordOtherKind(uint32_t codePoint);

#endif // MAUL_UNICODE_SRC_WORD_KIND_H
