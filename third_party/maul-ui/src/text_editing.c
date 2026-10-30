// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Editing a text block: its rules, its selection, and the edits typing,
// pasting, deleting, undoing and redoing make, each recorded before the
// text changes and dropped again if it cannot.

#include "text_editing.h"

#include "fpenv.h"
#include "invariant.h"
#include "text_blocks.h"
#include "text_entry.h"
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

muiTextEditRules muiDefaultTextEditRules(void)
{
    return (muiTextEditRules){0, mui_filterNone, mui_purposeText, 0, DEFAULT_UNDO_LIMIT};
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

// The first character start at or after an offset: bytes put beside
// others can join them into one character around it.
static uint32_t Onward(const muiTextBlock* block, uint32_t at)
{
    while (!muiIsCharacterStart(block, at))
    {
        at++;
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

// Read only by MUI_ASSERT, which Release builds compile out.
// Whether a selection's ends are offsets of the block's text at a
// character's start, as every edit and placement leaves them. They need
// not be cluster boundaries: a host may place them inside a cluster.
[[maybe_unused]] static bool EndsAtCharacters(const muiTextBlock* block, muiTextSelection selection)
{
    return muiIsCharacterStart(block, selection.anchor) &&
           muiIsCharacterStart(block, selection.caret.offset);
}

void muiPlaceSelection(muiTextBlock* block, muiTextSelection selection)
{
    MUI_ASSERT(EndsAtCharacters(block, selection));
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
        return mui_errorState;
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

static muiResult TextBlockSetEditing(muiTextService* service, muiTextBlockId blockId,
                                     const muiTextEditRules* rules)
{
    if (service == nullptr || blockId.index1 == 0 || rules == nullptr ||
        (rules->flags & ~(mui_editMultiline | mui_editReadOnly | mui_editPassword)) != 0 ||
        rules->filter > mui_filterDecimal || rules->purpose > mui_purposeUrl)
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
    editing->on = true;
    editing->rules = *rules;
    editing->selection = Collapsed(block->length);
    editing->preferredX = -1.0f;
    muiEndPress(editing);
    editing->revision = block->revision;
    return mui_success;
}

muiResult muiTextBlock_SetEditing(muiTextService* service, muiTextBlockId blockId,
                                  const muiTextEditRules* rules)
{
    muiResult status = muiEnterText(service);
    if (status == mui_success)
    {
        status = TextBlockSetEditing(service, blockId, rules);
        muiLeaveText(service);
    }
    return status;
}

static muiResult TextBlockClearEditing(muiTextService* service, muiTextBlockId blockId)
{
    if (service == nullptr || blockId.index1 == 0)
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
    muiFreeBuffer(&service->allocator, &editing->entries);
    muiFreeBuffer(&service->allocator, &editing->bytes);
    *editing = (muiTextEditing){0};
    return mui_success;
}

muiResult muiTextBlock_ClearEditing(muiTextService* service, muiTextBlockId blockId)
{
    muiResult status = muiEnterText(service);
    if (status == mui_success)
    {
        status = TextBlockClearEditing(service, blockId);
        muiLeaveText(service);
    }
    return status;
}

static MUI_NOINLINE muiResult TextBlockGetSelectionWorker(const muiTextService* service,
                                                          muiTextBlockId blockId,
                                                          muiTextSelection* selectionOut)
{
    if (selectionOut != nullptr)
    {
        memset(selectionOut, 0, sizeof *selectionOut);
    }
    muiTextBlock* block = nullptr;
    muiResult result =
        selectionOut != nullptr ? muiEditingBlock(service, blockId, &block) : mui_errorInvalid;
    if (result == mui_success)
    {
        *selectionOut = block->editing.selection;
    }
    return result;
}

muiResult muiTextBlock_GetSelection(const muiTextService* service, muiTextBlockId blockId,
                                    muiTextSelection* selectionOut)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = TextBlockGetSelectionWorker(service, blockId, selectionOut);
    muiFloatEnvLeave(floatEnv);
    return result;
}

muiResult muiSelectText(muiTextService* service, muiTextBlockId blockId, muiTextSelection selection)
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

static MUI_NOINLINE muiResult TextBlockSelectWorker(muiTextService* service, muiTextBlockId blockId,
                                                    muiTextSelection selection)
{
    muiResult status = muiEnterText(service);
    if (status == mui_success)
    {
        status = muiSelectText(service, blockId, selection);
        muiLeaveText(service);
    }
    return status;
}

muiResult muiTextBlock_Select(muiTextService* service, muiTextBlockId blockId,
                              muiTextSelection selection)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = TextBlockSelectWorker(service, blockId, selection);
    muiFloatEnvLeave(floatEnv);
    return result;
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
    muiResult fits = muiBlockTextStatus(block, start, end, length);
    if (fits != mui_success)
    {
        return muiCountText(service, fits);
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
    // An ill-formed end of what went in can join the bytes after it into
    // one character, or a deletion join the bytes on either side: the
    // caret goes after that character, as typing leaves it after what was
    // typed, and the history keeps that place. The run ends there, as the
    // next edit would not start where this one ended.
    uint32_t caret = Onward(block, start + length);
    if (caret != start + length)
    {
        editing->open = 0;
        if (editing->done != 0)
        {
            ((muiTextEdit*)editing->entries.data)[editing->done - 1].after = Collapsed(caret);
        }
    }
    editing->selection = Collapsed(caret);
    MUI_ASSERT(EndsAtCharacters(block, editing->selection));
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
// commits comes as typing. The bytes on its two sides can join into one
// character around that place, and the caret then goes after it.
static muiResult EndComposing(muiTextService* service, muiTextBlockId blockId, muiTextBlock* block)
{
    if (block->compositionLength == 0)
    {
        return mui_success;
    }
    uint32_t start = block->compositionStart;
    muiResult result = muiSetComposition(service, blockId, start, nullptr, 0, nullptr, 0);
    result = result == mui_success ? muiEndComposition(service, blockId) : result;
    if (result == mui_success)
    {
        muiPlaceSelection(block, Collapsed(Onward(block, start)));
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
    if (result != mui_success || (block->editing.rules.flags & mui_editReadOnly) != 0)
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

muiResult muiTypeText(muiTextService* service, muiTextBlockId blockId, const char* text,
                      size_t length, bool* changedOut)
{
    return Put(service, blockId, text, length, MUI_EDIT_TYPING, changedOut);
}

static MUI_NOINLINE muiResult TextBlockTypeWorker(muiTextService* service, muiTextBlockId blockId,
                                                  const char* text, size_t length, bool* changedOut)
{
    if (changedOut != nullptr)
    {
        *changedOut = false;
    }
    muiResult status = muiEnterText(service);
    if (status == mui_success)
    {
        status = muiTypeText(service, blockId, text, length, changedOut);
        muiLeaveText(service);
    }
    return status;
}

muiResult muiTextBlock_Type(muiTextService* service, muiTextBlockId blockId, const char* text,
                            size_t length, bool* changedOut)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = TextBlockTypeWorker(service, blockId, text, length, changedOut);
    muiFloatEnvLeave(floatEnv);
    return result;
}

muiResult muiPasteText(muiTextService* service, muiTextBlockId blockId, const char* text,
                       size_t length, bool* changedOut)
{
    return Put(service, blockId, text, length, MUI_EDIT_OTHER, changedOut);
}

static MUI_NOINLINE muiResult TextBlockPasteWorker(muiTextService* service, muiTextBlockId blockId,
                                                   const char* text, size_t length,
                                                   bool* changedOut)
{
    if (changedOut != nullptr)
    {
        *changedOut = false;
    }
    muiResult status = muiEnterText(service);
    if (status == mui_success)
    {
        status = muiPasteText(service, blockId, text, length, changedOut);
        muiLeaveText(service);
    }
    return status;
}

muiResult muiTextBlock_Paste(muiTextService* service, muiTextBlockId blockId, const char* text,
                             size_t length, bool* changedOut)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = TextBlockPasteWorker(service, blockId, text, length, changedOut);
    muiFloatEnvLeave(floatEnv);
    return result;
}

// Deletes the selection, or with none, the bytes from start up to end.
static muiResult Remove(muiTextService* service, muiTextBlock* block, uint32_t start, uint32_t end,
                        uint8_t kind, bool* changedOut)
{
    if ((block->editing.rules.flags & mui_editReadOnly) != 0)
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

muiResult muiEraseText(muiTextService* service, muiTextBlockId blockId, muiTextDeletion deletion,
                       bool* changedOut)
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
    result = result == mui_success && (block->editing.rules.flags & mui_editReadOnly) == 0
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

static MUI_NOINLINE muiResult TextBlockEraseWorker(muiTextService* service, muiTextBlockId blockId,
                                                   muiTextDeletion deletion, bool* changedOut)
{
    if (changedOut != nullptr)
    {
        *changedOut = false;
    }
    muiResult status = muiEnterText(service);
    if (status == mui_success)
    {
        status = muiEraseText(service, blockId, deletion, changedOut);
        muiLeaveText(service);
    }
    return status;
}

muiResult muiTextBlock_Erase(muiTextService* service, muiTextBlockId blockId,
                             muiTextDeletion deletion, bool* changedOut)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = TextBlockEraseWorker(service, blockId, deletion, changedOut);
    muiFloatEnvLeave(floatEnv);
    return result;
}

muiResult muiEraseTextTo(muiTextService* service, muiTextBlockId blockId, uint32_t offset,
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

static MUI_NOINLINE muiResult TextBlockEraseToWorker(muiTextService* service,
                                                     muiTextBlockId blockId, uint32_t offset,
                                                     bool* changedOut)
{
    if (changedOut != nullptr)
    {
        *changedOut = false;
    }
    muiResult status = muiEnterText(service);
    if (status == mui_success)
    {
        status = muiEraseTextTo(service, blockId, offset, changedOut);
        muiLeaveText(service);
    }
    return status;
}

muiResult muiTextBlock_EraseTo(muiTextService* service, muiTextBlockId blockId, uint32_t offset,
                               bool* changedOut)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = TextBlockEraseToWorker(service, blockId, offset, changedOut);
    muiFloatEnvLeave(floatEnv);
    return result;
}

static MUI_NOINLINE muiResult TextBlockGetSelectedTextWorker(const muiTextService* service,
                                                             muiTextBlockId blockId,
                                                             const char** textOut,
                                                             size_t* lengthOut)
{
    if (textOut != nullptr)
    {
        *textOut = nullptr;
    }
    if (lengthOut != nullptr)
    {
        *lengthOut = 0;
    }
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
    bool hidden = (block->editing.rules.flags & mui_editPassword) != 0;
    *textOut = (const char*)block->text.data + start;
    *lengthOut = hidden ? 0 : SelectionEnd(selection) - start;
    return mui_success;
}

muiResult muiTextBlock_GetSelectedText(const muiTextService* service, muiTextBlockId blockId,
                                       const char** textOut, size_t* lengthOut)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = TextBlockGetSelectedTextWorker(service, blockId, textOut, lengthOut);
    muiFloatEnvLeave(floatEnv);
    return result;
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
    if (editing == nullptr || (editing->rules.flags & mui_editReadOnly) != 0 ||
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

muiResult muiUndoText(muiTextService* service, muiTextBlockId blockId, bool* changedOut)
{
    return Step(service, blockId, true, changedOut);
}

static MUI_NOINLINE muiResult TextBlockUndoWorker(muiTextService* service, muiTextBlockId blockId,
                                                  bool* changedOut)
{
    if (changedOut != nullptr)
    {
        *changedOut = false;
    }
    muiResult status = muiEnterText(service);
    if (status == mui_success)
    {
        status = muiUndoText(service, blockId, changedOut);
        muiLeaveText(service);
    }
    return status;
}

muiResult muiTextBlock_Undo(muiTextService* service, muiTextBlockId blockId, bool* changedOut)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = TextBlockUndoWorker(service, blockId, changedOut);
    muiFloatEnvLeave(floatEnv);
    return result;
}

muiResult muiRedoText(muiTextService* service, muiTextBlockId blockId, bool* changedOut)
{
    return Step(service, blockId, false, changedOut);
}

static MUI_NOINLINE muiResult TextBlockRedoWorker(muiTextService* service, muiTextBlockId blockId,
                                                  bool* changedOut)
{
    if (changedOut != nullptr)
    {
        *changedOut = false;
    }
    muiResult status = muiEnterText(service);
    if (status == mui_success)
    {
        status = muiRedoText(service, blockId, changedOut);
        muiLeaveText(service);
    }
    return status;
}

muiResult muiTextBlock_Redo(muiTextService* service, muiTextBlockId blockId, bool* changedOut)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = TextBlockRedoWorker(service, blockId, changedOut);
    muiFloatEnvLeave(floatEnv);
    return result;
}

static MUI_NOINLINE muiResult TextBlockGetInputPurposeWorker(const muiTextService* service,
                                                             muiTextBlockId blockId,
                                                             muiInputPurpose* purposeOut)
{
    if (purposeOut != nullptr)
    {
        *purposeOut = 0;
    }
    muiTextBlock* block = nullptr;
    muiResult result =
        purposeOut != nullptr ? muiEditingBlock(service, blockId, &block) : mui_errorInvalid;
    if (result == mui_success)
    {
        const muiTextEditRules* rules = &block->editing.rules;
        *purposeOut = (rules->flags & mui_editPassword) != 0 ? mui_purposePassword
                      : rules->filter != mui_filterNone      ? mui_purposeNumber
                                                             : rules->purpose;
    }
    return result;
}

muiResult muiTextBlock_GetInputPurpose(const muiTextService* service, muiTextBlockId blockId,
                                       muiInputPurpose* purposeOut)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = TextBlockGetInputPurposeWorker(service, blockId, purposeOut);
    muiFloatEnvLeave(floatEnv);
    return result;
}

static MUI_NOINLINE muiResult TextBlockGetUndoStateWorker(const muiTextService* service,
                                                          muiTextBlockId blockId, bool* undoOut,
                                                          bool* redoOut)
{
    if (undoOut != nullptr)
    {
        *undoOut = false;
    }
    if (redoOut != nullptr)
    {
        *redoOut = false;
    }
    muiTextBlock* block = nullptr;
    muiResult result = undoOut != nullptr && redoOut != nullptr
                           ? muiEditingBlock(service, blockId, &block)
                           : mui_errorInvalid;
    if (result == mui_success)
    {
        bool open = (block->editing.rules.flags & mui_editReadOnly) == 0;
        *undoOut = open && block->editing.done != 0;
        *redoOut = open && block->editing.done != block->editing.entryCount;
    }
    return result;
}

muiResult muiTextBlock_GetUndoState(const muiTextService* service, muiTextBlockId blockId,
                                    bool* undoOut, bool* redoOut)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = TextBlockGetUndoStateWorker(service, blockId, undoOut, redoOut);
    muiFloatEnvLeave(floatEnv);
    return result;
}

static muiResult TextBlockCompose(muiTextService* service, muiTextBlockId blockId, const char* text,
                                  size_t length, uint32_t caret,
                                  const muiCompositionSegment* segments, uint32_t segmentCount,
                                  bool* changedOut)
{
    muiTextBlock* block = nullptr;
    muiResult result = (text != nullptr || length == 0) && caret <= length
                           ? muiEditBlock(service, blockId, &block)
                           : muiRefuseText(service);
    // A password takes no composition, as platforms turn input methods
    // off in one.
    if (result != mui_success ||
        (block->editing.rules.flags & (mui_editReadOnly | mui_editPassword)) != 0)
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
    result = result == mui_success ? muiSetComposition(service, blockId, selection->caret.offset,
                                                       text, length, segments, segmentCount)
                                   : result;
    if (result != mui_success)
    {
        return result;
    }
    // A caret at the composition's end can be inside a character its last
    // bytes make with those after it: it goes after that character.
    muiPlaceSelection(block, Collapsed(Onward(block, block->compositionStart + caret)));
    muiEndPress(&block->editing);
    block->editing.revision = block->revision;
    if (changedOut != nullptr)
    {
        *changedOut = true;
    }
    return mui_success;
}

static MUI_NOINLINE muiResult TextBlockComposeWorker(muiTextService* service,
                                                     muiTextBlockId blockId, const char* text,
                                                     size_t length, uint32_t caret,
                                                     const muiCompositionSegment* segments,
                                                     uint32_t segmentCount, bool* changedOut)
{
    if (changedOut != nullptr)
    {
        *changedOut = false;
    }
    muiResult status = muiEnterText(service);
    if (status == mui_success)
    {
        status = TextBlockCompose(service, blockId, text, length, caret, segments, segmentCount,
                                  changedOut);
        muiLeaveText(service);
    }
    return status;
}

muiResult muiTextBlock_Compose(muiTextService* service, muiTextBlockId blockId, const char* text,
                               size_t length, uint32_t caret, const muiCompositionSegment* segments,
                               uint32_t segmentCount, bool* changedOut)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = TextBlockComposeWorker(service, blockId, text, length, caret, segments,
                                              segmentCount, changedOut);
    muiFloatEnvLeave(floatEnv);
    return result;
}
