// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The AT-SPI adapter's Text interface (record mui-0008, I123): a node's
// value text by character, word, line and paragraph, its caret and its
// selection. AT-SPI counts characters (code points); the tree's marks
// count bytes. Sentences are not given: an empty answer, which Orca
// reads as no sentence support.

#include "access_text.h"
#include "allocator.h"
#include "atspi.h"

#include <string.h>

// AT-SPI's granularities, and the older boundary types.
enum
{
    GRAIN_CHAR = 0,
    GRAIN_WORD = 1,
    GRAIN_SENTENCE = 2,
    GRAIN_LINE = 3,
    GRAIN_PARAGRAPH = 4,
    BOUNDARY_CHAR = 0,
    BOUNDARY_WORD_START = 1,
    BOUNDARY_LINE_START = 5,
    // Not a granularity: none of the text.
    GRAIN_NONE = 99
};

#define ERROR_INVALID_ARGS "org.freedesktop.DBus.Error.InvalidArgs"

// Bytes of a text from start up to end.
typedef struct Span
{
    uint32_t start;
    uint32_t end;
} Span;

bool muiAtspiHasText(const muiAccessNode* node)
{
    return muiAccessIsTextInput(node) ||
           (node->text[mui_accessValue] != nullptr && (node->flags & mui_accessNumeric) == 0);
}

static bool IsLead(char byte)
{
    return ((unsigned char)byte & 0xC0u) != 0x80u;
}

// The byte where a text's character of an index starts, clamped to the
// text: 0 below 0.
static uint32_t ByteOf(const muiAccessText* text, int32_t index)
{
    return index > 0 ? muiAccessByteOfPoints(text, (uint32_t)index) : 0;
}

static int32_t PointsBefore(const muiAccessText* text, uint32_t offset)
{
    return (int32_t)muiAccessPointsBefore(text->bytes, offset);
}

// The unit at a byte: from its last boundary at or before the byte to the
// next after it.
static Span UnitAt(const muiAccessText* text, muiAccessUnit unit, uint32_t at)
{
    uint32_t next = muiAccessBoundaryAfter(text, mui_unitCharacter, at);
    return (Span){muiAccessBoundaryBefore(text, unit, next > at ? next : at),
                  muiAccessBoundaryAfter(text, unit, at)};
}

// The word at a byte, or the one before: from its start to the next
// word's, or the text's end; none before the first.
static Span WordAt(const muiAccessText* text, uint32_t at)
{
    const muiAccessTextMarks* marks = text->marks;
    uint32_t found = UINT32_MAX;
    for (uint32_t i = 0; i < marks->wordCount && marks->words[i].start <= at; i++)
    {
        found = i;
    }
    if (found == UINT32_MAX)
    {
        return (Span){at, at};
    }
    uint32_t end = found + 1 < marks->wordCount ? marks->words[found + 1].start : text->length;
    return (Span){marks->words[found].start, end};
}

static Span SpanAt(const muiAccessText* text, uint32_t at, uint32_t grain)
{
    switch (grain)
    {
    case GRAIN_CHAR:
        return (Span){at, muiAccessBoundaryAfter(text, mui_unitCharacter, at)};
    case GRAIN_WORD:
        return WordAt(text, at);
    case GRAIN_LINE:
        return UnitAt(text, mui_unitLine, at);
    case GRAIN_PARAGRAPH:
        return UnitAt(text, mui_unitParagraph, at);
    default:
        return (Span){0, 0};
    }
}

// Writes a span's text and its offsets in characters, (sii).
static bool AppendSpan(muiAtspiApp* app, muiDBusIter* iter, const muiAccessText* text, Span span)
{
    uint32_t size = span.end - span.start;
    char* copy = muiAllocate(&app->allocator, (size_t)size + 1, 1);
    if (copy == nullptr)
    {
        return false;
    }
    memcpy(copy, text->bytes + span.start, size);
    copy[size] = '\0';
    const int32_t start = PointsBefore(text, span.start);
    const int32_t end = PointsBefore(text, span.end);
    bool ok = muiAtspiAppendString(app, iter, copy) &&
              app->dbus.appendBasic(iter, mui_dbusTypeInt32, &start) &&
              app->dbus.appendBasic(iter, mui_dbusTypeInt32, &end);
    muiRelease(&app->allocator, copy, (size_t)size + 1, 1);
    return ok;
}

