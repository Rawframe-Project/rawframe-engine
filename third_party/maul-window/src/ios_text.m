// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text and input methods on iOS (ios.h). The view is a text input
// client (UITextInput) whose document is only an input method's marked
// text: typed and accepted text leaves at once as text records (through
// ios_keys.m), as on the other platforms. While the window accepts text
// the marked text comes as mwin_eventImePreedit, one clause underlined,
// the method's selection as the selection and its end as the caret, and
// the candidate window goes by the caret the program gave. While it does
// not, a method still composes, as the user expects of their keyboard,
// but only the text it accepts is posted, as on macOS. The program
// turning text input off drops a composition, telling UIKit so.

#include "apple_text.h"
#include "ios.h"

#include <string.h>

// A place in the marked text, in UTF-16 units.
@interface MwinIOSPosition : UITextPosition
{
  @public
    NSInteger offset;
}
@end

@implementation MwinIOSPosition
@end

// A span of the marked text, in UTF-16 units.
@interface MwinIOSRange : UITextRange
{
  @public
    NSInteger from;
    NSInteger to;
}
@end

static MwinIOSPosition* PositionAt(NSInteger offset)
{
    MwinIOSPosition* position = [[[MwinIOSPosition alloc] init] autorelease];
    position->offset = offset;
    return position;
}

static MwinIOSRange* RangeOf(NSInteger from, NSInteger to)
{
    MwinIOSRange* range = [[[MwinIOSRange alloc] init] autorelease];
    range->from = from < to ? from : to;
    range->to = from < to ? to : from;
    return range;
}

@implementation MwinIOSRange
- (UITextPosition*)start
{
    return PositionAt(from);
}

- (UITextPosition*)end
{
    return PositionAt(to);
}

- (BOOL)isEmpty
{
    return from == to;
}
@end

static NSInteger OffsetOf(UITextPosition* position)
{
    return [position isKindOfClass:[MwinIOSPosition class]] ? ((MwinIOSPosition*)position)->offset
                                                            : 0;
}

static void Post(mwinIOSPlatform* platform, uint32_t slot, mwinEvent* event)
{
    event->timeNs = mwinIOSNow();
    mwinPost(platform->context, slot, event);
}

// Ends a composition the program was shown.
static void EndPreedit(mwinIOSPlatform* platform, uint32_t slot)
{
    if (!platform->context->windows[slot].state.composing)
    {
        return;
    }
    mwinEvent end = {.type = mwin_eventImePreedit};
    end.data.preedit.caret = -1;
    Post(platform, slot, &end);
}

static MwinIOSView* ViewOf(mwinIOSPlatform* platform, uint32_t slot)
{
    return (MwinIOSView*)platform->windows[slot].view;
}

void mwinIOSSetMarkedText(mwinIOSPlatform* platform, uint32_t slot, NSString* text,
                          NSRange selected)
{
    MwinIOSView* view = ViewOf(platform, slot);
    [view->marked release];
    view->marked = text.length > 0 ? [text copy] : nil;
    view->markedSelection = selected;
    if (text.length == 0)
    {
        EndPreedit(platform, slot);
        return;
    }
    const char* bytes = text.UTF8String;
    size_t length = bytes != nullptr ? strlen(bytes) : 0;
    if (!platform->windows[slot].textInput || length == 0 || length > UINT32_MAX)
    {
        return;
    }
    mwinPreeditSegment segment = {0, (uint32_t)length, mwin_preeditUnderline};
    mwinEvent event = {.type = mwin_eventImePreedit};
    mwinPreeditEvent* preedit = &event.data.preedit;
    preedit->text = bytes;
    preedit->length = (uint32_t)length;
    bool placed = selected.location != NSNotFound && NSMaxRange(selected) <= text.length;
    uint32_t start = placed ? mwinAppleBytesBefore(text, selected.location) : (uint32_t)length;
    uint32_t end = placed ? mwinAppleBytesBefore(text, NSMaxRange(selected)) : (uint32_t)length;
    preedit->caret = (int32_t)end;
    preedit->selectionStart = start;
    preedit->selectionEnd = end;
    preedit->segments = &segment;
    preedit->segmentCount = 1;
    Post(platform, slot, &event);
}

void mwinIOSEndComposition(mwinIOSPlatform* platform, uint32_t slot, bool accept)
{
    MwinIOSView* view = ViewOf(platform, slot);
    if (view->marked == nil)
    {
        return;
    }
    if (accept)
    {
        NSString* accepted = [view->marked autorelease];
        view->marked = nil;
        mwinIOSInsertText(platform, slot, accepted);
    }
    else
    {
        // UIKit reads the document again, and finds no composition.
        [view->inputDelegate textWillChange:(id<UITextInput>)view];
        [view->marked release];
        view->marked = nil;
        [view->inputDelegate textDidChange:(id<UITextInput>)view];
    }
    EndPreedit(platform, slot);
}

