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

#include "apple_text.h"
#include "macos.h"

#include <string.h>

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

// Ends a composition the window shows.
static void EndComposition(mwinMacPlatform* platform, uint32_t slot)
{
    mwinMacWindow* window = &platform->windows[slot];
    [window->marked release];
    window->marked = nil;
    if (!platform->context->windows[slot].state.composing)
    {
        return;
    }
    mwinEvent end = {.type = mwin_eventImePreedit};
    end.data.preedit.caret = -1;
    Post(platform, slot, &end);
}

// Typed text without control characters or AppKit's function key
// characters, which are keys.
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
    const char* bytes = kept.UTF8String;
    size_t length = bytes != nullptr ? strlen(bytes) : 0;
    if (length == 0 || length > UINT32_MAX)
    {
        return;
    }
    mwinEvent event = {.type = mwin_eventTextInput};
    event.data.text = (mwinTextEvent){bytes, (uint32_t)length};
    Post(platform, slot, &event);
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

// The clauses of marked text in bytes, neighbours of one style joined;
// past the last segment the rest joins it. Returns the count, with the
// target clauses' span in *preedit's selection when there is one.
static uint32_t Segment(id string, NSString* text, mwinPreeditSegment* segments,
                        mwinPreeditEvent* preedit)
{
    __block uint32_t count = 0;
    __block bool targeted = false;
    void (^add)(NSRange) = ^(NSRange range) {
      mwinPreeditStyle style = StyleOf(string, range);
      uint32_t start = mwinAppleBytesBefore(text, range.location);
      uint32_t end = mwinAppleBytesBefore(text, NSMaxRange(range));
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
    const char* bytes = text.UTF8String;
    size_t length = bytes != nullptr ? strlen(bytes) : 0;
    if (length == 0 || length > UINT32_MAX)
    {
        return;
    }
    mwinPreeditSegment segments[MWIN_MAX_PREEDIT_SEGMENTS];
    mwinEvent event = {.type = mwin_eventImePreedit};
    mwinPreeditEvent* preedit = &event.data.preedit;
    preedit->text = bytes;
    preedit->length = (uint32_t)length;
    // The caret follows the method's selection, whose span is the
    // selection when no clause is marked as the target.
    uint32_t start = selected.location != NSNotFound ? mwinAppleBytesBefore(text, selected.location)
                                                     : (uint32_t)length;
    uint32_t end = selected.location != NSNotFound
                       ? mwinAppleBytesBefore(text, NSMaxRange(selected))
                       : (uint32_t)length;
    preedit->caret = (int32_t)end;
    preedit->selectionStart = start;
    preedit->selectionEnd = end;
    preedit->segmentCount = Segment(string, text, segments, preedit);
    preedit->segments = segments;
    Post(platform, slot, &event);
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

mwinOutcome mwinMacSetTextInput(mwinMacPlatform* platform, uint32_t slot, bool enabled,
                                mwinRect caret)
{
    mwinMacWindow* window = &platform->windows[slot];
    window->caret = caret;
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