// Reads a call's arguments of the types named, 'i', 'u' or 's': the
// integers into values and the strings into strings, each in order;
// false for others.
static bool ReadArgs(const muiDBusApi* dbus, DBusMessage* call, const char* types, int32_t* values,
                     const char** strings)
{
    muiDBusIter iter;
    bool ok = dbus->iterInit(call, &iter);
    for (size_t i = 0; ok && types[i] != '\0'; i++)
    {
        int type = types[i] == 'i'   ? mui_dbusTypeInt32
                   : types[i] == 'u' ? mui_dbusTypeUint32
                                     : mui_dbusTypeString;
        ok = (i == 0 || dbus->next(&iter)) && dbus->argType(&iter) == type;
        if (ok)
        {
            dbus->getBasic(&iter, type == mui_dbusTypeString ? (void*)strings++ : (void*)values++);
        }
    }
    return ok;
}

static bool ReadInts(const muiDBusApi* dbus, DBusMessage* call, const char* types, int32_t* values)
{
    return ReadArgs(dbus, call, types, values, nullptr);
}

// The granularity an older boundary type stands for; none for the
// sentence and end types.
static uint32_t GrainOfBoundary(uint32_t boundary)
{
    return boundary == BOUNDARY_CHAR         ? GRAIN_CHAR
           : boundary == BOUNDARY_WORD_START ? GRAIN_WORD
           : boundary == BOUNDARY_LINE_START ? GRAIN_LINE
                                             : GRAIN_NONE;
}

// GetStringAtOffset and the older Get{Before,At,After}Offset: the span at
// the offset, or the one before or after it.
static bool AppendSpanAt(muiAtspiApp* app, muiDBusIter* iter, DBusMessage* call,
                         const muiAccessText* text, const char* member, bool* ok)
{
    int32_t args[2] = {0, 0};
    *ok = ReadInts(&app->dbus, call, "iu", args);
    bool string = strcmp(member, "GetStringAtOffset") == 0;
    uint32_t grain = string ? (uint32_t)args[1] : GrainOfBoundary((uint32_t)args[1]);
    uint32_t at = ByteOf(text, args[0]);
    Span span = SpanAt(text, at, grain);
    if (strcmp(member, "GetTextBeforeOffset") == 0)
    {
        span =
            span.start != 0 && grain != GRAIN_NONE
                ? SpanAt(text, muiAccessBoundaryBefore(text, mui_unitCharacter, span.start), grain)
                : (Span){0, 0};
    }
    else if (strcmp(member, "GetTextAfterOffset") == 0)
    {
        span = span.end < text->length && grain != GRAIN_NONE ? SpanAt(text, span.end, grain)
                                                              : (Span){text->length, text->length};
    }
    *ok = *ok && AppendSpan(app, iter, text, span);
    return true;
}

// The code point at a byte, 0 past the text. The record's text is
// well-formed UTF-8.
static int32_t PointAt(const muiAccessText* text, uint32_t at)
{
    if (at >= text->length)
    {
        return 0;
    }
    const unsigned char* bytes = (const unsigned char*)text->bytes + at;
    uint32_t size = muiAccessBoundaryAfter(text, mui_unitCharacter, at) - at;
    uint32_t point = size == 1 ? bytes[0] : bytes[0] & (0x7Fu >> size);
    for (uint32_t i = 1; i < size; i++)
    {
        point = point << 6 | (bytes[i] & 0x3Fu);
    }
    return (int32_t)point;
}

// The selection's bytes, the lower first; empty for a caret alone or
// none.
static Span SelectionOf(const muiAccessText* text)
{
    const muiAccessTextMarks* marks = text->marks;
    if (!marks->selected || marks->anchor == marks->focus)
    {
        return (Span){0, 0};
    }
    return marks->anchor < marks->focus ? (Span){marks->anchor, marks->focus}
                                        : (Span){marks->focus, marks->anchor};
}

// Writes the answers that read the text.
static bool AppendRead(muiAtspiApp* app, muiDBusIter* iter, DBusMessage* call,
                       const muiAccessText* text, const char* member, bool* ok)
{
    const muiDBusApi* dbus = &app->dbus;
    int32_t args[2] = {0, 0};
    if (strcmp(member, "GetText") == 0)
    {
        *ok = ReadInts(dbus, call, "ii", args);
        uint32_t start = ByteOf(text, args[0]);
        uint32_t end = args[1] < 0 ? text->length : ByteOf(text, args[1]);
        Span span = {start, end > start ? end : start};
        uint32_t size = span.end - span.start;
        char* copy = muiAllocate(&app->allocator, (size_t)size + 1, 1);
        *ok = *ok && copy != nullptr;
        if (copy != nullptr)
        {
            memcpy(copy, text->bytes + span.start, size);
            copy[size] = '\0';
            *ok = *ok && muiAtspiAppendString(app, iter, copy);
            muiRelease(&app->allocator, copy, (size_t)size + 1, 1);
        }
    }
    else if (strcmp(member, "GetCharacterAtOffset") == 0)
    {
        *ok = ReadInts(dbus, call, "i", args);
        const int32_t point = args[0] < 0 ? 0 : PointAt(text, ByteOf(text, args[0]));
        *ok = *ok && dbus->appendBasic(iter, mui_dbusTypeInt32, &point);
    }
    else if (strcmp(member, "GetStringAtOffset") == 0 || strcmp(member, "GetTextAtOffset") == 0 ||
             strcmp(member, "GetTextBeforeOffset") == 0 ||
             strcmp(member, "GetTextAfterOffset") == 0)
    {
        return AppendSpanAt(app, iter, call, text, member, ok);
    }
    else
    {
        return false;
    }
    return true;
}

