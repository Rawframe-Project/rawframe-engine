// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The content view of a macOS window (macos.h): flipped, so its
// coordinates run down from the top left as the contract's do, backed
// by a CAMetalLayer for a GPU layer to present to, and the responder its
// window's keyboard, mouse and wheel input comes to, and the way into
// the program's accessibility tree. Keys come by
// virtual key code; text comes through the view's text input client,
// with Command held no key types text, and input methods compose into
// it only while the window accepts text (macos_text.m). Quick clicks are
// AppKit's click counts; a pen's mouse events are the pen's
// (macos_pen.m); drags come to it too (macos_drop.m). Precise scrolling
// (touchpads, Magic Mouse) comes in points, ten to a detent; a wheel's
// in lines, one to a detent. The sign is what the user's scrolling
// direction makes it, as on the other platforms.

#include "accessibility.h"
#include "macos.h"

// Precise scrolling's points per detent.
#define POINTS_PER_DETENT 10.0

@interface MwinMacView : NSView <NSTextInputClient>
{
  @public
    mwinMacPlatform* platform;
    uint32_t slot;
    // The root of the program's accessibility tree, held while it is.
    id accessibilityRoot;
}
@end

static mwinMacWindow* WindowOf(const MwinMacView* view)
{
    return &view->platform->windows[view->slot];
}

static void Post(const MwinMacView* view, mwinEvent* event)
{
    event->timeNs = mwinMacNow();
    mwinPost(view->platform->context, view->slot, event);
}

static void PostKey(const MwinMacView* view, mwinEventType type, NSEvent* event, mwinKeyCode code)
{
    mwinEvent record = {.type = type};
    record.data.key = (mwinKeyEvent){code, mwinMacModifiersOf(event.modifierFlags),
                                     mwinMacMeaningOf(view->platform, event.keyCode, code),
                                     // AppKit raises for a modifier event's repeat.
                                     event.type == NSEventTypeKeyDown && event.ARepeat};
    Post(view, &record);
}

// A modifier key went down or up, or Caps Lock toggled, which posts a
// press and a release.
static void OnFlags(const MwinMacView* view, NSEvent* event)
{
    mwinKeyCode code = mwinMacCodeOf(event.keyCode);
    int change = mwinMacModifierChange(code, event.modifierFlags);
    if (change < 0)
    {
        return;
    }
    if (change != 0)
    {
        PostKey(view, mwin_eventKeyDown, event, code);
    }
    if (change != 1)
    {
        PostKey(view, mwin_eventKeyUp, event, code);
    }
}

static mwinMouseButton ButtonOf(NSEvent* event)
{
    switch (event.type)
    {
    case NSEventTypeLeftMouseDown:
    case NSEventTypeLeftMouseUp:
        return mwin_buttonLeft;
    case NSEventTypeRightMouseDown:
    case NSEventTypeRightMouseUp:
        return mwin_buttonRight;
    default:
        break;
    }
    switch (event.buttonNumber)
    {
    case 2:
        return mwin_buttonMiddle;
    case 3:
        return mwin_buttonBack;
    case 4:
        return mwin_buttonForward;
    default:
        return 0;
    }
}

static void PostPointer(const MwinMacView* view, mwinEventType type, NSEvent* event,
                        mwinMouseButton button, uint8_t clicks)
{
    NSPoint point = [view convertPoint:event.locationInWindow fromView:nil];
    mwinEvent record = {.type = type};
    record.data.pointer = (mwinPointerEvent){{(float)point.x, (float)point.y},
                                             mwinMacModifiersOf(event.modifierFlags),
                                             WindowOf(view)->buttons,
                                             button,
                                             clicks};
    Post(view, &record);
}

static void OnButton(const MwinMacView* view, NSEvent* event, bool down)
{
    if (mwinMacTakePen(view->platform, view->slot, event))
    {
        return;
    }
    mwinMouseButton button = ButtonOf(event);
    if (button == 0)
    {
        return;
    }
    mwinMacWindow* window = WindowOf(view);
    uint8_t bit = (uint8_t)(1u << (button - 1));
    window->buttons = down ? (uint8_t)(window->buttons | bit) : (uint8_t)(window->buttons & ~bit);
    NSInteger clicks = event.clickCount;
    PostPointer(view, down ? mwin_eventButtonDown : mwin_eventButtonUp, event, button,
                (uint8_t)(clicks < 0           ? 0
                          : clicks > UINT8_MAX ? UINT8_MAX
                                               : clicks));
}

