// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the editor needs of deletions: one found in any text, and one from
// an editing block's caret beside a composition shown.

#ifndef MAUL_UI_SRC_TEXT_DELETE_H
#define MAUL_UI_SRC_TEXT_DELETE_H

#include "text_block.h"
#include "text_service.h"

#include "maul-ui/text_edit.h"

#include <stdbool.h>
#include <stdint.h>

// muiTextBlock_FindDeletion over a text given, which may be one a block
// does not hold.
void muiFindDeletionIn(const char* text, uint32_t length, uint32_t offset, muiTextDeletion deletion,
                       uint32_t* startOut, uint32_t* endOut);

// Where the caret goes as a block's composition shown goes out, in the
// text without it: its start, or after the character the bytes on its two
// sides join into there. No sequence is over four bytes, so the four on
// each side decide it.
uint32_t muiAfterComposition(const muiTextBlock* block);

// What a deletion from the caret of an editing block takes, in the text
// without a composition shown, deleting from where it leaves the caret;
// false when memory for that text runs out. It uses the removed scratch.
bool muiFindEditDeletion(muiTextService* service, const muiTextBlock* block,
                         muiTextDeletion deletion, uint32_t* startOut, uint32_t* endOut);

#endif // MAUL_UI_SRC_TEXT_DELETE_H
