// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Input methods on Win32, through imm32. A window that does not accept
// text has no input context, so an input method never takes its keys;
// one that does has the default context. Windows' composition window
// is not shown: the composition comes as mwin_eventImePreedit, its
// segments from the clause attributes, and the result as text. The
// candidate window is placed below the caret, clear of it.

#ifndef MAUL_WINDOW_SRC_WIN32_IME_H
#define MAUL_WINDOW_SRC_WIN32_IME_H

#include "win32.h"

// The UTF-8 bytes of the first index units of count UTF-16 units, a
// pair as one character of four, a lone surrogate as U+FFFD's three.
uint32_t mwinWin32Utf8Before(const WCHAR* units, uint32_t count, uint32_t index);

// A composition's caret, selection and clauses, in UTF-8 bytes, from its
// units, the clause attribute of each (attributeCount of them, those
// past the last unattributed) and the caret in units (-1 for none): the
// target clauses' span is the selection, else it is empty at the caret.
// segments holds MWIN_MAX_PREEDIT_SEGMENTS.
void mwinWin32ShapePreedit(const WCHAR* units, uint32_t count, const BYTE* attributes,
                           LONG attributeCount, LONG caret, mwinPreeditSegment* segments,
                           mwinPreeditEvent* preedit);

// Handles an input method message of a window: true when handled, with
// the result in *result.
bool mwinWin32HandleIme(mwinWin32Window* window, UINT message, WPARAM wParam, LPARAM lParam,
                        LRESULT* result);

// Carries out a text input request of the window in a slot.
mwinOutcome mwinWin32SetTextInput(mwinWin32Window* window, bool enabled, mwinRect caret);

// Takes a new window's input context away until it accepts text.
void mwinWin32StartIme(mwinWin32Window* window);

#endif // MAUL_WINDOW_SRC_WIN32_IME_H
