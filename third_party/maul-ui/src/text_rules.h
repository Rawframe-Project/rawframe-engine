// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A field's rules over text going in (record mui-0006): control
// characters dropped, line breaks made spaces on a single line, a filter
// keeping the value a number, and a maximum length cutting what goes in.

#ifndef MAUL_UI_SRC_TEXT_RULES_H
#define MAUL_UI_SRC_TEXT_RULES_H

#include "text_block.h"
#include "text_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Writes into out what of a text going in over the bytes of an editing
// block from start up to end its rules let in, and its length; false
// when memory runs out.
bool muiApplyEditRules(muiTextService* service, const muiTextBlock* block, uint32_t start,
                       uint32_t end, const char* text, size_t length, muiBuffer* out,
                       uint32_t* lengthOut);

#endif // MAUL_UI_SRC_TEXT_RULES_H
