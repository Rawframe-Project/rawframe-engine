// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text and input methods on macOS (macos.h). The view's input context
// types every key's text. While the window does not accept text, the
// context takes only Roman keyboard layouts, as a password field's does,
// so no input method composes, and a dead key's accent waiting is not
// shown. While it does, every input source may compose into it: the
// method's marked text comes as mwin_eventImePreedit,
// its clauses from the marked clause attribute and the thick underline
// that marks the one being converted, and the result as text. The
// candidate window goes by the caret the program gave.

#include "allocator.h"
#include "apple_text.h"
#include "macos.h"
#include "utf8.h"

static void Post(mwinMacPlatform* platform, uint32_t slot, mwinEvent* event)
{
    event->timeNs = mwinMacNow();
    mwinPost(platform->context, slot, event);
}

static NSString* PlainOf(id string)
{
    return [string isKindOfClass:[NSAttributedString class]] ? ((NSAttributedString*)string).string
                                                             : (NSString*)string;
}

// Ends the input method's composition, when one runs.
static void EndComposition(mwinMacPlatform* platform, uint32_t slot)
{
    mwinMacWindow* window = &platform->windows[slot];
    [window->marked release];
    window->marked = nil;
    if (!platform->context->windows[slot].imeComposing)
    {
        return;
    }
    mwinEvent end = {.kind = mwin_eventImePreedit};
    end.data.preedit.caret = -1;
    Post(platform, slot, &end);
}

// Typed text without control characters or AppKit's function key
// characters, which are keys; a lone surrogate as U+FFFD, and a reset in
// its place when there is no memory for it.
static void PostText(mwinMacPlatform* platform, uint32_t slot, NSString* string)
{
    NSMutableString* kept = [NSMutableString stringWithCapacity:string.length];
    for (NSUInteger i = 0; i < string.length; i++)
    {
        unichar unit = [string characterAtIndex:i];
        if (unit >= 0x20 && unit != 0x7F && (unit < 0xF700 || unit > 0xF8FF))
        {
            CFStringAppendCharacters((CFMutableStringRef)kept, &unit, 1);
        }
    }
    mwinContext* context = platform->context;
    size_t length = 0;
    size_t size = 0;
    char* bytes = mwinAppleUtf8Of(context, kept, &length, &size);
    if (bytes != nullptr && length <= UINT32_MAX)
    {
        mwinEvent event = {.kind = mwin_eventTextInput};
        event.data.text = (mwinTextEvent){bytes, (uint32_t)length};
        Post(platform, slot, &event);
    }
    else if (kept.length > 0)
    {
        mwinEvent reset = {.kind = mwin_eventInputStateReset};
        Post(platform, slot, &reset);
    }
    if (bytes != nullptr)
    {
        mwinRelease(&context->allocator, bytes, size, 1);
    }
}

// The style of a clause: the thick underline marks the one a conversion
// works on.
static mwinPreeditStyle StyleOf(id string, NSRange range)
{
    if (![string isKindOfClass:[NSAttributedString class]] || range.length == 0)
    {
        return mwin_preeditUnderline;
    }
    NSNumber* underline = [(NSAttributedString*)string attribute:NSUnderlineStyleAttributeName
                                                         atIndex:range.location
                                                  effectiveRange:nullptr];
    return underline.integerValue >= NSUnderlineStyleThick ? mwin_preeditTarget
                                                           : mwin_preeditUnderline;
}

// The clauses of marked text in bytes of *preedit's text, neighbours of
// one style joined; past the last segment the rest joins it. Returns the
// count, with the target clauses' span in *preedit's selection when
// there is one.
static uint32_t Segment(id string, NSString* text, mwinPreeditSegment* segments,
                        mwinPreeditEvent* preedit)
{
    __block uint32_t count = 0;
    __block bool targeted = false;
    const char* bytes = preedit->text;
    size_t length = preedit->length;
    void (^add)(NSRange) = ^(NSRange range) {
      mwinPreeditStyle style = StyleOf(string, range);
      uint32_t start = (uint32_t)mwinBytesBefore(bytes, length, muni_unitUtf16, range.location);
      uint32_t end = (uint32_t)mwinBytesBefore(bytes, length, muni_unitUtf16, NSMaxRange(range));
      if (count == 0 || (segments[count - 1].style != style && count < MWIN_MAX_PREEDIT_SEGMENTS))
      {
          segments[count++] = (mwinPreeditSegment){start, 0, style};
      }
      segments[count - 1].length = end - segments[count - 1].start;
      if (style == mwin_preeditTarget)
      {
          preedit->selectionStart = targeted ? preedit->selectionStart : start;
          preedit->selectionEnd = end;
          targeted = true;
      }
    };
    NSRange all = NSMakeRange(0, text.length);
    if ([string isKindOfClass:[NSAttributedString class]])
    {
        [(NSAttributedString*)string enumerateAttribute:NSMarkedClauseSegmentAttributeName
                                                inRange:all
                                                options:0
                                             usingBlock:^(id value, NSRange range, BOOL* stop) {
                                               (void)value;
                                               (void)stop;
                                               add(range);
                                             }];
    }
    else
    {
        add(all);
    }
    return count;
}

