// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Keys, typed text and the pointer for an editing block (record
// mui-0006): a key's action by the keymap, PC's after Windows and the
// desktop toolkits of Linux, Mac's after AppKit's standard bindings.
// Shortcuts want exactly their modifier, so AltGr's Control and Alt,
// which types on many layouts, takes none. Letters are read from what
// the key means under the layout, as browsers read shortcuts.

#include "text_block.h"
#include "text_editing.h"

#include "maul-ui/layout.h"
#include "maul-ui/text_edit.h"
#include "maul-ui/text_editor.h"

enum
{
    OP_NONE,
    OP_MOVE,
    OP_ERASE,
    OP_ERASE_TO,
    OP_ENTER,
    OP_ALL,
    OP_COPY,
    OP_CUT,
    OP_PASTE,
    OP_UNDO,
    OP_REDO
};

// What a key does: an operation, the movement of a move or of an erase
// to where it goes, and a deletion's way.
typedef struct Action
{
    uint8_t op;
    muiTextMovement movement;
    muiTextDeletion deletion;
} Action;

static const muiModifiers HELD = mui_modShift | mui_modControl | mui_modAlt | mui_modMeta;

static Action Op(uint8_t op)
{
    return (Action){op, 0, 0};
}

static Action Move(muiTextMovement movement)
{
    return (Action){OP_MOVE, movement, 0};
}

static Action EraseTo(muiTextMovement movement)
{
    return (Action){OP_ERASE_TO, movement, 0};
}

static Action Erase(muiTextDeletion deletion)
{
    return (Action){OP_ERASE, 0, deletion};
}

// The shortcut of a letter with the command modifier.
static Action ShortcutOf(muiKey key, bool shift, muiKeymap keymap)
{
    switch (key >= 'A' && key <= 'Z' ? key - 'A' + 'a' : key)
    {
    case 'a':
        return Op(shift ? OP_NONE : OP_ALL);
    case 'c':
        return Op(shift ? OP_NONE : OP_COPY);
    case 'x':
        return Op(shift ? OP_NONE : OP_CUT);
    case 'v':
        return Op(shift ? OP_NONE : OP_PASTE);
    case 'z':
        return Op(shift ? OP_REDO : OP_UNDO);
    case 'y':
        return Op(keymap == mui_keymapPc && !shift ? OP_REDO : OP_NONE);
    default:
        return Op(OP_NONE);
    }
}

// The keys a line moves with: up and down by line, or on a single line,
// to its ends.
static Action Vertical(bool down, bool multiline)
{
    if (multiline)
    {
        return Move(down ? mui_moveLineDown : mui_moveLineUp);
    }
    return Move(down ? mui_moveTextEnd : mui_moveTextStart);
}

static Action PcKey(muiKeyCode code, bool control, bool multiline)
{
    switch (code)
    {
    case mui_codeArrowLeft:
        return Move(control ? mui_movePreviousWordStart : mui_moveLeft);
    case mui_codeArrowRight:
        return Move(control ? mui_moveNextWordStart : mui_moveRight);
    case mui_codeArrowUp:
    case mui_codeArrowDown:
        return Vertical(code == mui_codeArrowDown, multiline);
    case mui_codeHome:
        return Move(control ? mui_moveTextStart : mui_moveLineStart);
    case mui_codeEnd:
        return Move(control ? mui_moveTextEnd : mui_moveLineEnd);
    case mui_codeBackspace:
        return control ? EraseTo(mui_movePreviousWordStart) : Erase(mui_deleteBackward);
    case mui_codeDelete:
        return control ? EraseTo(mui_moveNextWordStart) : Erase(mui_deleteForward);
    default:
        return Op(OP_NONE);
    }
}

