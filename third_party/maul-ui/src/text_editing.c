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
#include "text_delete.h"
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
    editing->pressStart = (uint32_t)editing->selection.anchor;
    editing->pressEnd = (uint32_t)editing->selection.anchor;
}

// Read only by MUI_ASSERT, which Release builds compile out.
// Whether a selection's ends are offsets of the block's text at a
// character's start, as every edit and placement leaves them. They need
// not be cluster boundaries: a host may place them inside a cluster.
[[maybe_unused]] static bool EndsAtCharacters(const muiTextBlock* block, muiTextSelection selection)
{
    return muiIsCharacterStart(block, muiTextOffsetOf(selection.anchor)) &&
           muiIsCharacterStart(block, muiTextOffsetOf(selection.caret.offset));
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
        editing->selection.anchor = Within(block, (uint32_t)editing->selection.anchor);
        editing->selection.caret.offset = Within(block, (uint32_t)editing->selection.caret.offset);
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
        (uint32_t)rules->filter > mui_filterDecimal || (uint32_t)rules->purpose > mui_purposeUrl)
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
    if (!muiIsCharacterStart(block, muiTextOffsetOf(selection.anchor)) ||
        !muiIsCharacterStart(block, muiTextOffsetOf(selection.caret.offset)) ||
        (uint32_t)selection.caret.affinity > mui_affinityUpstream)
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
    return (uint32_t)(selection->anchor < selection->caret.offset ? selection->anchor
                                                                  : selection->caret.offset);
}

static uint32_t SelectionEnd(const muiTextSelection* selection)
{
    return (uint32_t)(selection->anchor > selection->caret.offset ? selection->anchor
                                                                  : selection->caret.offset);
}

// Takes an input method's composition out of the text, the caret where it
// began, so an edit lands where the history expects: what the method
// commits comes as typing. The bytes on its two sides can join into one
// character around that place, and the caret then goes after it.
static muiResult EndComposing(muiTextService* service, muiTextBlock* block)
{
    if (block->compositionLength == 0)
    {
        return mui_success;
    }
    uint32_t start = block->compositionStart;
    uint32_t end = start + block->compositionLength;
    muiResult result = muiComposeOver(service, block, start, end, nullptr, 0, nullptr, 0);
    if (result == mui_success)
    {
        muiPlaceSelection(block, Collapsed(Onward(block, start)));
        muiEndPress(&block->editing);
        block->editing.revision = block->revision;
    }
    return result;
}

// Copies the bytes an edit from start up to end takes out, those on either
// side of a composition shown, to the removed scratch, and returns what
// replaces the text from `from`: the text, or the bytes kept between the
// composition and start and the text after them, copied behind.
static const char* CopyAround(muiTextService* service, const muiTextBlock* block, uint32_t start,
                              uint32_t end, uint32_t from, const char* text, uint32_t length)
{
    const char* old = block->text.data;
    char* removed = service->removedScratch.data;
    uint32_t shown = block->compositionLength;
    uint32_t composition = block->compositionStart;
    uint32_t head = shown != 0 && start < composition ? composition - start : 0;
    if (head != 0)
    {
        memcpy(removed, old + start, head);
    }
    if (end - start != head)
    {
        memcpy(removed + head, old + start + head + shown, end - start - head);
    }
    uint32_t kept = start - from;
    if (kept == 0)
    {
        return text;
    }
    char* joined = removed + (end - start);
    memcpy(joined, old + from + shown, kept);
    if (length != 0)
    {
        memcpy(joined + kept, text, length);
    }
    return joined;
}

