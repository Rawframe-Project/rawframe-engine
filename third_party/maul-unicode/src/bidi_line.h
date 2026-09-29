// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Rule L1 of UAX #9 on levels the caller may change, for parts of the
// library that order a line themselves.

#ifndef MAUL_UNICODE_SRC_BIDI_LINE_H
#define MAUL_UNICODE_SRC_BIDI_LINE_H

#include <stddef.h>
#include <stdint.h>

// Gives segment and paragraph separators the paragraph level, and so the
// whitespace and isolate formatting characters before them or at the end
// of the line, and the characters rule X9 removed among those.
void muniBidiResetLineLevels(const uint8_t* line, uint8_t* levels, size_t length,
                             uint8_t paragraphLevel);

#endif // MAUL_UNICODE_SRC_BIDI_LINE_H
