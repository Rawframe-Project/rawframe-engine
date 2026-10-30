// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The text functions the library calls from inside its own: the public
// ones without the busy mark (Maul conventions, Threads), which the call
// they are made from holds.

#ifndef MAUL_UI_SRC_TEXT_ENTRY_H
#define MAUL_UI_SRC_TEXT_ENTRY_H

#include "maul-ui/text_block.h"
#include "maul-ui/text_edit.h"
#include "maul-ui/text_editor.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// muiTextBlock_SetComposition and muiTextBlock_EndComposition.
muiResult muiSetComposition(muiTextService* service, muiTextBlockId blockId, uint32_t offset,
                            const char* text, size_t length, const muiCompositionSegment* segments,
                            uint32_t segmentCount);
muiResult muiEndComposition(muiTextService* service, muiTextBlockId blockId);

// The editing calls: muiTextBlock_Select, _Type, _Paste, _Erase, _EraseTo,
// _Undo and _Redo.
muiResult muiSelectText(muiTextService* service, muiTextBlockId blockId,
                        muiTextSelection selection);
muiResult muiTypeText(muiTextService* service, muiTextBlockId blockId, const char* text,
                      size_t length, bool* changedOut);
muiResult muiPasteText(muiTextService* service, muiTextBlockId blockId, const char* text,
                       size_t length, bool* changedOut);
muiResult muiEraseText(muiTextService* service, muiTextBlockId blockId, muiTextDeletion deletion,
                       bool* changedOut);
muiResult muiEraseTextTo(muiTextService* service, muiTextBlockId blockId, uint32_t offset,
                         bool* changedOut);
muiResult muiUndoText(muiTextService* service, muiTextBlockId blockId, bool* changedOut);
muiResult muiRedoText(muiTextService* service, muiTextBlockId blockId, bool* changedOut);

// muiNode_HitTestText, muiNode_GetTextCaret and muiNode_MoveTextPosition.
muiResult muiHitText(const muiTextHost* host, muiNodeId nodeId, float width, float x, float y,
                     muiTextPosition* positionOut);
muiResult muiCaretOf(const muiTextHost* host, muiNodeId nodeId, float width,
                     muiTextPosition position, muiTextCaret* caretOut);
muiResult muiMoveText(const muiTextHost* host, muiNodeId nodeId, float width, muiTextPosition from,
                      muiTextMovement movement, float preferredX, muiTextPosition* positionOut);

// muiNode_MoveEditCaret, muiNode_HandleEditPress and muiNode_HandleEditDrag.
muiResult muiEditMove(const muiTextHost* host, muiNodeId nodeId, muiTextMovement movement,
                      bool extend);
muiResult muiEditPress(const muiTextHost* host, muiNodeId nodeId, float x, float y,
                       uint32_t clickCount, bool extend);
muiResult muiEditDrag(const muiTextHost* host, muiNodeId nodeId, float x, float y);

#endif // MAUL_UI_SRC_TEXT_ENTRY_H
