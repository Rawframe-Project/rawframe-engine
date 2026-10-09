// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Editing a text block (record mui-0006): its rules, its selection, and
// the edits typing, pasting, deleting, undoing and redoing make, each
// recorded before the text changes and dropped again if it cannot.

#include "text_editing.h"

#include "text_blocks.h"
#include "text_history.h"
#include "text_rules.h"
#include "text_service.h"

#include "maul-ui/text_edit.h"

#include <string.h>

enum
{
    // Edits a single-line field keeps by default.
    DEFAULT_UNDO_LIMIT = 100
};

muiTextEditDef muiDefaultTextEditDef(void)
{
    return (muiTextEditDef){0, mui_filterNone, mui_purposeText, 0, DEFAULT_UNDO_LIMIT};
}

// An offset kept within a block's text, at a character's start.
static uint32_t Within(const muiTextBlock* block, uint32_t at)
{
    at = at < block->length ? at : block->length;
    while (!muiIsCharacterStart(block, at))
    {
        at--;
    }
    return at;
}

static muiTextSelection Collapsed(uint32_t at)
{
    return (muiTextSelection){at, {at, mui_affinityDownstream}};
}

void muiEndPress(muiTextEditing* editing)
{
    editing->grain = MUI_GRAIN_CLUSTER;
    editing->pressStart = editing->selection.anchor;
    editing->pressEnd = editing->selection.anchor;
}

void muiPlaceSelection(muiTextBlock* block, muiTextSelection selection)
{
    block->editing.selection = selection;
    block->editing.open = 0;
}

muiResult muiEditingBlock(const muiTextService* service, muiTextBlockId blockId,
                          muiTextBlock** blockOut)
{
    if (service == nullptr || blockId.index1 == 0)
    {
        return mui_errorInvalid;
    }
    muiTextBlock* block = muiResolveTextBlock(service, blockId);
    if (block == nullptr)
    {
        return mui_errorStale;
    }
    muiTextEditing* editing = &block->editing;
    if (!editing->on)
    {
        return mui_errorInvalid;
    }
    if (editing->revision != block->revision)
    {
        muiClearHistory(editing);
        editing->selection.anchor = Within(block, editing->selection.anchor);
        editing->selection.caret.offset = Within(block, editing->selection.caret.offset);
        editing->preferredX = -1.0f;
        muiEndPress(editing);
        editing->revision = block->revision;
    }
    *blockOut = block;
    return mui_success;
}

muiResult muiEditBlock(muiTextService* service, muiTextBlockId blockId, muiTextBlock** blockOut)
{
    return muiCountText(service, muiEditingBlock(service, blockId, blockOut));
}

muiResult muiRefuseEdit(muiTextService* service)
{
    return muiRefuseText(service);
}

muiResult muiTextBlock_SetEditing(muiTextService* service, muiTextBlockId blockId,
                                  const muiTextEditDef* def)
{
    if (service == nullptr || blockId.index1 == 0 ||
        (def != nullptr &&
         ((def->flags & ~(mui_editMultiline | mui_editReadOnly | mui_editPassword)) != 0 ||
          def->filter > mui_filterDecimal || def->purpose > mui_purposeUrl)))
    {
        return muiRefuseText(service);
    }
    muiTextBlock* block = muiResolveTextBlock(service, blockId);
    if (block == nullptr)
    {
        return mui_errorStale;
    }
    muiTextEditing* editing = &block->editing;
    muiClearHistory(editing);
    if (def == nullptr)
    {
        muiFreeBuffer(&service->allocator, &editing->entries);
        muiFreeBuffer(&service->allocator, &editing->bytes);
        *editing = (muiTextEditing){0};
        return mui_success;
    }
    editing->on = true;
    editing->def = *def;
    editing->selection = Collapsed(block->length);
    editing->preferredX = -1.0f;
    muiEndPress(editing);
    editing->revision = block->revision;
    return mui_success;
}

muiResult muiTextBlock_GetSelection(const muiTextService* service, muiTextBlockId blockId,
                                    muiTextSelection* selectionOut)
{
    muiTextBlock* block = nullptr;
    muiResult result =
        selectionOut != nullptr ? muiEditingBlock(service, blockId, &block) : mui_errorInvalid;
    if (result == mui_success)
    {
        *selectionOut = block->editing.selection;
    }
    return result;
}