// Asks the host for a request on a node that takes its action; whether
// it was done.
static bool Ask(const muiAtspiObject* object, const muiAccessRequest* request)
{
    return (object->node->actions & (1u << request->action)) != 0 &&
           object->adapter->action(object->adapter->user, request);
}

// Asks the host to select from one character to another, the caret at
// the second.
static bool Select(const muiAtspiObject* object, const muiAccessText* text, int32_t anchor,
                   int32_t focus)
{
    const muiAccessRequest request = {.action = mui_actionSetSelection,
                                      .target = object->node->id,
                                      .anchor = ByteOf(text, anchor),
                                      .focus = ByteOf(text, focus)};
    return Ask(object, &request);
}

// The answers that set the selection: the caret placed, the one
// selection set, added where there is none, or taken back to the caret.
static muiDBusBool SetSelection(DBusMessage* call, const muiAtspiObject* object,
                                const muiAccessText* text, const char* member,
                                const muiDBusApi* dbus, bool* ok)
{
    int32_t args[3] = {0, 0, 0};
    Span selection = SelectionOf(text);
    bool none = selection.start == selection.end;
    if (strcmp(member, "SetCaretOffset") == 0)
    {
        *ok = ReadInts(dbus, call, "i", args);
        return *ok && Select(object, text, args[0], args[0]);
    }
    if (strcmp(member, "SetSelection") == 0)
    {
        *ok = ReadInts(dbus, call, "iii", args);
        return *ok && args[0] == 0 && Select(object, text, args[1], args[2]);
    }
    if (strcmp(member, "AddSelection") == 0)
    {
        *ok = ReadInts(dbus, call, "ii", args);
        return *ok && none && Select(object, text, args[0], args[1]);
    }
    *ok = ReadInts(dbus, call, "i", args);
    int32_t caret = PointsBefore(text, text->marks->focus);
    return *ok && args[0] == 0 && !none && Select(object, text, caret, caret);
}

// Writes the answers about the selection, and about scrolling to text,
// which is not offered.
static bool AppendSelection(muiAtspiApp* app, muiDBusIter* iter, DBusMessage* call,
                            const muiAtspiObject* object, const muiAccessText* text,
                            const char* member, bool* ok)
{
    const muiDBusApi* dbus = &app->dbus;
    Span selection = SelectionOf(text);
    int32_t args[1] = {0};
    if (strcmp(member, "GetNSelections") == 0)
    {
        const int32_t count = selection.end != selection.start ? 1 : 0;
        *ok = dbus->appendBasic(iter, mui_dbusTypeInt32, &count);
    }
    else if (strcmp(member, "GetSelection") == 0)
    {
        *ok = ReadInts(dbus, call, "i", args);
        Span chosen = args[0] == 0 ? selection : (Span){0, 0};
        const int32_t start = PointsBefore(text, chosen.start);
        const int32_t end = PointsBefore(text, chosen.end);
        *ok = *ok && dbus->appendBasic(iter, mui_dbusTypeInt32, &start) &&
              dbus->appendBasic(iter, mui_dbusTypeInt32, &end);
    }
    else if (strcmp(member, "SetCaretOffset") == 0 || strcmp(member, "SetSelection") == 0 ||
             strcmp(member, "AddSelection") == 0 || strcmp(member, "RemoveSelection") == 0)
    {
        const muiDBusBool done = SetSelection(call, object, text, member, dbus, ok);
        *ok = *ok && dbus->appendBasic(iter, mui_dbusTypeBoolean, &done);
    }
    else if (strcmp(member, "ScrollSubstringTo") == 0 ||
             strcmp(member, "ScrollSubstringToPoint") == 0)
    {
        const muiDBusBool done = 0;
        *ok = dbus->appendBasic(iter, mui_dbusTypeBoolean, &done);
    }
    else
    {
        return false;
    }
    return true;
}

