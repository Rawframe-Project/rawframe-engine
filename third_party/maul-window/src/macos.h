// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the macOS backend keeps (mwin-0024): per window slot its NSWindow,
// the library's content view with its CAMetalLayer, the window's delegate
// and what the program was told of it; per monitor slot the display
// shown there; what drives the program's frames; the keyboard's layout,
// the cursors, the pen, the gamepads, the services, the dialogs and the
// system's facts. Objects are held with manual
// retain and release. Included by the backend's Objective-C files only.

#ifndef MAUL_WINDOW_SRC_MACOS_H
#define MAUL_WINDOW_SRC_MACOS_H

#include "apple_pad.h"
#include "core.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/CADisplayLink.h>
#import <QuartzCore/CAMetalLayer.h>

typedef struct mwinMacPlatform mwinMacPlatform;

// A window: nil objects in a free slot. Its size in pixels, its scale,
// its place in logical units from the top left of the primary screen,
// its monitor slot (-1 before the first) and its mode, as last posted.
typedef struct mwinMacWindow
{
    NSWindow* window;
    NSView* view;
    CAMetalLayer* layer;
    id delegate;
    uint32_t width;
    uint32_t height;
    float scale;
    mwinPosition position;
    int32_t monitor;
    mwinWindowMode mode;
    // The mouse buttons held over the view (bit b - 1 for button b), and
    // whether the pointer is over it.
    uint8_t buttons;
    bool pointerInside;
    // The cursor asked for: a shape, or a cursor made from images when
    // cursorImage is live (mwin-0027).
    mwinCursorMode cursorMode;
    mwinCursorShape cursorShape;
    mwinCursorId cursorImage;
    // Whether the window accepts text, where its caret is, and an input
    // method's marked text (nil when none) with its selection in UTF-16
    // units.
    bool textInput;
    mwinRect caret;
    NSString* marked;
    NSRange markedSelection;
    // The pen's state over the window, as last posted.
    mwinPenFlags penFlags;
    // What a drag over the window carries (0 when none is), and where it
    // was last posted.
    mwinDragContents dragContents;
    mwinPosition dragPosition;
    // The edge a press on an edge region resizes from (mwin_hitClient
    // when none), with the frame and the pointer's place at the press.
    mwinHitKind resizing;
    NSRect resizeFrame;
    NSPoint resizeStart;
} mwinMacWindow;

struct mwinMacPlatform
{
    mwinContext* context;
    mwinMacWindow* windows;
    // Per monitor slot, the CGDirectDisplayID of its screen; 0 when free.
    uint32_t* displays;
    // What calls the program's frames: a display link, or a timer before
    // macOS 14, and the object they call.
    id driver;
    id stepper;
    id screensObserver;
    // The current keyboard input source (a TISInputSourceRef, held), what
    // tells of its changes, and what hands Command's key releases on.
    const void* layout;
    id layoutObserver;
    id keyUpMonitor;
    // The blank cursor of the hiding modes, made when first needed, and
    // the slot of the window whose cursor is captured, plus one; 0 when
    // none is.
    NSCursor* blankCursor;
    uint32_t captured;
    // Whether the pen near the tablet shows its eraser end.
    bool penEraser;
    // GameController's pads (apple_pad.h).
    mwinApplePads pads;
    // Whether the display is kept awake, and the power assertion that
    // does it (an IOPMAssertionID).
    bool awake;
    uint32_t awakeAssertion;
    // The file dialogs showing; nil before the first.
    id dialogs;
    // What announces changes of the system's facts: notifications, the
    // appearance's observer, and the power source's run loop source (a
    // CFRunLoopSourceRef, held).
    id systemObservers[4];
    id appearanceWatcher;
    const void* powerSource;
};

// The platform of a context whose backend is macOS.
mwinMacPlatform* mwinMacPlatformOf(const mwinContext* context);

// Nanoseconds on the clock NSEvent timestamps count (the uptime without
// sleep).
uint64_t mwinMacNow(void);

// Reads the screens into the monitor slots, adding, changing and
// removing monitors, and forgets them all at the end.
void mwinMacReadScreens(mwinMacPlatform* platform, uint64_t timeNs);
void mwinMacForgetScreens(mwinMacPlatform* platform);

// The monitor slot of a screen, or -1.
int32_t mwinMacMonitorOf(const mwinMacPlatform* platform, NSScreen* screen);