static Action MacKey(muiKeyCode code, bool option, bool command, bool multiline)
{
    if (option && command)
    {
        return Op(OP_NONE);
    }
    switch (code)
    {
    case mui_codeArrowLeft:
        return Move(option    ? mui_movePreviousWordStart
                    : command ? mui_moveLineStart
                              : mui_moveLeft);
    case mui_codeArrowRight:
        return Move(option ? mui_moveNextWordEnd : command ? mui_moveLineEnd : mui_moveRight);
    case mui_codeArrowUp:
    case mui_codeArrowDown:
        return command ? Move(code == mui_codeArrowDown ? mui_moveTextEnd : mui_moveTextStart)
                       : Vertical(code == mui_codeArrowDown, multiline);
    case mui_codeBackspace:
        return option    ? EraseTo(mui_movePreviousWordStart)
               : command ? EraseTo(mui_moveLineStart)
                         : Erase(mui_deleteBackward);
    case mui_codeDelete:
        return option ? EraseTo(mui_moveNextWordEnd) : Erase(mui_deleteForward);
    default:
        return Op(OP_NONE);
    }
}

// What a key does under a keymap.
static Action ActionOf(const muiEvent* event, muiKeymap keymap, bool multiline)
{
    muiModifiers held = event->modifiers & HELD;
    bool shift = (held & mui_modShift) != 0;
    muiModifiers command = keymap == mui_keymapMac ? mui_modMeta : mui_modControl;
    if ((held & ~mui_modShift) == command && (event->key & MUI_KEY_NAMED) == 0)
    {
        return ShortcutOf(event->key, shift, keymap);
    }
    if (event->code == mui_codeEnter && (held & ~mui_modShift) == 0)
    {
        return Op(multiline ? OP_ENTER : OP_NONE);
    }
    if (keymap == mui_keymapPc && (held & (mui_modAlt | mui_modMeta)) == 0)
    {
        bool control = (held & mui_modControl) != 0;
        if (event->code == mui_codeInsert)
        {
            return Op(control && !shift ? OP_COPY : shift && !control ? OP_PASTE : OP_NONE);
        }
        if (event->code == mui_codeDelete && shift && !control)
        {
            return Op(OP_CUT);
        }
        return PcKey(event->code, control, multiline);
    }
    if (keymap == mui_keymapMac && (held & mui_modControl) == 0)
    {
        return MacKey(event->code, (held & mui_modAlt) != 0, (held & mui_modMeta) != 0, multiline);
    }
    return Op(OP_NONE);
}

static muiTextBlockId BlockOf(const muiTextHost* host, muiNodeId nodeId)
{
    uint64_t key = muiNode_GetHostKey(host->context, nodeId);
    return (muiTextBlockId){(uint32_t)key, (uint32_t)(key >> 32)};
}

// Writes the selected text to the clipboard.
static muiResult Copy(const muiTextService* service, muiTextBlockId blockId,
                      const muiTextEditInput* input)
{
    const char* text = nullptr;
    size_t length = 0;
    muiResult result = muiTextBlock_GetSelectedText(service, blockId, &text, &length);
    if (result == mui_success && length != 0 && input->writeClipboard != nullptr)
    {
        input->writeClipboard(input->user, text, length);
    }
    return result;
}

// Erases from the caret to where a movement takes it.
static muiResult EraseToward(const muiTextHost* host, muiNodeId nodeId, muiTextBlockId blockId,
                             muiTextMovement movement, bool* changedOut)
{
    muiTextSelection selection;
    muiTextPosition to = {0, mui_affinityDownstream};
    float width = muiNode_GetContentRect(host->context, nodeId).width;
    muiResult result = muiTextBlock_GetSelection(host->service, blockId, &selection);
    result = result == mui_success
                 ? muiTextMove(host, nodeId, width, selection.caret, movement, 0.0f, &to)
                 : result;
    return result == mui_success
               ? muiTextBlock_EraseTo(host->service, blockId, to.offset, changedOut)
               : result;
}

// Cuts the selection: copied, then deleted; nothing on a read-only or
// password field, or with no selection.
static muiResult Cut(const muiTextHost* host, muiTextBlockId blockId, const muiTextBlock* block,
                     const muiTextEditInput* input, bool* changedOut)
{
    const muiTextSelection* selection = &block->editing.selection;
    if ((block->editing.def.flags & (mui_editReadOnly | mui_editPassword)) != 0 ||
        selection->anchor == selection->caret.offset)
    {
        return mui_success;
    }
    muiResult result = Copy(host->service, blockId, input);
    return result == mui_success
               ? muiTextBlock_Erase(host->service, blockId, mui_deleteBackward, changedOut)
               : result;
}

