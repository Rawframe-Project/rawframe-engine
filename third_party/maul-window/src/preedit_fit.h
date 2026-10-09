// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fits a composition's offsets to its text (mwin-0034): input methods
// hand them in bytes, and some hand one inside a character, past the
// text's end, or a selection backwards. The core fits every composition
// a backend posts, so the program only ever sees offsets on character
// boundaries within the text.

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

#endif // MAUL_WINDOW_SRC_PREEDIT_FIT_H