void mwinMacSetMarkedText(mwinMacPlatform* platform, uint32_t slot, id string, NSRange selected)
{
    mwinMacWindow* window = &platform->windows[slot];
    NSString* text = PlainOf(string);
    if (text.length == 0)
    {
        EndComposition(platform, slot);
        return;
    }
    [window->marked release];
    window->marked = [text copy];
    window->markedSelection = selected;
    if (!window->textInput)
    {
        return;
    }
    mwinContext* context = platform->context;
    size_t length = 0;
    size_t size = 0;
    char* bytes = mwinAppleUtf8Of(context, text, &length, &size);
    if (bytes == nullptr || length > UINT32_MAX)
    {
        // The program's composition cannot follow the input method's.
        mwinEvent reset = {.kind = mwin_eventInputStateReset};
        Post(platform, slot, &reset);
        if (bytes != nullptr)
        {
            mwinRelease(&context->allocator, bytes, size, 1);
        }
        return;
    }
    mwinPreeditSegment segments[MWIN_MAX_PREEDIT_SEGMENTS];
    mwinEvent event = {.kind = mwin_eventImePreedit};
    mwinPreeditEvent* preedit = &event.data.preedit;
    preedit->text = bytes;
    preedit->length = (uint32_t)length;
    // The caret follows the method's selection, whose span is the
    // selection when no clause is marked as the target.
    bool placed = selected.location != NSNotFound;
    uint32_t start =
        placed ? (uint32_t)mwinBytesBefore(bytes, length, muni_unitUtf16, selected.location)
               : (uint32_t)length;
    uint32_t end =
        placed ? (uint32_t)mwinBytesBefore(bytes, length, muni_unitUtf16, NSMaxRange(selected))
               : (uint32_t)length;
    preedit->caret = (int32_t)end;
    preedit->selectionStart = start;
    preedit->selectionEnd = end;
    preedit->segmentCount = Segment(string, text, segments, preedit);
    preedit->segments = segments;
    Post(platform, slot, &event);
    mwinRelease(&context->allocator, bytes, size, 1);
}

void mwinMacInsertText(mwinMacPlatform* platform, uint32_t slot, id string)
{
    PostText(platform, slot, PlainOf(string));
    EndComposition(platform, slot);
}

void mwinMacUnmarkText(mwinMacPlatform* platform, uint32_t slot)
{
    mwinMacWindow* window = &platform->windows[slot];
    // The marked text is accepted as it is.
    if (window->marked != nil)
    {
        PostText(platform, slot, window->marked);
    }
    EndComposition(platform, slot);
}

void mwinMacLimitInputSources(NSView* view, bool all)
{
    view.inputContext.allowedInputSourceLocales =
        all ? nil : @[ NSAllRomanInputSourcesLocaleIdentifier ];
}

NSRect mwinMacCaretOnScreen(const mwinMacPlatform* platform, uint32_t slot)
{
    const mwinMacWindow* window = &platform->windows[slot];
    mwinRect caret = window->caret;
    NSRect inView =
        NSMakeRect((CGFloat)caret.x, (CGFloat)caret.y, (CGFloat)caret.width, (CGFloat)caret.height);
    return [window->window convertRectToScreen:[window->view convertRect:inView toView:nil]];
}

mwinOutcome mwinMacSetTextInput(mwinMacPlatform* platform, uint32_t slot, bool enabled)
{
    mwinMacWindow* window = &platform->windows[slot];
    if (!enabled && window->textInput)
    {
        // The composition is dropped, not committed, if the method
        // accepts it on its way out.
        [window->marked release];
        window->marked = nil;
        [window->view.inputContext discardMarkedText];
        EndComposition(platform, slot);
    }
    window->textInput = enabled;
    mwinMacLimitInputSources(window->view, enabled);
    // The candidate window moves to the caret.
    [window->view.inputContext invalidateCharacterCoordinates];
    return mwin_outcomeDone;
}

mwinOutcome mwinMacSetTextCaret(mwinMacPlatform* platform, uint32_t slot, mwinRect caret)
{
    mwinMacWindow* window = &platform->windows[slot];
    window->caret = caret;
    [window->view.inputContext invalidateCharacterCoordinates];
    return mwin_outcomeDone;
}
