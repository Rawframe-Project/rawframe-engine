// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The NSAccessibility adapter's text (record mui-0008, research 143): a
// text input's or an edited text's characters, selection, lines and
// ranges, in UTF-16 as AppKit counts, through the boundaries every
// adapter shares; the selection and the text set through the host; where
// its characters are, from the tree's clusters, or the node's frame for
// text without them.

#include "access_text.h"
#include "ns.h"

#include <string.h>

static const muiAccessNode* TextNodeOf(const MUIAccessibilityNode* object)
{
    const muiAccessNode* node = object->adapter != nullptr
                                    ? muiAccessTree_Find(object->adapter->tree, object->nodeId)
                                    : nullptr;
    return node != nullptr && muiAccessIsEdited(node) ? node : nullptr;
}

// A byte range of a text as AppKit's, in UTF-16.
static NSRange RangeOf(const muiAccessText* text, uint32_t start, uint32_t end)
{
    NSUInteger from = muiAccessUtf16Before(text->bytes, start);
    return NSMakeRange(from, muiAccessUtf16Before(text->bytes, end) - from);
}

// An AppKit range's bytes, kept within the text; false for one past it.
static bool BytesOf(const muiAccessText* text, NSRange range, uint32_t* startOut, uint32_t* endOut)
{
    NSUInteger units = muiAccessUtf16Before(text->bytes, text->length);
    if (range.location > units || range.length > units - range.location)
    {
        return false;
    }
    *startOut = muiAccessByteOfUtf16(text, (uint32_t)range.location);
    *endOut = muiAccessByteOfUtf16(text, (uint32_t)(range.location + range.length));
    return true;
}

static NSString* StringOfBytes(const muiAccessText* text, uint32_t start, uint32_t end)
{
    NSString* string = [[NSString alloc] initWithBytes:text->bytes + start
                                                length:end - start
                                              encoding:NSUTF8StringEncoding];
    return [string autorelease];
}

// The line holding a byte, by the lines' starts; 0 when none are given.
static NSInteger LineOf(const muiAccessText* text, uint32_t at)
{
    NSInteger line = 0;
    for (uint32_t i = 1; i < text->marks->lineCount && text->marks->lineStarts[i] <= at; i++)
    {
        line = (NSInteger)i;
    }
    return line;
}

static bool Ask(const MUIAccessibilityNode* object, const muiAccessRequest* request)
{
    const muiNsAdapter* adapter = object->adapter;
    return adapter != nullptr && adapter->action(adapter->user, request);
}

bool muiNsIsTextSelector(SEL selector)
{
    return selector == @selector(accessibilityNumberOfCharacters) ||
           selector == @selector(accessibilitySelectedTextRange) ||
           selector == @selector(setAccessibilitySelectedTextRange:) ||
           selector == @selector(accessibilitySelectedText) ||
           selector == @selector(setAccessibilitySelectedText:) ||
           selector == @selector(accessibilityInsertionPointLineNumber) ||
           selector == @selector(accessibilityVisibleCharacterRange) ||
           selector == @selector(accessibilityLineForIndex:) ||
           selector == @selector(accessibilityRangeForLine:) ||
           selector == @selector(accessibilityStringForRange:) ||
           selector == @selector(accessibilityRangeForIndex:) ||
           selector == @selector(accessibilityStyleRangeForIndex:) ||
           selector == @selector(accessibilityFrameForRange:) ||
           selector == @selector(accessibilityRangeForPosition:);
}

bool muiNsAllowsText(const muiAccessNode* node, SEL selector)
{
    if (selector == @selector(setAccessibilitySelectedTextRange:))
    {
        return (node->actions & (1u << mui_actionSetSelection)) != 0;
    }
    if (selector == @selector(setAccessibilitySelectedText:))
    {
        return (node->actions & (1u << mui_actionReplaceText)) != 0;
    }
    return true;
}