@interface MwinIOSView (MwinText) <UITextInput>
@end

@implementation MwinIOSView (MwinText)
- (BOOL)canBecomeFirstResponder
{
    return YES;
}

- (BOOL)hasText
{
    return YES;
}

- (void)insertText:(NSString*)text
{
    if (platform == nullptr)
    {
        return;
    }
    // Text in place of a composition accepts it as that text.
    [marked release];
    marked = nil;
    mwinIOSInsertText(platform, slot, text);
    EndPreedit(platform, slot);
}

- (void)deleteBackward
{
    if (platform == nullptr)
    {
        return;
    }
    if (marked == nil)
    {
        mwinIOSDeleteBackward(platform, slot);
        return;
    }
    NSRange last = [marked rangeOfComposedCharacterSequenceAtIndex:marked.length - 1];
    NSString* shorter = [marked substringToIndex:last.location];
    mwinIOSSetMarkedText(platform, slot, shorter, NSMakeRange(shorter.length, 0));
}

- (NSString*)textInRange:(UITextRange*)range
{
    NSInteger length = (NSInteger)marked.length;
    MwinIOSRange* span = (MwinIOSRange*)range;
    if (![range isKindOfClass:[MwinIOSRange class]] || span->from < 0 || span->to > length)
    {
        return @"";
    }
    return marked != nil
               ? [marked substringWithRange:NSMakeRange((NSUInteger)span->from,
                                                        (NSUInteger)(span->to - span->from))]
               : @"";
}

- (void)replaceRange:(UITextRange*)range withText:(NSString*)text
{
    if (platform == nullptr)
    {
        return;
    }
    MwinIOSRange* span = (MwinIOSRange*)range;
    NSInteger length = (NSInteger)marked.length;
    if (marked == nil || ![range isKindOfClass:[MwinIOSRange class]] || span->to > length)
    {
        [self insertText:text];
        return;
    }
    NSString* changed =
        [marked stringByReplacingCharactersInRange:NSMakeRange((NSUInteger)span->from,
                                                               (NSUInteger)(span->to - span->from))
                                        withString:text];
    mwinIOSSetMarkedText(platform, slot, changed,
                         NSMakeRange((NSUInteger)span->from + text.length, 0));
}

- (UITextRange*)selectedTextRange
{
    if (marked == nil || markedSelection.location == NSNotFound)
    {
        return RangeOf((NSInteger)marked.length, (NSInteger)marked.length);
    }
    return RangeOf((NSInteger)markedSelection.location, (NSInteger)NSMaxRange(markedSelection));
}

- (void)setSelectedTextRange:(UITextRange*)range
{
    if (platform == nullptr || marked == nil || ![range isKindOfClass:[MwinIOSRange class]])
    {
        return;
    }
    MwinIOSRange* span = (MwinIOSRange*)range;
    NSString* text = [[marked retain] autorelease];
    mwinIOSSetMarkedText(platform, slot, text,
                         NSMakeRange((NSUInteger)span->from, (NSUInteger)(span->to - span->from)));
}

- (UITextRange*)markedTextRange
{
    return marked != nil ? RangeOf(0, (NSInteger)marked.length) : nil;
}

- (NSDictionary<NSAttributedStringKey, id>*)markedTextStyle
{
    return nil;
}

- (void)setMarkedTextStyle:(NSDictionary<NSAttributedStringKey, id>*)style
{
    (void)style;
}

- (void)setMarkedText:(NSString*)text selectedRange:(NSRange)selected
{
    if (platform != nullptr)
    {
        mwinIOSSetMarkedText(platform, slot, text, selected);
    }
}

- (void)unmarkText
{
    if (platform != nullptr)
    {
        mwinIOSEndComposition(platform, slot, true);
    }
}

- (UITextPosition*)beginningOfDocument
{
    return PositionAt(0);
}

- (UITextPosition*)endOfDocument
{
    return PositionAt((NSInteger)marked.length);
}

- (UITextRange*)textRangeFromPosition:(UITextPosition*)from toPosition:(UITextPosition*)to
{
    return RangeOf(OffsetOf(from), OffsetOf(to));
}

- (UITextPosition*)positionFromPosition:(UITextPosition*)position offset:(NSInteger)offset
{
    NSInteger moved = OffsetOf(position) + offset;
    return moved >= 0 && moved <= (NSInteger)marked.length ? PositionAt(moved) : nil;
}