static muiResult Act(const muiTextHost* host, muiNodeId nodeId, muiTextBlockId blockId,
                     const muiTextBlock* block, Action action, bool extend,
                     const muiTextEditInput* input, muiTextEditOutcome* outcome)
{
    muiTextService* service = host->service;
    switch (action.op)
    {
    case OP_MOVE:
        return muiTextEditMove(host, nodeId, action.movement, extend);
    case OP_ERASE:
        return muiTextBlock_Erase(service, blockId, action.deletion, &outcome->changed);
    case OP_ERASE_TO:
        return EraseToward(host, nodeId, blockId, action.movement, &outcome->changed);
    case OP_ENTER:
        return muiTextBlock_Type(service, blockId, "\n", 1, &outcome->changed);
    case OP_ALL:
        return muiTextBlock_Select(service, blockId,
                                   (muiTextSelection){0, {block->length, mui_affinityDownstream}});
    case OP_COPY:
        return Copy(service, blockId, input);
    case OP_CUT:
        return Cut(host, blockId, block, input, &outcome->changed);
    case OP_PASTE:
        outcome->paste = (block->editing.def.flags & mui_editReadOnly) == 0;
        return mui_success;
    case OP_UNDO:
        return muiTextBlock_Undo(service, blockId, &outcome->changed);
    default:
        return muiTextBlock_Redo(service, blockId, &outcome->changed);
    }
}

// A pointer record's part: a press of the first button places the
// selection, a drag's records extend it.
static muiResult Point(const muiTextHost* host, muiNodeId nodeId, const muiEvent* event,
                       muiTextEditOutcome* outcome)
{
    const muiPointerRecord* record = event->pointer;
    bool shift = (event->modifiers & mui_modShift) != 0;
    if (record->kind == mui_pointerRecordPress && record->button == 0)
    {
        outcome->handled = true;
        return muiTextEditPress(host, nodeId, record->x, record->y, record->clickCount, shift);
    }
    if (record->kind == mui_pointerRecordDragStart || record->kind == mui_pointerRecordDragMove ||
        record->kind == mui_pointerRecordMove)
    {
        outcome->handled = true;
        return muiTextEditDrag(host, nodeId, record->x, record->y);
    }
    return mui_success;
}

muiResult muiTextEditEvent(const muiTextHost* host, muiNodeId nodeId, const muiEvent* event,
                           const muiTextEditInput* input, muiTextEditOutcome* outcomeOut)
{
    if (host == nullptr || host->service == nullptr || host->context == nullptr ||
        event == nullptr || input == nullptr || outcomeOut == nullptr ||
        input->keymap > mui_keymapMac)
    {
        return muiRefuseEdit(host != nullptr ? host->service : nullptr);
    }
    muiTextEditOutcome outcome = {false, false, false};
    muiTextBlockId blockId = BlockOf(host, nodeId);
    muiTextBlock* block = nullptr;
    muiResult result = muiEditBlock(host->service, blockId, &block);
    if (result == mui_success && event->kind == mui_eventText)
    {
        // Control characters are the keys' to act on.
        unsigned char first = event->length != 0 ? (unsigned char)event->text[0] : 0;
        outcome.handled = first >= 0x20 && first != 0x7F;
        result = outcome.handled ? muiTextBlock_Type(host->service, blockId, event->text,
                                                     event->length, &outcome.changed)
                                 : result;
    }
    else if (result == mui_success && event->kind == mui_eventKeyDown)
    {
        Action action =
            ActionOf(event, input->keymap, (block->editing.def.flags & mui_editMultiline) != 0);
        outcome.handled = action.op != OP_NONE;
        result = outcome.handled ? Act(host, nodeId, blockId, block, action,
                                       (event->modifiers & mui_modShift) != 0, input, &outcome)
                                 : result;
    }
    else if (result == mui_success && event->kind == mui_eventPointer && event->pointer != nullptr)
    {
        result = Point(host, nodeId, event, &outcome);
    }
    if (result == mui_success)
    {
        *outcomeOut = outcome;
    }
    return result;
}
