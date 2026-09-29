// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Input methods on the web: a window that accepts text has a text field
// of its own, invisible, at the caret, which takes the focus from the
// canvas, so the browser's input methods and on-screen keyboards work
// as in any page. Compositions come from its composition events as a
// preedit, one underlined segment, the caret where the field has it;
// text from its input events. The canvas and its field have the focus
// as one. An on-screen keyboard shows while the field has the focus.

#ifndef MAUL_WINDOW_SRC_WEB_TEXT_H
#define MAUL_WINDOW_SRC_WEB_TEXT_H

#include "web.h"

// Carries out a text input or on-screen keyboard request.
mwinOutcome mwinWebSetTextInput(mwinWebPlatform* platform, uint32_t slot, bool enabled,
                                mwinRect caret);
mwinOutcome mwinWebSetVirtualKeyboard(mwinWebPlatform* platform, uint32_t slot, bool visible,
                                      mwinInputPurpose purpose);

// Handles a record of text or of a composition.
void mwinWebHandleTextRecord(mwinWebPlatform* platform, const mwinWebRecord* record);

#endif // MAUL_WINDOW_SRC_WEB_TEXT_H