- (UITextPosition*)positionFromPosition:(UITextPosition*)position
                            inDirection:(UITextLayoutDirection)direction
                                 offset:(NSInteger)offset
{
    bool back = direction == UITextLayoutDirectionLeft || direction == UITextLayoutDirectionUp;
    return [self positionFromPosition:position offset:back ? -offset : offset];
}

- (NSComparisonResult)comparePosition:(UITextPosition*)position toPosition:(UITextPosition*)other
{
    NSInteger a = OffsetOf(position);
    NSInteger b = OffsetOf(other);
    return a < b ? NSOrderedAscending : (a > b ? NSOrderedDescending : NSOrderedSame);
}

- (NSInteger)offsetFromPosition:(UITextPosition*)from toPosition:(UITextPosition*)to
{
    return OffsetOf(to) - OffsetOf(from);
}

- (id<UITextInputDelegate>)inputDelegate
{
    return inputDelegate;
}

- (void)setInputDelegate:(id<UITextInputDelegate>)delegate
{
    inputDelegate = delegate;
}

- (id<UITextInputTokenizer>)tokenizer
{
    if (tokenizer == nil)
    {
        tokenizer = [[UITextInputStringTokenizer alloc] initWithTextInput:self];
    }
    return tokenizer;
}

- (UITextPosition*)positionWithinRange:(UITextRange*)range
                   farthestInDirection:(UITextLayoutDirection)direction
{
    bool back = direction == UITextLayoutDirectionLeft || direction == UITextLayoutDirectionUp;
    return back ? range.start : range.end;
}

- (UITextRange*)characterRangeByExtendingPosition:(UITextPosition*)position
                                      inDirection:(UITextLayoutDirection)direction
{
    bool back = direction == UITextLayoutDirectionLeft || direction == UITextLayoutDirectionUp;
    return RangeOf(OffsetOf(position), back ? 0 : (NSInteger)marked.length);
}

- (NSWritingDirection)baseWritingDirectionForPosition:(UITextPosition*)position
                                          inDirection:(UITextStorageDirection)direction
{
    (void)position;
    (void)direction;
    return NSWritingDirectionNatural;
}

- (void)setBaseWritingDirection:(NSWritingDirection)writingDirection forRange:(UITextRange*)range
{
    (void)writingDirection;
    (void)range;
}

// The caret the program gave, where the candidate window goes.
static CGRect CaretOf(const MwinIOSView* view)
{
    if (view->platform == nullptr)
    {
        return CGRectZero;
    }
    mwinRect caret = view->platform->windows[view->slot].caret;
    return CGRectMake((CGFloat)caret.x, (CGFloat)caret.y, (CGFloat)caret.width,
                      (CGFloat)caret.height);
}

- (CGRect)firstRectForRange:(UITextRange*)range
{
    (void)range;
    return CaretOf(self);
}

- (CGRect)caretRectForPosition:(UITextPosition*)position
{
    (void)position;
    return CaretOf(self);
}

- (NSArray<UITextSelectionRect*>*)selectionRectsForRange:(UITextRange*)range
{
    (void)range;
    return @[];
}

- (UITextPosition*)closestPositionToPoint:(CGPoint)point
{
    (void)point;
    return self.endOfDocument;
}

- (UITextPosition*)closestPositionToPoint:(CGPoint)point withinRange:(UITextRange*)range
{
    (void)point;
    return range.end;
}

- (UITextRange*)characterRangeAtPoint:(CGPoint)point
{
    (void)point;
    return RangeOf((NSInteger)marked.length, (NSInteger)marked.length);
}

// The traits: what the on-screen keyboard is, and that nothing is
// corrected, capitalized or completed.
- (UIView*)inputView
{
    return platform != nullptr && platform->windows[slot].keyboard ? nil : noKeyboard;
}

- (UIKeyboardType)keyboardType
{
    return platform != nullptr ? mwinIOSKeyboardTypeOf(platform->windows[slot].purpose)
                               : UIKeyboardTypeDefault;
}

- (BOOL)isSecureTextEntry
{
    return platform != nullptr && platform->windows[slot].purpose == mwin_purposePassword;
}

- (UITextAutocorrectionType)autocorrectionType
{
    return UITextAutocorrectionTypeNo;
}

- (UITextAutocapitalizationType)autocapitalizationType
{
    return UITextAutocapitalizationTypeNone;
}

- (UITextSpellCheckingType)spellCheckingType
{
    return UITextSpellCheckingTypeNo;
}

- (UITextSmartQuotesType)smartQuotesType
{
    return UITextSmartQuotesTypeNo;
}

- (UITextSmartDashesType)smartDashesType
{
    return UITextSmartDashesTypeNo;
}

- (UITextSmartInsertDeleteType)smartInsertDeleteType
{
    return UITextSmartInsertDeleteTypeNo;
}
@end