muiResult muiTextBlock_Select(muiTextService* service, muiTextBlockId blockId,
                              muiTextSelection selection)
{
    muiTextBlock* block = nullptr;
    muiResult result = muiEditBlock(service, blockId, &block);
    if (result != mui_success)
    {
        return result;
    }
    if (!muiIsCharacterStart(block, selection.anchor) ||
        !muiIsCharacterStart(block, selection.caret.offset) ||
        selection.caret.affinity > mui_affinityUpstream)
    {
        return muiRefuseText(service);
    }
    muiPlaceSelection(block, selection);
    block->editing.preferredX = -1.0f;
    muiEndPress(&block->editing);
    return mui_success;
}

static uint32_t SelectionStart(const muiTextSelection* selection)
{
    return selection->anchor < selection->caret.offset ? selection->anchor
                                                       : selection->caret.offset;
}

static uint32_t SelectionEnd(const muiTextSelection* selection)
{
    return selection->anchor > selection->caret.offset ? selection->anchor
                                                       : selection->caret.offset;
}

// Replaces the bytes from start up to end with text the rules let in,
// recorded as an edit of a kind, the caret after it.
static muiResult Edit(muiTextService* service, muiTextBlock* block, uint32_t start, uint32_t end,
                      const char* text, uint32_t length, uint8_t kind, bool* changedOut)
{
    muiTextEditing* editing = &block->editing;
    if (start == end && length == 0)
    {
        return mui_success;
    }
    if (!muiFitsBlockText(block, start, end, length))
    {
        return muiRefuseText(service);
    }
    const muiTextEdit edit = {
        start, end - start, length, 0, editing->selection, Collapsed(start + length), kind};
    const char* removed = (const char*)block->text.data + start;
    if (!muiRecordEdit(&service->allocator, editing, &edit, removed, text))
    {
        return mui_errorCapacity;
    }
    muiResult result = muiReplaceBlockText(service, block, start, end, text, length);
    if (result != mui_success)
    {
        // Out of memory, the text kept: the history keeps no edit that
        // did not happen.
        muiClearHistory(editing);
        return result;
    }
    editing->selection = edit.after;
    editing->preferredX = -1.0f;
    muiEndPress(editing);
    editing->revision = block->revision;
    if (changedOut != nullptr)
    {
        *changedOut = true;
    }
    return mui_success;
}

// Takes an input method's composition out of the text, the caret where it
// began, so an edit lands where the history expects: what the method
// commits comes as typing.
static muiResult EndComposing(muiTextService* service, muiTextBlockId blockId, muiTextBlock* block)
{
    if (block->compositionLength == 0)
    {
        return mui_success;
    }
    uint32_t start = block->compositionStart;
    muiResult result = muiTextBlock_SetComposition(service, blockId, start, nullptr, 0, nullptr, 0);
    result = result == mui_success ? muiTextBlock_EndComposition(service, blockId) : result;
    if (result == mui_success)
    {
        muiPlaceSelection(block, Collapsed(start));
        muiEndPress(&block->editing);
        block->editing.revision = block->revision;
    }
    return result;
}

// Puts text over the selection, under the rules.
static muiResult Put(muiTextService* service, muiTextBlockId blockId, const char* text,
                     size_t length, uint8_t kind, bool* changedOut)
{
    if (changedOut != nullptr)
    {
        *changedOut = false;
    }
    muiTextBlock* block = nullptr;
    muiResult result = text != nullptr || length == 0 ? muiEditBlock(service, blockId, &block)
                                                      : muiRefuseText(service);
    if (result != mui_success || (block->editing.def.flags & mui_editReadOnly) != 0)
    {
        return result;
    }
    result = EndComposing(service, blockId, block);
    if (result != mui_success)
    {
        return result;
    }
    const muiTextSelection* selection = &block->editing.selection;
    uint32_t start = SelectionStart(selection);
    uint32_t end = SelectionEnd(selection);
    uint32_t kept = 0;
    if (!muiApplyEditRules(service, block, start, end, text, length, &service->editScratch, &kept))
    {
        return mui_errorCapacity;
    }
    // A selection the rules let nothing replace stays.
    if (kept == 0 && length != 0)
    {
        return mui_success;
    }
    return Edit(service, block, start, end, service->editScratch.data, kept, kind, changedOut);
}

muiResult muiTextBlock_Type(muiTextService* service, muiTextBlockId blockId, const char* text,
                            size_t length, bool* changedOut)
{
    return Put(service, blockId, text, length, MUI_EDIT_TYPING, changedOut);
}