// The height of the primary screen in points, which turns AppKit's
// bottom-left coordinates into the contract's top-left ones.
CGFloat mwinMacPrimaryHeight(void);

// A window's content view, made retained: the view class of the slot's
// window (macos_view.m).
NSView* mwinMacCreateView(mwinMacPlatform* platform, uint32_t slot, NSRect frame);

// An accessibility root request's outcome: the program's root, an
// object of the NSAccessibility protocol, as the view's child
// (macos_view.m).
mwinOutcome mwinMacSetAccessibilityRoot(mwinMacPlatform* platform, uint32_t slot, id root);

// The physical key of a virtual key code; what the current layout makes
// of a key with no modifier; the modifiers of an event's flags.
mwinKeyCode mwinMacCodeOf(uint16_t virtualKey);
mwinKey mwinMacMeaningOf(const mwinMacPlatform* platform, uint16_t virtualKey, mwinKeyCode code);
mwinModifiers mwinMacModifiersOf(NSEventModifierFlags flags);

// What a flagsChanged: event did to a key: 1 pressed, 0 released, 2
// toggled (Caps Lock), -1 when the key is no modifier.
int mwinMacModifierChange(mwinKeyCode code, NSEventModifierFlags flags);

// The backend's mapKeyCode and keyboardLayout.
mwinKey mwinMacMapKeyCode(const mwinMacPlatform* platform, mwinKeyCode code);
mwinResult mwinMacKeyboardLayout(const mwinMacPlatform* platform, char* buffer, size_t capacity,
                                 size_t* lengthOut);

// Reads the keyboard layout and watches its changes and Command's key
// releases, from the start of the backend to its stop.
void mwinMacWatchKeyboard(mwinMacPlatform* platform);
void mwinMacUnwatchKeyboard(mwinMacPlatform* platform);

// An input method's marked text and its selection, committed text, and
// the marked text accepted as it is (macos_text.m); where the caret is
// on the screen; a text input request's outcome.
void mwinMacSetMarkedText(mwinMacPlatform* platform, uint32_t slot, id string, NSRange selected);
void mwinMacInsertText(mwinMacPlatform* platform, uint32_t slot, id string);
void mwinMacUnmarkText(mwinMacPlatform* platform, uint32_t slot);
NSRect mwinMacCaretOnScreen(const mwinMacPlatform* platform, uint32_t slot);

// Lets every input source compose into a view, or only Roman keyboard
// layouts.
void mwinMacLimitInputSources(NSView* view, bool all);
mwinOutcome mwinMacSetTextInput(mwinMacPlatform* platform, uint32_t slot, bool enabled,
                                mwinRect caret);

// The cursor a window shows over its view (macos_cursor.m); a cursor
// mode, shape or image request's outcome; the backend's releaseCursor;
// capturing the cursor while the window is the key window, or letting it
// go; letting every cursor go at the stop.
NSCursor* mwinMacCursorOf(mwinMacPlatform* platform, const mwinMacWindow* window);
mwinOutcome mwinMacSetCursorMode(mwinMacPlatform* platform, uint32_t slot, mwinCursorMode mode);
mwinOutcome mwinMacSetCursorShape(mwinMacPlatform* platform, uint32_t slot, mwinCursorShape shape);
mwinOutcome mwinMacSetCursorImage(mwinMacPlatform* platform, uint32_t slot, mwinCursorId cursor);
void mwinMacReleaseCursor(mwinContext* context, uint32_t slot);
void mwinMacApplyCapture(mwinMacPlatform* platform, uint32_t slot, bool focused);
void mwinMacForgetCursors(mwinMacPlatform* platform);

// Posts a pen's event as pen records: true when the event was a pen's
// (macos_pen.m). Notes which end of the pen came near the tablet.
bool mwinMacTakePen(mwinMacPlatform* platform, uint32_t slot, NSEvent* event);
void mwinMacPenProximity(mwinMacPlatform* platform, NSEvent* event);

// The clipboard's text written from the context and read into it; an
// address opened; a file shown; the display kept awake, or let go, at
// each pump (macos_services.m).
mwinOutcome mwinMacWriteClipboard(const mwinMacPlatform* platform);
mwinOutcome mwinMacReadClipboard(mwinMacPlatform* platform);
mwinOutcome mwinMacReadClipboardData(mwinMacPlatform* platform, const mwinRequest* request);
mwinOutcome mwinMacOpenUrl(const mwinRequest* request);
mwinOutcome mwinMacRevealFile(const mwinRequest* request);
void mwinMacKeepAwake(mwinMacPlatform* platform, bool wanted);