bool muiNsReplaceAll(const MUIAccessibilityNode* object, NSString* value)
{
    const muiAccessNode* node = TextNodeOf(object);
    const char* bytes = [value UTF8String];
    if (node == nullptr || bytes == nullptr || (node->actions & (1u << mui_actionReplaceText)) == 0)
    {
        return false;
    }
    const muiAccessRequest request = {.action = mui_actionReplaceText,
                                      .target = node->id,
                                      .anchor = 0,
                                      .focus = muiAccessValueOf(node).length,
                                      .text = bytes,
                                      .length = (uint32_t)strlen(bytes)};
    return Ask(object, &request);
}

@implementation MUIAccessibilityNode (MUIText)

- (NSInteger)accessibilityNumberOfCharacters
{
    const muiAccessNode* node = TextNodeOf(self);
    if (node == nullptr)
    {
        return 0;
    }
    muiAccessText text = muiAccessValueOf(node);
    return (NSInteger)muiAccessUtf16Before(text.bytes, text.length);
}

// The selection; at the caret when nothing is selected; none, past the
// text, when it is not being edited.
- (NSRange)accessibilitySelectedTextRange
{
    const muiAccessNode* node = TextNodeOf(self);
    if (node == nullptr || !node->marks.selected)
    {
        return NSMakeRange(NSNotFound, 0);
    }
    muiAccessText text = muiAccessValueOf(node);
    uint32_t anchor = node->marks.anchor;
    uint32_t focus = node->marks.focus;
    return RangeOf(&text, anchor < focus ? anchor : focus, anchor < focus ? focus : anchor);
}

- (void)setAccessibilitySelectedTextRange:(NSRange)range
{
    const muiAccessNode* node = TextNodeOf(self);
    uint32_t start = 0;
    uint32_t end = 0;
    if (node == nullptr)
    {
        return;
    }
    muiAccessText text = muiAccessValueOf(node);
    if (BytesOf(&text, range, &start, &end))
    {
        const muiAccessRequest request = {
            .action = mui_actionSetSelection, .target = node->id, .anchor = start, .focus = end};
        (void)Ask(self, &request);
    }
}

- (NSString*)accessibilitySelectedText
{
    const muiAccessNode* node = TextNodeOf(self);
    if (node == nullptr || !node->marks.selected)
    {
        return nil;
    }
    muiAccessText text = muiAccessValueOf(node);
    uint32_t anchor = node->marks.anchor;
    uint32_t focus = node->marks.focus;
    return StringOfBytes(&text, anchor < focus ? anchor : focus, anchor < focus ? focus : anchor);
}

- (void)setAccessibilitySelectedText:(NSString*)value
{
    const muiAccessNode* node = TextNodeOf(self);
    const char* bytes = [value UTF8String];
    if (node == nullptr || bytes == nullptr || !node->marks.selected)
    {
        return;
    }
    const muiAccessRequest request = {.action = mui_actionReplaceText,
                                      .target = node->id,
                                      .anchor = node->marks.anchor,
                                      .focus = node->marks.focus,
                                      .text = bytes,
                                      .length = (uint32_t)strlen(bytes)};
    (void)Ask(self, &request);
}

- (NSInteger)accessibilityInsertionPointLineNumber
{
    const muiAccessNode* node = TextNodeOf(self);
    if (node == nullptr || !node->marks.selected)
    {
        return 0;
    }
    muiAccessText text = muiAccessValueOf(node);
    return LineOf(&text, node->marks.focus);
}

// All of it: a part scrolled out of the field is not known without
// character geometry.
- (NSRange)accessibilityVisibleCharacterRange
{
    const muiAccessNode* node = TextNodeOf(self);
    if (node == nullptr)
    {
        return NSMakeRange(0, 0);
    }
    muiAccessText text = muiAccessValueOf(node);
    return RangeOf(&text, 0, text.length);
}

- (NSInteger)accessibilityLineForIndex:(NSInteger)index
{
    const muiAccessNode* node = TextNodeOf(self);
    if (node == nullptr || index < 0)
    {
        return 0;
    }
    muiAccessText text = muiAccessValueOf(node);
    return LineOf(&text, muiAccessByteOfUtf16(&text, (uint32_t)MIN(index, (NSInteger)UINT32_MAX)));
}

