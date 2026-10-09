// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A block's editing state as the editor's commands share it (record
// mui-0006): the block of an id, brought up to date with changes made
// elsewhere, and its selection placed.

#ifndef MAUL_UI_SRC_TEXT_EDITING_H
#define MAUL_UI_SRC_TEXT_EDITING_H

#include "text_block.h"
#include "text_service.h"

#include "maul-ui/base.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_editor.h"

#include <stdint.h>

// The units a press selects, and drags extend by.
enum
{
    MUI_GRAIN_CLUSTER = 0,
    MUI_GRAIN_WORD = 1,
    MUI_GRAIN_PARAGRAPH = 2
};

// An editing block by its id: `mui_success`; `mui_errorInvalid` for the
// null id or a block not editing; `mui_errorStale` for one that is gone.
// Text changed since the editor last saw it empties the history and
// keeps the selection within it.
muiResult muiEditingBlock(const muiTextService* service, muiTextBlockId blockId,
                          muiTextBlock** blockOut);

// muiEditingBlock for an edit: invalid input counted as the service's
// misuse.
muiResult muiEditBlock(muiTextService* service, muiTextBlockId blockId, muiTextBlock** blockOut);

// Refuses an editor's invalid input, counted as the service's misuse
// unless the service is NULL.
muiResult muiRefuseEdit(muiTextService* service);

// Ends a press's gesture: what a drag extends from is the selection's
// anchor, a cluster at a time. Called wherever the selection is placed
// or the text changes by anything but the press and its drags.
void muiEndPress(muiTextEditing* editing);

// Places a block's selection, which ends a run of typing or deleting
// undone together.
void muiPlaceSelection(muiTextBlock* block, muiTextSelection selection);

#endif // MAUL_UI_SRC_TEXT_EDITING_H