// A drag entering a window's view, moving over it, leaving it, and
// dropped on it (macos_drop.m).
NSDragOperation mwinMacDragEntered(mwinMacPlatform* platform, uint32_t slot,
                                   id<NSDraggingInfo> drag);
NSDragOperation mwinMacDragUpdated(mwinMacPlatform* platform, uint32_t slot,
                                   id<NSDraggingInfo> drag);
void mwinMacDragExited(mwinMacPlatform* platform, uint32_t slot);
bool mwinMacDrop(mwinMacPlatform* platform, uint32_t slot, id<NSDraggingInfo> drag);

// Shows a window's file dialog request, answered when the user is done:
// -1. Cancels the panels whose requests went, at each pump; closes a
// window's panels, or every panel at the stop with a slot of -1
// (macos_dialog.m).
int mwinMacAskDialog(mwinMacPlatform* platform, uint32_t slot, uint32_t request);
void mwinMacPumpDialogs(mwinMacPlatform* platform);
void mwinMacCloseDialogs(mwinMacPlatform* platform, int64_t slot);

// Reads the system's facts and locales into the context; watches their
// changes from the start of the backend to its stop (macos_system.m).
void mwinMacReadSystem(mwinMacPlatform* platform);
void mwinMacWatchSystem(mwinMacPlatform* platform);
void mwinMacUnwatchSystem(mwinMacPlatform* platform);

// Owned windows and popups (macos_owned.m): the owner slot of a popup,
// whose place is measured from its owner's content, or -1; a new
// window, borderless for a popup, made retained; a new window placed,
// centered on its owner or on the screen, a popup at its place; a
// window shown or hidden, a child of its owner while it shows; its
// owner let go before it is destroyed; whether it is a menu.
int32_t mwinMacPopupOwner(const mwinMacPlatform* platform, uint32_t slot);
NSWindow* mwinMacMakeWindow(const mwinMacPlatform* platform, uint32_t slot, NSRect content,
                            NSWindowStyleMask style);
void mwinMacPlaceNew(mwinMacPlatform* platform, uint32_t slot);
void mwinMacShow(mwinMacPlatform* platform, uint32_t slot, bool visible);
void mwinMacForgetOwner(mwinMacPlatform* platform, uint32_t slot);
bool mwinMacIsMenu(const mwinMacPlatform* platform, uint32_t slot);

// Moves a window's content to a place: from the top left of the primary
// screen, or of its owner's content for a popup (macos_window.m).
void mwinMacPlace(mwinMacPlatform* platform, uint32_t slot, mwinPosition position);

struct mwinIconCopyImage;

// An icon request's outcome: the application's icon (macos_icon.m);
// and an image of an icon's or a cursor's as a representation of a
// size in points, autoreleased, or nil.
mwinOutcome mwinMacSetIcon(const mwinRequest* request);
NSBitmapImageRep* mwinMacImageRep(const struct mwinIconCopyImage* image, NSSize size);

// Window chrome (macos_chrome.m): the style mask of a style, and a
// window given a style; a press, a drag and a release that the hit
// regions take (true when taken: not the program's); a size limit, an
// aspect ratio or an opacity request's outcome.
NSWindowStyleMask mwinMacStyleMaskOf(mwinWindowStyle style);
void mwinMacApplyStyle(NSWindow* window, mwinWindowStyle style);
bool mwinMacPressChrome(mwinMacPlatform* platform, uint32_t slot, NSEvent* event);
bool mwinMacDragChrome(mwinMacPlatform* platform, uint32_t slot, NSEvent* event);
bool mwinMacReleaseChrome(mwinMacPlatform* platform, uint32_t slot);
mwinOutcome mwinMacSetLimits(mwinMacPlatform* platform, uint32_t slot, mwinSize minimum,
                             mwinSize maximum);
mwinOutcome mwinMacSetAspect(mwinMacPlatform* platform, uint32_t slot, uint32_t width,
                             uint32_t height);
mwinOutcome mwinMacSetOpacity(mwinMacPlatform* platform, uint32_t slot, float opacity);

// The backend's window operations (backend.h).
void mwinMacCreateWindow(mwinContext* context, uint32_t slot);
void mwinMacDestroyWindow(mwinContext* context, uint32_t slot);
void mwinMacSubmit(mwinContext* context, uint32_t slot, uint32_t request);

#endif // MAUL_WINDOW_SRC_MACOS_H
