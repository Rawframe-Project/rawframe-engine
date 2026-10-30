// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Directional navigation's search, for scrolling's arrow rule.

#ifndef MAUL_UI_SRC_NAVIGATE_H
#define MAUL_UI_SRC_NAVIGATE_H

#include "context.h"

#include "maul-ui/focus.h"

#include <stdint.h>

// The node directional navigation goes to from focus: its link that way,
// else the best candidate in scope's subtree; 0 for none or focus.
uint32_t muiNavigateFind(const muiContext* context, uint32_t scope, uint32_t focus,
                         muiDirection direction);

// muiNode_MoveFocusToward without the busy mark, for the library's own calls
// from inside a call that holds it.
muiResult muiMoveFocusToward(muiContext* context, muiNodeId rootId, uint8_t player,
                             muiDirection direction);

#endif // MAUL_UI_SRC_NAVIGATE_H
