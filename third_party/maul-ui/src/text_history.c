// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An editing block's undo history (record mui-0006). Typing joins the
// edit before it while it goes on where that one ended, until a word
// starts after white space: undo takes typing back a word at a time.
// Backspaces join while each ends where the last began, forward
// deletions while each begins where the last did: a run of either goes
// back whole, as AppKit's does. Edits are kept in order, their bytes in
// one buffer in the same order, so dropping the oldest or what was
// undone moves no other edit's bytes but by a shift.

#include "text_history.h"

#include "maul-unicode/encoding.h"
#include "maul-unicode/properties.h"

#include <string.h>

void muiClearHistory(muiTextEditing* editing)
{
    editing->entryCount = 0;
    editing->done = 0;
    editing->byteCount = 0;
    editing->open = 0;
}

const char* muiEditBytes(const muiTextEditing* editing, const muiTextEdit* edit)
{
    return (const char*)editing->bytes.data + edit->bytes;
}

// The last character of text, or 0 for none.
static uint32_t LastPoint(const char* text, uint32_t length)
{
    uint32_t start = length;
    while (start > 0 && length - start < 4 && ((unsigned char)text[start - 1] & 0xC0u) == 0x80u)
    {
        start--;
    }
    start -= start > 0 ? 1u : 0u;
    uint32_t point = 0;
    size_t size = 1;
    if (length != 0)
    {
        (void)muniDecodeUtf8(text + start, length - start, &point, &size);
    }
    return point;
}

// Whether text going in after other text starts a word after white space.
static bool StartsWord(const char* before, uint32_t beforeLength, const char* text, uint32_t length)
{
    uint32_t first = 0;
    size_t size = 1;
    (void)muniDecodeUtf8(text, length, &first, &size);
    return muniIsWhiteSpace(LastPoint(before, beforeLength)) && !muniIsWhiteSpace(first);
}

// Whether an edit of the kind the last left open continues it. Placing
// the selection anywhere closes a run (muiPlaceSelection), so an edit of
// that kind starts where the last left the caret: typing joins unless it
// starts a word, deletions always.
static bool Continues(const muiTextEditing* editing, const muiTextEdit* last,
                      const muiTextEdit* edit, const char* inserted)
{
    return edit->kind != MUI_EDIT_TYPING || !StartsWord(muiEditBytes(editing, last) + last->removed,
                                                        last->inserted, inserted, edit->inserted);
}

// Joins an edit to the last, whose bytes end the buffer.
static void Join(muiTextEditing* editing, muiTextEdit* last, const muiTextEdit* edit,
                 const char* removed, const char* inserted)
{
    char* bytes = editing->bytes.data;
    if (edit->kind == MUI_EDIT_BACKWARD)
    {
        // What a backspace removed came before what the run had.
        char* at = bytes + last->bytes;
        memmove(at + edit->removed, at, last->removed);
        memcpy(at, removed, edit->removed);
        last->start = edit->start;
    }
    else
    {
        memcpy(bytes + editing->byteCount, edit->kind == MUI_EDIT_TYPING ? inserted : removed,
               edit->removed + edit->inserted);
    }
    last->removed += edit->removed;
    last->inserted += edit->inserted;
    last->after = edit->after;
    editing->byteCount += edit->removed + edit->inserted;
}

static void DropOldest(muiTextEditing* editing)
{
    muiTextEdit* entries = editing->entries.data;
    uint32_t shift = entries[0].removed + entries[0].inserted;
    char* bytes = editing->bytes.data;
    memmove(bytes, bytes + shift, editing->byteCount - shift);
    memmove(entries, entries + 1, (editing->entryCount - 1) * sizeof *entries);
    editing->entryCount--;
    editing->done--;
    editing->byteCount -= shift;
    for (uint32_t i = 0; i < editing->entryCount; i++)
    {
        entries[i].bytes -= shift;
    }
}

bool muiRecordEdit(const muiAllocator* allocator, muiTextEditing* editing, const muiTextEdit* edit,
                   const char* removed, const char* inserted)
{
    // What was undone goes.
    muiTextEdit* entries = editing->entries.data;
    uint32_t done = editing->done;
    uint32_t kept =
        done != 0 ? entries[done - 1].bytes + entries[done - 1].removed + entries[done - 1].inserted
                  : 0;
    if (editing->def.undoLimit == 0)
    {
        muiClearHistory(editing);
        return true;
    }
    if (!muiReserveKeeping(allocator, &editing->entries, (done + 1u) * sizeof(muiTextEdit),
                           done * sizeof(muiTextEdit)) ||
        !muiReserveKeeping(allocator, &editing->bytes,
                           (size_t)kept + edit->removed + edit->inserted, kept))
    {
        return false;
    }
    entries = editing->entries.data;
    if (entries == nullptr || editing->bytes.data == nullptr)
    {
        // Reserved above; said again for the analyzer.
        return false;
    }
    editing->entryCount = done;
    editing->byteCount = kept;
    muiTextEdit* last = done != 0 ? &entries[done - 1] : nullptr;
    bool joins =
        last != nullptr && editing->open == edit->kind && Continues(editing, last, edit, inserted);
    // Typing and deleting stay open to the next of their kind.
    editing->open = edit->kind != MUI_EDIT_OTHER ? edit->kind : 0;
    if (joins)
    {
        Join(editing, last, edit, removed, inserted);
        return true;
    }
    if (editing->entryCount == editing->def.undoLimit)
    {
        DropOldest(editing);
    }
    muiTextEdit* added = &entries[editing->entryCount++];
    *added = *edit;
    added->bytes = editing->byteCount;
    char* bytes = editing->bytes.data;
    if (edit->removed != 0)
    {
        memcpy(bytes + editing->byteCount, removed, edit->removed);
    }
    if (edit->inserted != 0)
    {
        memcpy(bytes + editing->byteCount + edit->removed, inserted, edit->inserted);
    }
    editing->byteCount += edit->removed + edit->inserted;
    editing->done = editing->entryCount;
    return true;
}