- (NSRange)accessibilityRangeForLine:(NSInteger)line
{
    const muiAccessNode* node = TextNodeOf(self);
    if (node == nullptr || line < 0)
    {
        return NSMakeRange(NSNotFound, 0);
    }
    muiAccessText text = muiAccessValueOf(node);
    const muiAccessTextMarks* marks = text.marks;
    uint32_t count = marks->lineCount != 0 ? marks->lineCount : 1;
    if ((NSUInteger)line >= count)
    {
        return NSMakeRange(NSNotFound, 0);
    }
    uint32_t start = marks->lineCount != 0 ? marks->lineStarts[line] : 0;
    uint32_t end =
        (uint32_t)line + 1 < marks->lineCount ? marks->lineStarts[line + 1] : text.length;
    return RangeOf(&text, start, end);
}

- (NSString*)accessibilityStringForRange:(NSRange)range
{
    const muiAccessNode* node = TextNodeOf(self);
    uint32_t start = 0;
    uint32_t end = 0;
    if (node == nullptr)
    {
        return nil;
    }
    muiAccessText text = muiAccessValueOf(node);
    return BytesOf(&text, range, &start, &end) ? StringOfBytes(&text, start, end) : nil;
}

// The character at an index, a surrogate pair whole.
- (NSRange)accessibilityRangeForIndex:(NSInteger)index
{
    const muiAccessNode* node = TextNodeOf(self);
    if (node == nullptr || index < 0)
    {
        return NSMakeRange(NSNotFound, 0);
    }
    muiAccessText text = muiAccessValueOf(node);
    uint32_t start = muiAccessByteOfUtf16(&text, (uint32_t)MIN(index, (NSInteger)UINT32_MAX));
    if (start == text.length)
    {
        return NSMakeRange(NSNotFound, 0);
    }
    return RangeOf(&text, start, muiAccessBoundaryAfter(&text, mui_unitCharacter, start));
}

// One style throughout: runs carry no attributes yet.
- (NSRange)accessibilityStyleRangeForIndex:(NSInteger)index
{
    (void)index;
    return [self accessibilityVisibleCharacterRange];
}

// The box around the range's lines on the screen; none for an empty
// range of text with clusters.
- (NSRect)accessibilityFrameForRange:(NSRange)range
{
    const muiAccessNode* node = TextNodeOf(self);
    uint32_t start = 0;
    uint32_t end = 0;
    muiRect box = {0};
    if (node == nullptr)
    {
        return NSZeroRect;
    }
    muiAccessText text = muiAccessValueOf(node);
    if (!BytesOf(&text, range, &start, &end) ||
        !muiAccessTextBox(adapter->tree, nodeId, start, end, &adapter->allocator, &box))
    {
        return NSZeroRect;
    }
    return muiNsScreenRectOfBox(adapter, box);
}

// The character at a point on the screen, or the nearest; the text's
// first without clusters; none for no text.
- (NSRange)accessibilityRangeForPosition:(NSPoint)point
{
    const muiAccessNode* node = TextNodeOf(self);
    if (node == nullptr)
    {
        return NSMakeRange(NSNotFound, 0);
    }
    muiAccessText text = muiAccessValueOf(node);
    float x = 0.0f;
    float y = 0.0f;
    uint32_t at = 0;
    muiNsRootPointOf(adapter, point, &x, &y);
    if (muiAccessTree_GetTextOffsetAt(adapter->tree, nodeId, x, y, &at) != mui_success)
    {
        at = 0;
    }
    if (at >= text.length)
    {
        return text.length == 0 ? NSMakeRange(NSNotFound, 0)
                                : RangeOf(&text, text.length, text.length);
    }
    return RangeOf(&text, at, muiAccessBoundaryAfter(&text, mui_unitCharacter, at));
}

@end