// Writes an empty a{ss}.
static bool AppendNoAttributes(muiAtspiApp* app, muiDBusIter* iter)
{
    muiDBusIter array;
    return app->dbus.openContainer(iter, mui_dbusTypeArray, "{ss}", &array) &&
           app->dbus.closeContainer(iter, &array);
}

// Writes the extents of characters from one to another in a coordinate
// type, all zero for none.
static bool AppendRangeExtents(muiAtspiApp* app, muiDBusIter* iter, const muiAtspiObject* object,
                               const muiAccessText* text, int32_t start, int32_t end,
                               uint32_t coordinates)
{
    int32_t extents[4] = {0, 0, 0, 0};
    muiRect box = {0};
    uint32_t from = ByteOf(text, start < end ? start : end);
    uint32_t to = ByteOf(text, start < end ? end : start);
    if (muiAccessTextBox(object->adapter->tree, object->node->id, from, to,
                         &object->adapter->app->allocator, &box))
    {
        muiAtspiRectIn(object, box, coordinates, extents);
    }
    bool ok = true;
    for (int i = 0; i < 4 && ok; i++)
    {
        ok = app->dbus.appendBasic(iter, mui_dbusTypeInt32, &extents[i]);
    }
    return ok;
}

// Writes the answers about geometry: a character's extents, a range's,
// the character at a point (-1 outside the node; the text's start when
// it has no clusters).
static bool AppendGeometry(muiAtspiApp* app, muiDBusIter* iter, DBusMessage* call,
                           const muiAtspiObject* object, const muiAccessText* text,
                           const char* member, bool* ok)
{
    int32_t args[3] = {0, 0, 0};
    if (strcmp(member, "GetCharacterExtents") == 0)
    {
        // The character to the next; the last offset a client can name
        // is past any text's end.
        *ok = ReadInts(&app->dbus, call, "iu", args) &&
              AppendRangeExtents(app, iter, object, text, args[0],
                                 args[0] < INT32_MAX ? args[0] + 1 : args[0], (uint32_t)args[1]);
    }
    else if (strcmp(member, "GetRangeExtents") == 0)
    {
        *ok = ReadInts(&app->dbus, call, "iiu", args) &&
              AppendRangeExtents(app, iter, object, text, args[0], args[1], (uint32_t)args[2]);
    }
    else if (strcmp(member, "GetOffsetAtPoint") == 0)
    {
        *ok = ReadInts(&app->dbus, call, "iiu", args);
        float x = 0.0f;
        float y = 0.0f;
        uint32_t at = 0;
        int32_t offset = -1;
        if (*ok && muiAtspiPointIn(object, (uint32_t)args[2], args[0], args[1], &x, &y))
        {
            muiResult status =
                muiAccessTree_GetTextOffsetAt(object->adapter->tree, object->node->id, x, y, &at);
            offset = status == mui_success ? PointsBefore(text, at) : 0;
        }
        *ok = *ok && app->dbus.appendBasic(iter, mui_dbusTypeInt32, &offset);
    }
    else
    {
        return false;
    }
    return true;
}

// Writes the answers about attributes: none yet, the whole text one run
// of no attributes.
static bool AppendOther(muiAtspiApp* app, muiDBusIter* iter, const muiAccessText* text,
                        const char* member, bool* ok)
{
    const muiDBusApi* dbus = &app->dbus;
    const int32_t zero = 0;
    const int32_t count = PointsBefore(text, text->length);
    if (strcmp(member, "GetAttributes") == 0 || strcmp(member, "GetAttributeRun") == 0)
    {
        *ok = AppendNoAttributes(app, iter) && dbus->appendBasic(iter, mui_dbusTypeInt32, &zero) &&
              dbus->appendBasic(iter, mui_dbusTypeInt32, &count);
    }
    else if (strcmp(member, "GetDefaultAttributes") == 0 ||
             strcmp(member, "GetDefaultAttributeSet") == 0)
    {
        *ok = AppendNoAttributes(app, iter);
    }
    else if (strcmp(member, "GetAttributeValue") == 0)
    {
        *ok = muiAtspiAppendString(app, iter, "");
    }
    else if (strcmp(member, "GetBoundedRanges") == 0)
    {
        muiDBusIter array;
        *ok = dbus->openContainer(iter, mui_dbusTypeArray, "(iisv)", &array) &&
              dbus->closeContainer(iter, &array);
    }
    else
    {
        return false;
    }
    return true;
}