muiResult muiTextBlock_Paste(muiTextService* service, muiTextBlockId blockId, const char* text,
                             size_t length, bool* changedOut)
{
    return Put(service, blockId, text, length, MUI_EDIT_OTHER, changedOut);
}

// Deletes the selection, or with none, the bytes from start up to end.
static muiResult Remove(muiTextService* service, muiTextBlock* block, uint32_t start, uint32_t end,
                        uint8_t kind, bool* changedOut)
{
    if ((block->editing.def.flags & mui_editReadOnly) != 0)
    {
        return mui_success;
    }
    const muiTextSelection* selection = &block->editing.selection;
    if (selection->anchor != selection->caret.offset)
    {
        start = SelectionStart(selection);
        end = SelectionEnd(selection);
        kind = MUI_EDIT_OTHER;
    }
    return Edit(service, block, start, end, nullptr, 0, kind, changedOut);
}

muiResult muiTextBlock_Erase(muiTextService* service, muiTextBlockId blockId,
                             muiTextDeletion deletion, bool* changedOut)
{
    if (changedOut != nullptr)
    {
        *changedOut = false;
    }
    muiTextBlock* block = nullptr;
    muiResult result = deletion <= mui_deleteForward ? muiEditBlock(service, blockId, &block)
                                                     : muiRefuseText(service);
    uint32_t start = 0;
    uint32_t end = 0;
    result = result == mui_success && (block->editing.def.flags & mui_editReadOnly) == 0
                 ? EndComposing(service, blockId, block)
                 : result;
    if (result == mui_success)
    {
        result = muiTextBlock_FindDeletion(service, blockId, block->editing.selection.caret.offset,
                                           deletion, &start, &end);
    }
    if (result != mui_success)
    {
        return result;
    }
    uint8_t kind = deletion == mui_deleteBackward ? MUI_EDIT_BACKWARD : MUI_EDIT_FORWARD;
    return Remove(service, block, start, end, kind, changedOut);
}

muiResult muiTextBlock_EraseTo(muiTextService* service, muiTextBlockId blockId, uint32_t offset,
                               bool* changedOut)
{
    if (changedOut != nullptr)
    {
        *changedOut = false;
    }
    muiTextBlock* block = nullptr;
    muiResult result = muiEditBlock(service, blockId, &block);
    if (result != mui_success)
    {
        return result;
    }
    offset = offset < block->length ? offset : block->length;
    if (!muiIsCharacterStart(block, offset))
    {
        return muiRefuseText(service);
    }
    if (block->compositionLength != 0)
    {
        // The offset was found in text the composition is part of.
        return mui_success;
    }
    uint32_t caret = block->editing.selection.caret.offset;
    return Remove(service, block, caret < offset ? caret : offset, caret > offset ? caret : offset,
                  MUI_EDIT_OTHER, changedOut);
}

muiResult muiTextBlock_GetSelectedText(const muiTextService* service, muiTextBlockId blockId,
                                       const char** textOut, size_t* lengthOut)
{
    muiTextBlock* block = nullptr;
    muiResult result = textOut != nullptr && lengthOut != nullptr
                           ? muiEditingBlock(service, blockId, &block)
                           : mui_errorInvalid;
    if (result != mui_success)
    {
        return result;
    }
    const muiTextSelection* selection = &block->editing.selection;
    uint32_t start = SelectionStart(selection);
    bool hidden = (block->editing.def.flags & mui_editPassword) != 0;
    *textOut = (const char*)block->text.data + start;
    *lengthOut = hidden ? 0 : SelectionEnd(selection) - start;
    return mui_success;
}

