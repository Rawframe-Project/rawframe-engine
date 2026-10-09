// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An editing block's undo history (record mui-0006): the edits made, with
// the bytes each removed and inserted, a run of typing joined word by
// word and a run of deletions one way joined whole.

#ifndef MAUL_UI_SRC_TEXT_HISTORY_H
#define MAUL_UI_SRC_TEXT_HISTORY_H

#include "allocator.h"
#include "text_block.h"

#include <stdbool.h>

// What made an edit, as muiTextEdit's kind: the first three may join the
// edit before them of their kind.
enum
{
    MUI_EDIT_TYPING = 1,
    MUI_EDIT_BACKWARD = 2,
    MUI_EDIT_FORWARD = 3,
    MUI_EDIT_OTHER = 4
};

// Records an edit, its removed and inserted bytes, after those not undone
// and joining the last where it continues it, the oldest dropped past
// the limit; false when memory runs out, which keeps the history.
bool muiRecordEdit(const muiAllocator* allocator, muiTextEditing* editing, const muiTextEdit* edit,
                   const char* removed, const char* inserted);

// Empties a history, its memory kept.
void muiClearHistory(muiTextEditing* editing);

// The bytes an edit removed, followed by those it inserted.
const char* muiEditBytes(const muiTextEditing* editing, const muiTextEdit* edit);

#endif // MAUL_UI_SRC_TEXT_HISTORY_H