static void OnMove(const MwinMacView* view, NSEvent* event)
{
    if (mwinMacTakePen(view->platform, view->slot, event))
    {
        return;
    }
    mwinMacWindow* window = WindowOf(view);
    // A captured cursor stays put; the mouse's motion is the delta.
    if (view->platform->captured == view->slot + 1)
    {
        mwinEvent record = {.type = mwin_eventRawPointerDelta};
        record.data.delta = (mwinDeltaEvent){(float)event.deltaX, (float)event.deltaY};
        Post(view, &record);
        return;
    }
    if (!window->pointerInside)
    {
        window->pointerInside = true;
        PostPointer(view, mwin_eventCursorEntered, event, 0, 0);
    }
    PostPointer(view, mwin_eventCursorMoved, event, 0, 0);
}

static void OnWheel(const MwinMacView* view, NSEvent* event)
{
    double per = event.hasPreciseScrollingDeltas ? POINTS_PER_DETENT : 1.0;
    // AppKit counts toward the left as positive x.
    mwinWheelEvent wheel = {(float)(-event.scrollingDeltaX / per),
                            (float)(event.scrollingDeltaY / per)};
    if (wheel.x == 0.0f && wheel.y == 0.0f)
    {
        return;
    }
    mwinEvent record = {.type = mwin_eventWheel};
    record.data.wheel = wheel;
    Post(view, &record);
}

@implementation MwinMacView
- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self != nil)
    {
        self.wantsLayer = YES;
        NSTrackingAreaOptions options = NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved |
                                        NSTrackingActiveAlways | NSTrackingInVisibleRect |
                                        NSTrackingEnabledDuringMouseDrag;
        NSTrackingArea* area = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                                            options:options
                                                              owner:self
                                                           userInfo:nil];
        [self addTrackingArea:area];
        [area release];
    }
    return self;
}

- (CALayer*)makeBackingLayer
{
    return [CAMetalLayer layer];
}

- (BOOL)wantsUpdateLayer
{
    return YES;
}

- (BOOL)isFlipped
{
    return YES;
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

// A press is never AppKit's to move the window: under a custom chrome's
// clear title bar it is the program's, or a caption region's.
- (BOOL)mouseDownCanMoveWindow
{
    return NO;
}

// A click that activates the window is the program's too.
- (BOOL)acceptsFirstMouse:(NSEvent*)event
{
    (void)event;
    return YES;
}

- (void)keyDown:(NSEvent*)event
{
    mwinKeyCode code = mwinMacCodeOf(event.keyCode);
    if (code != mwin_codeUnknown)
    {
        PostKey(self, mwin_eventKeyDown, event, code);
    }
    if ((event.modifierFlags & NSEventModifierFlagCommand) == 0)
    {
        [self interpretKeyEvents:@[ event ]];
    }
}

- (void)keyUp:(NSEvent*)event
{
    mwinKeyCode code = mwinMacCodeOf(event.keyCode);
    if (code != mwin_codeUnknown)
    {
        PostKey(self, mwin_eventKeyUp, event, code);
    }
}

- (void)flagsChanged:(NSEvent*)event
{
    OnFlags(self, event);
}

// A press on a caption or an edge moves or resizes the window, and is
// not the program's.
- (void)mouseDown:(NSEvent*)event
{
    if (!mwinMacPressChrome(platform, slot, event))
    {
        OnButton(self, event, true);
    }
}

- (void)mouseUp:(NSEvent*)event
{
    if (!mwinMacReleaseChrome(platform, slot))
    {
        OnButton(self, event, false);
    }
}

- (void)rightMouseDown:(NSEvent*)event
{
    OnButton(self, event, true);
}

- (void)rightMouseUp:(NSEvent*)event
{
    OnButton(self, event, false);
}

- (void)otherMouseDown:(NSEvent*)event
{
    OnButton(self, event, true);
}

- (void)otherMouseUp:(NSEvent*)event
{
    OnButton(self, event, false);
}

- (void)mouseMoved:(NSEvent*)event
{
    OnMove(self, event);
}

- (void)mouseDragged:(NSEvent*)event
{
    if (!mwinMacDragChrome(platform, slot, event))
    {
        OnMove(self, event);
    }
}

- (void)rightMouseDragged:(NSEvent*)event
{
    OnMove(self, event);
}

- (void)otherMouseDragged:(NSEvent*)event
{
    OnMove(self, event);
}

- (void)mouseEntered:(NSEvent*)event
{
    OnMove(self, event);
}

- (void)mouseExited:(NSEvent*)event
{
    mwinMacWindow* window = WindowOf(self);
    if (window->pointerInside)
    {
        window->pointerInside = false;
        PostPointer(self, mwin_eventCursorLeft, event, 0, 0);
    }
}

- (void)scrollWheel:(NSEvent*)event
{
    OnWheel(self, event);
}

// The dragging destination.
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)drag
{
    return mwinMacDragEntered(platform, slot, drag);
}

- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)drag
{
    return mwinMacDragUpdated(platform, slot, drag);
}

- (void)draggingExited:(id<NSDraggingInfo>)drag
{
    (void)drag;
    mwinMacDragExited(platform, slot);
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)drag
{
    return mwinMacDrop(platform, slot, drag);
}

- (void)tabletPoint:(NSEvent*)event
{
    (void)mwinMacTakePen(platform, slot, event);
}

- (void)tabletProximity:(NSEvent*)event
{
    mwinMacPenProximity(platform, event);
}

- (void)resetCursorRects
{
    [self addCursorRect:self.visibleRect cursor:mwinMacCursorOf(platform, WindowOf(self))];
}

// The accessibility client's way in: the program's root is the view's
// child, and answers what is focused and what is under a point. The
// first question tells the program that a client came, root or none.
static id Asked(const MwinMacView* view)
{
    mwinNoteAccessibilityAsked(view->platform->context, view->slot);
    return view->accessibilityRoot;
}

- (NSArray*)accessibilityChildren
{
    id root = Asked(self);
    return root != nil ? @[ root ] : [super accessibilityChildren];
}

- (id)accessibilityFocusedUIElement
{
    id root = Asked(self);
    id focused = root != nil ? [root accessibilityFocusedUIElement] : nil;
    return focused != nil ? focused : root != nil ? root : [super accessibilityFocusedUIElement];
}

- (id)accessibilityHitTest:(NSPoint)point
{
    id root = Asked(self);
    id hit = root != nil ? [root accessibilityHitTest:point] : nil;
    return hit != nil ? hit : root != nil ? root : [super accessibilityHitTest:point];
}

- (void)dealloc
{
    [accessibilityRoot release];
    [super dealloc];
}

// The text input client.
- (void)insertText:(id)string replacementRange:(NSRange)range
{
    (void)range;
    mwinMacInsertText(platform, slot, string);
}

// Keys that make commands (Enter, the arrows) are keys already; nothing
// beeps.
- (void)doCommandBySelector:(SEL)selector
{
    (void)selector;
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)selected replacementRange:(NSRange)range
{
    (void)range;
    mwinMacSetMarkedText(platform, slot, string, selected);
}

- (void)unmarkText
{
    mwinMacUnmarkText(platform, slot);
}

- (NSRange)selectedRange
{
    const mwinMacWindow* window = WindowOf(self);
    return window->marked != nil ? window->markedSelection : NSMakeRange(NSNotFound, 0);
}

- (NSRange)markedRange
{
    const mwinMacWindow* window = WindowOf(self);
    return window->marked != nil ? NSMakeRange(0, window->marked.length)
                                 : NSMakeRange(NSNotFound, 0);
}

- (BOOL)hasMarkedText
{
    return WindowOf(self)->marked != nil;
}

- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range
                                               actualRange:(NSRangePointer)actual
{
    (void)range;
    (void)actual;
    return nil;
}

- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText
{
    return @[ NSUnderlineStyleAttributeName, NSMarkedClauseSegmentAttributeName ];
}

- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actual
{
    if (actual != nullptr)
    {
        *actual = range;
    }
    return mwinMacCaretOnScreen(platform, slot);
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point
{
    (void)point;
    return NSNotFound;
}
@end

mwinOutcome mwinMacSetAccessibilityRoot(mwinMacPlatform* platform, uint32_t slot, id root)
{
    MwinMacView* view = (MwinMacView*)platform->windows[slot].view;
    [root retain];
    [view->accessibilityRoot release];
    view->accessibilityRoot = root;
    // Clients read the view's children again.
    NSAccessibilityPostNotification(view, NSAccessibilityLayoutChangedNotification);
    return mwin_outcomeDone;
}

NSView* mwinMacCreateView(mwinMacPlatform* platform, uint32_t slot, NSRect frame)
{
    MwinMacView* view = [[MwinMacView alloc] initWithFrame:frame];
    if (view != nil)
    {
        view->platform = platform;
        view->slot = slot;
        mwinMacLimitInputSources(view, false);
        [view registerForDraggedTypes:@[ NSPasteboardTypeFileURL, NSPasteboardTypeString ]];
    }
    return view;
}