// Undoes or redoes an edit: back to what it removed, or on to what it
// inserted.
static muiResult Step(muiTextService* service, muiTextBlockId blockId, bool back, bool* changedOut)
{
    if (changedOut != nullptr)
    {
        *changedOut = false;
    }
    muiTextBlock* block = nullptr;
    muiResult result = muiEditBlock(service, blockId, &block);
    muiTextEditing* editing = result == mui_success ? &block->editing : nullptr;
    if (editing == nullptr || (editing->def.flags & mui_editReadOnly) != 0 ||
        block->compositionLength != 0 ||
        (back ? editing->done == 0 : editing->done == editing->entryCount))
    {
        return result;
    }
    const muiTextEdit* edit =
        &((const muiTextEdit*)editing->entries.data)[back ? editing->done - 1 : editing->done];
    const char* bytes = muiEditBytes(editing, edit);
    result = back ? muiReplaceBlockText(service, block, edit->start, edit->start + edit->inserted,
                                        bytes, edit->removed)
                  : muiReplaceBlockText(service, block, edit->start, edit->start + edit->removed,
                                        bytes + edit->removed, edit->inserted);
    if (result != mui_success)
    {
        return result;
    }
    if (back)
    {
        editing->done--;
    }
    else
    {
        editing->done++;
    }
    muiPlaceSelection(block, back ? edit->before : edit->after);
    editing->preferredX = -1.0f;
    muiEndPress(editing);
    editing->revision = block->revision;
    if (changedOut != nullptr)
    {
        *changedOut = true;
    }
    return mui_success;
}

muiResult muiTextBlock_Undo(muiTextService* service, muiTextBlockId blockId, bool* changedOut)
{
    return Step(service, blockId, true, changedOut);
}

muiResult muiTextBlock_Redo(muiTextService* service, muiTextBlockId blockId, bool* changedOut)
{
    return Step(service, blockId, false, changedOut);
}

muiResult muiTextBlock_GetInputPurpose(const muiTextService* service, muiTextBlockId blockId,
                                       muiInputPurpose* purposeOut)
{
    muiTextBlock* block = nullptr;
    muiResult result =
        purposeOut != nullptr ? muiEditingBlock(service, blockId, &block) : mui_errorInvalid;
    if (result == mui_success)
    {
        const muiTextEditDef* def = &block->editing.def;
        *purposeOut = (def->flags & mui_editPassword) != 0 ? mui_purposePassword
                      : def->filter != mui_filterNone      ? mui_purposeNumber
                                                           : def->purpose;
    }
    return result;
}

muiResult muiTextBlock_GetUndoState(const muiTextService* service, muiTextBlockId blockId,
                                    bool* undoOut, bool* redoOut)
{
    muiTextBlock* block = nullptr;
    muiResult result = undoOut != nullptr && redoOut != nullptr
                           ? muiEditingBlock(service, blockId, &block)
                           : mui_errorInvalid;
    if (result == mui_success)
    {
        bool open = (block->editing.def.flags & mui_editReadOnly) == 0;
        *undoOut = open && block->editing.done != 0;
        *redoOut = open && block->editing.done != block->editing.entryCount;
    }
    return result;
}

muiResult muiTextBlock_Compose(muiTextService* service, muiTextBlockId blockId, const char* text,
                               size_t length, uint32_t caret, const muiCompositionSegment* segments,
                               uint32_t segmentCount, bool* changedOut)
{
    if (changedOut != nullptr)
    {
        *changedOut = false;
    }
    muiTextBlock* block = nullptr;
    muiResult result = (text != nullptr || length == 0) && caret <= length
                           ? muiEditBlock(service, blockId, &block)
                           : muiRefuseText(service);
    // A password takes no composition, as platforms turn input methods
    // off in one.
    if (result != mui_success ||
        (block->editing.def.flags & (mui_editReadOnly | mui_editPassword)) != 0)
    {
        return result;
    }
    if (length == 0)
    {
        bool composing = block->compositionLength != 0;
        result = EndComposing(service, blockId, block);
        if (changedOut != nullptr)
        {
            *changedOut = composing && result == mui_success;
        }
        return result;
    }
    if (caret < length && (((const unsigned char*)text)[caret] & 0xC0u) == 0x80u)
    {
        return muiRefuseText(service);
    }
    // A composition starting over a selection takes its place, as typing
    // would: undone as an edit of its own.
    const muiTextSelection* selection = &block->editing.selection;
    if (block->compositionLength == 0 && selection->anchor != selection->caret.offset)
    {
        result = Edit(service, block, SelectionStart(selection), SelectionEnd(selection), nullptr,
                      0, MUI_EDIT_OTHER, nullptr);
    }
    result = result == mui_success
                 ? muiTextBlock_SetComposition(service, blockId, selection->caret.offset, text,
                                               length, segments, segmentCount)
                 : result;
    if (result != mui_success)
    {
        return result;
    }
    muiPlaceSelection(block, Collapsed(block->compositionStart + caret));
    muiEndPress(&block->editing);
    block->editing.revision = block->revision;
    if (changedOut != nullptr)
    {
        *changedOut = true;
    }
    return mui_success;
}