// Replaces the bytes from start up to end with text the rules let in,
// recorded as an edit of a kind, the caret after it. Where a composition
// shows, start and end are offsets in the text without it, end at or
// after its start: it goes in the same change, unrecorded, as the history
// never held it, and the bytes between it and start stay.
static muiResult Edit(muiTextService* service, muiTextBlock* block, uint32_t start, uint32_t end,
                      const char* text, uint32_t length, uint8_t kind, bool* changedOut)
{
    muiTextEditing* editing = &block->editing;
    if (start == end && length == 0)
    {
        return EndComposing(service, block);
    }
    uint32_t composition = block->compositionStart;
    uint32_t shown = block->compositionLength;
    MUI_ASSERT(shown == 0 || composition <= end);
    // The text replaced runs from the first of the composition and start.
    uint32_t from = shown != 0 && composition < start ? composition : start;
    uint32_t kept = start - from;
    muiResult fits = muiBlockTextStatus(block, from, end + shown, (size_t)kept + length);
    if (fits != mui_success)
    {
        return muiCountText(service, fits);
    }
    const muiTextSelection before =
        shown != 0 ? Collapsed(muiAfterComposition(block)) : editing->selection;
    const muiTextEdit edit = {start, end - start, length, 0, before, Collapsed(start + length),
                              kind};
    // The history's room and a copy of the bytes taken out are found
    // first, and the edit recorded once the text has changed, so running
    // out of memory leaves the text and the history as they were.
    // Bytes kept go in again before the text, after the copy.
    size_t scratch = edit.removed + (kept != 0 ? (size_t)kept + length : 0);
    if (!muiReserveEdit(&service->allocator, editing, edit.removed, length) ||
        !muiReserve(&service->allocator, &service->removedScratch, scratch))
    {
        return mui_errorCapacity;
    }
    const char* replacement = CopyAround(service, block, start, end, from, text, length);
    text = replacement + kept;
    muiResult result =
        muiReplaceBlockText(service, block, from, end + shown, replacement, (size_t)kept + length);
    if (result != mui_success)
    {
        return result;
    }
    bool recorded =
        muiRecordEdit(&service->allocator, editing, &edit, service->removedScratch.data, text);
    MUI_ASSERT(recorded);
    (void)recorded;
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

// Puts text over the selection, under the rules.
static muiResult Put(muiTextService* service, muiTextBlockId blockId, const char* text,
                     size_t length, uint8_t kind, bool* changedOut)
{
    if (changedOut != nullptr)
    {
        *changedOut = false;
    }
    if (length > MUI_MAX_TEXT_BYTES)
    {
        return muiCountText(service, mui_errorLimit);
    }
    muiTextBlock* block = nullptr;
    muiResult result = text != nullptr || length == 0 ? muiEditBlock(service, blockId, &block)
                                                      : muiRefuseText(service);
    if (result != mui_success || (block->editing.rules.flags & mui_editReadOnly) != 0)
    {
        return result;
    }
    // Text goes where a composition shown leaves the caret, the edit
    // taking it out, or over the selection; the rules read the text around
    // what the edit replaces.
    const muiTextSelection* selection = &block->editing.selection;
    uint32_t shown = block->compositionLength;
    uint32_t start = shown != 0 ? muiAfterComposition(block) : SelectionStart(selection);
    uint32_t end = shown != 0 ? start : SelectionEnd(selection);
    uint32_t from = shown != 0 ? block->compositionStart : start;
    uint32_t kept = 0;
    if (!muiApplyEditRules(service, block, from, end + shown, text, length, &service->editScratch,
                           &kept))
    {
        return mui_errorCapacity;
    }
    // A selection the rules let nothing replace stays; a composition goes.
    if (kept == 0 && length != 0)
    {
        return EndComposing(service, block);
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
    if (muiRefusesBusyText(service))
    {
        return mui_errorBusy;
    }
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
    if (muiRefusesBusyText(service))
    {
        return mui_errorBusy;
    }
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
    muiResult result = (uint32_t)deletion <= mui_deleteForward
                           ? muiEditBlock(service, blockId, &block)
                           : muiRefuseText(service);
    if (result != mui_success || (block->editing.rules.flags & mui_editReadOnly) != 0)
    {
        return result;
    }
    uint8_t kind = deletion == mui_deleteBackward ? MUI_EDIT_BACKWARD : MUI_EDIT_FORWARD;
    uint32_t start = 0;
    uint32_t end = 0;
    if (!muiFindEditDeletion(service, block, deletion, &start, &end))
    {
        return mui_errorCapacity;
    }
    // A composition shown goes in the same edit.
    return block->compositionLength == 0
               ? Remove(service, block, start, end, kind, changedOut)
               : Edit(service, block, start, end, nullptr, 0, kind, changedOut);
}

static MUI_NOINLINE muiResult TextBlockEraseWorker(muiTextService* service, muiTextBlockId blockId,
                                                   muiTextDeletion deletion, bool* changedOut)
{
    if (muiRefusesBusyText(service))
    {
        return mui_errorBusy;
    }
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
    uint32_t caret = (uint32_t)block->editing.selection.caret.offset;
    return Remove(service, block, caret < offset ? caret : offset, caret > offset ? caret : offset,
                  MUI_EDIT_OTHER, changedOut);
}

static MUI_NOINLINE muiResult TextBlockEraseToWorker(muiTextService* service,
                                                     muiTextBlockId blockId, size_t offset,
                                                     bool* changedOut)
{
    if (muiRefusesBusyText(service))
    {
        return mui_errorBusy;
    }
    if (changedOut != nullptr)
    {
        *changedOut = false;
    }
    muiResult status = muiEnterText(service);
    if (status == mui_success)
    {
        status = muiEraseTextTo(service, blockId, muiTextOffsetOf(offset), changedOut);
        muiLeaveText(service);
    }
    return status;
}

muiResult muiTextBlock_EraseTo(muiTextService* service, muiTextBlockId blockId, size_t offset,
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
    if (muiRefusesBusyText(service))
    {
        return mui_errorBusy;
    }
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
    if (muiRefusesBusyText(service))
    {
        return mui_errorBusy;
    }
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

// Puts a composition over the selection in one change, the selection's
// removal recorded as an edit of its own. The history's room and a copy
// of the removed bytes are found first, so running out of memory leaves
// the text, the selection and the history as they were.
static muiResult ComposeOverSelection(muiTextService* service, muiTextBlock* block, uint32_t start,
                                      uint32_t end, const char* text, uint32_t length,
                                      const muiCompositionSegment* segments, uint32_t segmentCount)
{
    muiTextEditing* editing = &block->editing;
    const muiTextEdit edit = {
        start, end - start, 0, 0, editing->selection, Collapsed(start), MUI_EDIT_OTHER};
    if (!muiReserveEdit(&service->allocator, editing, edit.removed, 0) ||
        !muiReserve(&service->allocator, &service->removedScratch, edit.removed))
    {
        return mui_errorCapacity;
    }
    memcpy(service->removedScratch.data, (const char*)block->text.data + start, edit.removed);
    muiResult result =
        muiComposeOver(service, block, start, end, text, length, segments, segmentCount);
    if (result == mui_success)
    {
        bool recorded = muiRecordEdit(&service->allocator, editing, &edit,
                                      service->removedScratch.data, nullptr);
        MUI_ASSERT(recorded);
        (void)recorded;
        editing->preferredX = -1.0f;
    }
    return result;
}

static muiResult TextBlockCompose(muiTextService* service, muiTextBlockId blockId, const char* text,
                                  size_t length, size_t caret,
                                  const muiCompositionSegment* segments, uint32_t segmentCount,
                                  bool* changedOut)
{
    if (length > MUI_MAX_TEXT_BYTES)
    {
        return muiCountText(service, mui_errorLimit);
    }
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
        result = EndComposing(service, block);
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
    // would: undone as an edit of its own. Everything the composition
    // needs is checked first, so a refused one leaves the selection.
    const muiTextSelection* selection = &block->editing.selection;
    uint32_t start =
        block->compositionLength != 0 ? block->compositionStart : SelectionStart(selection);
    uint32_t end =
        block->compositionLength != 0 ? start + block->compositionLength : SelectionEnd(selection);
    result = muiSegmentsStatus(segments, segmentCount, length);
    result = result == mui_success ? muiBlockTextStatus(block, start, end, length) : result;
    if (result != mui_success)
    {
        return muiCountText(service, result);
    }
    result = block->compositionLength == 0 && start != end
                 ? ComposeOverSelection(service, block, start, end, text, (uint32_t)length,
                                        segments, segmentCount)
                 : muiSetComposition(service, blockId, selection->caret.offset, text, length,
                                     segments, segmentCount);
    if (result != mui_success)
    {
        return result;
    }
    // A caret at the composition's end can be inside a character its last
    // bytes make with those after it: it goes after that character.
    muiPlaceSelection(block, Collapsed(Onward(block, block->compositionStart + (uint32_t)caret)));
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
                                                     size_t length, size_t caret,
                                                     const muiCompositionSegment* segments,
                                                     uint32_t segmentCount, bool* changedOut)
{
    if (muiRefusesBusyText(service))
    {
        return mui_errorBusy;
    }
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
                               size_t length, size_t caret, const muiCompositionSegment* segments,
                               uint32_t segmentCount, bool* changedOut)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = TextBlockComposeWorker(service, blockId, text, length, caret, segments,
                                              segmentCount, changedOut);
    muiFloatEnvLeave(floatEnv);
    return result;
}
