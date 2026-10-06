// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Popups (record mui-0007), within the library.

#ifndef MAUL_UI_SRC_POPUP_H
#define MAUL_UI_SRC_POPUP_H

#include "context.h"

#include <stdint.h>

// Places the popups under the root at slot root beside their anchors,
// after layout and scrolling; a popup anchored inside another after it.
void muiPlacePopups(muiContext* context, uint32_t root);

// A pointer press on the node at slot, 0 for none: dismisses the popups
// outside which it fell.
void muiPopupPress(muiContext* context, uint32_t slot);

// An Escape no handler took: dismisses the popup set last; whether there
// was one.
bool muiPopupEscape(muiContext* context);

// Focus moved by code or navigation to the node at slot: dismisses the
// popups it left.
void muiPopupFocus(muiContext* context, uint32_t slot);

#endif // MAUL_UI_SRC_POPUP_H
