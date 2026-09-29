// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Bracket pairs in the bidi algorithm: BD16 finds them, rule N0 gives
// both brackets of a pair one direction.

#ifndef MAUL_UNICODE_SRC_BIDI_BRACKETS_H
#define MAUL_UNICODE_SRC_BIDI_BRACKETS_H

#include "bidi_core.h"

// BD16 and N0 over one isolating run sequence, after rules W1 to W7.
void muniBidiResolveBrackets(const muniBidiSequence* sequence);

#endif // MAUL_UNICODE_SRC_BIDI_BRACKETS_H
