// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The grapheme cluster rules as functions of their state, for the
// grapheme iterator and for a line iterator that breaks anywhere.

#ifndef MAUL_UNICODE_SRC_GRAPHEME_H
#define MAUL_UNICODE_SRC_GRAPHEME_H

#include "segmenter.h"

// Whether there is a boundary between the text so far and a code point
// whose Grapheme_Cluster_Break is after. No rule looks ahead.
bool muniGraphemeIsBoundary(const muniGraphemeRules* state, uint8_t after, uint32_t codePoint);

// Moves the state past a code point whose Grapheme_Cluster_Break is value.
void muniGraphemeAdvance(muniGraphemeRules* state, uint8_t value, uint32_t codePoint);

#endif // MAUL_UNICODE_SRC_GRAPHEME_H
