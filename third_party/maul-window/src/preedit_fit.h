// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fits a composition's offsets to its text: input methods
// hand them in bytes, and some hand one inside a character, past the
// text's end, or a selection backwards. The core fits every composition
// a backend posts, so the program only ever sees offsets on character
// boundaries within the text. It also drops the control characters an
// input method or a browser may hand in text or a composition.

#ifndef MAUL_WINDOW_SRC_PREEDIT_FIT_H
#define MAUL_WINDOW_SRC_PREEDIT_FIT_H

#include "maul-window/event.h"

// Fits a composition of well-formed UTF-8 text in place, its segments
// writable at segments (the count may shrink):
// - an offset past the text's end is its end, and the caret below -1
//   is -1;
// - a caret inside a character moves to the character's start;
// - a selection or segment reaching into a character covers it whole
//   (its start moves to the character's start, its end past the
//   character);
// - a selection ending before it starts is put in order;
// - a segment left empty is dropped.
void mwinFitPreedit(mwinPreeditEvent* preedit, mwinPreeditSegment* segments);

// The bytes well-formed UTF-8 text keeps once its control characters
// (U+0000 to U+001F and U+007F to U+009F), which are not text, go.
size_t mwinKeptTextBytes(const char* text, size_t length);

// Copies well-formed UTF-8 text without its control characters to out,
// which holds what mwinKeptTextBytes counts. With a composition (else
// null), moves its caret, selection and segments, writable at segments,
// with the bytes: an offset in or past a character that goes falls
// where it was.
void mwinCopyKeptText(char* out, const char* text, size_t length, mwinPreeditEvent* preedit,
                      mwinPreeditSegment* segments);

#endif // MAUL_WINDOW_SRC_PREEDIT_FIT_H