bool muiAtspiAnswerText(muiAtspiApp* app, DBusMessage* call, const muiAtspiObject* object,
                        const char* member)
{
    DBusMessage* reply = app->dbus.newMethodReturn(call);
    if (reply == nullptr)
    {
        muiAtspiSend(app, call, nullptr);
        return true;
    }
    const muiAccessText text = muiAccessValueOf(object->node);
    muiDBusIter iter;
    app->dbus.iterInitAppend(reply, &iter);
    bool ok = true;
    if (!AppendRead(app, &iter, call, &text, member, &ok) &&
        !AppendSelection(app, &iter, call, object, &text, member, &ok) &&
        !AppendGeometry(app, &iter, call, object, &text, member, &ok) &&
        !AppendOther(app, &iter, &text, member, &ok))
    {
        app->dbus.unrefMessage(reply);
        return false;
    }
    if (!ok)
    {
        app->dbus.unrefMessage(reply);
        reply = app->dbus.newError(call, ERROR_INVALID_ARGS, member);
    }
    muiAtspiSend(app, call, reply);
    return true;
}

bool muiAtspiAppendTextProperty(muiAtspiApp* app, muiDBusIter* iter, const muiAtspiObject* object,
                                const char* name, bool* ok)
{
    const muiAccessText text = muiAccessValueOf(object->node);
    int32_t value = 0;
    if (strcmp(name, "CharacterCount") == 0)
    {
        value = PointsBefore(&text, text.length);
    }
    else if (strcmp(name, "CaretOffset") == 0)
    {
        value = text.marks->selected ? PointsBefore(&text, text.marks->focus) : -1;
    }
    else
    {
        return false;
    }
    *ok = muiAtspiAppendVariant(app, iter, mui_dbusTypeInt32, &value);
    return true;
}

// Asks the host to replace text from one character to another; whether
// it was done.
static bool Replace(const muiAtspiObject* object, const muiAccessText* text, int32_t start,
                    int32_t end, const char* with, size_t length)
{
    const muiAccessRequest request = {.action = mui_actionReplaceText,
                                      .target = object->node->id,
                                      .anchor = ByteOf(text, start),
                                      .focus = ByteOf(text, end),
                                      .text = with,
                                      .length = (uint32_t)length};
    return length <= INT32_MAX && Ask(object, &request);
}

// InsertText's text: as many bytes as its length says, or all for a
// negative one, cut back to a character's start.
static size_t InsertedLength(const char* with, int32_t length)
{
    size_t all = strlen(with);
    size_t kept = length >= 0 && (size_t)length < all ? (size_t)length : all;
    while (kept > 0 && kept < all && !IsLead(with[kept]))
    {
        kept--;
    }
    return kept;
}

bool muiAtspiAnswerEditableText(muiAtspiApp* app, DBusMessage* call, const muiAtspiObject* object,
                                const char* member)
{
    const muiDBusApi* dbus = &app->dbus;
    const muiAccessText text = muiAccessValueOf(object->node);
    int32_t args[2] = {0, 0};
    const char* with = nullptr;
    bool ok = true;
    muiDBusBool done = 0;
    if (strcmp(member, "SetTextContents") == 0)
    {
        ok = ReadArgs(dbus, call, "s", args, &with);
        done = ok && Replace(object, &text, 0, INT32_MAX, with, strlen(with));
    }
    else if (strcmp(member, "InsertText") == 0)
    {
        ok = ReadArgs(dbus, call, "isi", args, &with);
        done = ok && Replace(object, &text, args[0], args[0], with, InsertedLength(with, args[1]));
    }
    else if (strcmp(member, "DeleteText") == 0)
    {
        ok = ReadInts(dbus, call, "ii", args);
        done = ok && Replace(object, &text, args[0], args[1] < 0 ? INT32_MAX : args[1], "", 0);
    }
    else if (strcmp(member, "CopyText") != 0 && strcmp(member, "CutText") != 0 &&
             strcmp(member, "PasteText") != 0)
    {
        return false;
    }
    // The clipboard is the host's: copying, cutting and pasting are not
    // offered, CopyText answering nothing.
    DBusMessage* reply =
        ok ? dbus->newMethodReturn(call) : dbus->newError(call, ERROR_INVALID_ARGS, member);
    muiDBusIter iter;
    if (reply != nullptr && ok && strcmp(member, "CopyText") != 0)
    {
        dbus->iterInitAppend(reply, &iter);
        if (!dbus->appendBasic(&iter, mui_dbusTypeBoolean, &done))
        {
            dbus->unrefMessage(reply);
            reply = nullptr;
        }
    }
    muiAtspiSend(app, call, reply);
    return true;
}
