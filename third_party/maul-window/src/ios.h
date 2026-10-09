// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the iOS backend keeps (mwin-0025): per window slot its scene, its
// UIWindow, the view controller and the library's view with its
// CAMetalLayer, and what the program was told of it; per monitor slot
// the screen shown there and what was last said of it; the scene waiting
// for a window; what drives the program's frames; whether the
// application runs; the keyboard's state; and the gamepads. Objects are held with manual retain and
// release. Included by the backend's Objective-C files only.

#ifndef MAUL_WINDOW_SRC_IOS_H
#define MAUL_WINDOW_SRC_IOS_H

#include "apple_pad.h"
#include "core.h"

#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>

typedef struct mwinIOSPlatform mwinIOSPlatform;

// The key codes a UIKey gives that the backend keeps a state of: the
// HID usages of the keyboard page up to the right Meta key.
#define MWIN_IOS_KEY_CODES 256

// The notifications that announce changes of the system's facts.
#define MWIN_IOS_SYSTEM_OBSERVERS 5

// A window: nil objects in a free slot. Its size in pixels, its scale,
// its place in logical units on its screen, its safe area, its monitor
// slot (-1 before the first) and its mode, as last posted.
typedef struct mwinIOSWindow
{
    UIWindowScene* scene;
    UIWindow* window;
    UIViewController* controller;
    UIView* view;
    CAMetalLayer* layer;
    uint32_t width;
    uint32_t height;
    float scale;
    mwinPosition position;
    mwinInsets safeArea;
    int32_t monitor;
    mwinWindowMode mode;
    // The pointer's buttons held over the view (bit b - 1 for button b).
    uint8_t buttons;
    // Whether the window accepts text and where its caret is; whether
    // the program asked for the on-screen keyboard, for what; the part of
    // the window the keyboard covers, as last posted.
    bool textInput;
    mwinRect caret;
    bool keyboard;
    mwinInputPurpose purpose;
    mwinRect covered;
    // What a drag over the window carries (0 when none is), and where it
    // was last posted.
    mwinDragContents dragContents;
    mwinPosition dragPosition;
} mwinIOSWindow;

// Where a touch is in its life, as UIKit's touch methods tell.
typedef enum mwinIOSTouchPhase
{
    mwin_iosTouchBegan,
    mwin_iosTouchMoved,
    mwin_iosTouchEnded,
    mwin_iosTouchCancelled,
} mwinIOSTouchPhase;

struct mwinIOSPlatform
{
    mwinContext* context;
    mwinIOSWindow* windows;
    // Per monitor slot, its UIScreen (held), nil when free, and what the
    // program was last told of it.
    id* screens;
    mwinMonitorInfo* screenInfo;
    // A scene of the application's role that has no window yet (held),
    // which the next window takes; nil when none waits.
    UIWindowScene* waitingScene;
    // What calls the program's frames: the display link, and the object
    // it calls.
    CADisplayLink* link;
    id stepper;
    // Whether the program was told the application stopped running.
    bool suspended;
    // What each printing key typed when last pressed under the current
    // layout (0 before), the keys held, and what tells of the on-screen
    // keyboard's frame and of the input mode's changes.
    mwinKey meanings[MWIN_IOS_KEY_CODES];
    uint8_t held[MWIN_IOS_KEY_CODES / 8];
    id keyboardObservers[2];
    // GameController's pads (apple_pad.h).
    mwinApplePads pads;
    // Whether the idle timer is off, keeping the display awake.
    bool awake;
    // The document pickers showing; nil before the first.
    id dialogs;
    // What announces changes of the system's facts, and whether the
    // battery was watched before the backend watched it.
    id systemObservers[MWIN_IOS_SYSTEM_OBSERVERS];
    bool batteryWatched;
};

// A window's view: its platform and slot, null once its window is
// destroyed; the input view that keeps the on-screen keyboard away; an
// input method's marked text (nil when none) with its selection in
// UTF-16 units, the delegate UIKit gives the text input client, not
// held, and the tokenizer it asks for; its drop interaction's delegate;
// the program's accessibility root. Its text input methods are in
// ios_text.m.
@interface MwinIOSView : UIView
{
  @public
    mwinIOSPlatform* platform;
    uint32_t slot;
    UIView* noKeyboard;
    NSString* marked;
    NSRange markedSelection;
    id<UITextInputDelegate> inputDelegate;
    UITextInputStringTokenizer* tokenizer;
    // The delegate of its drop interaction, which holds it weakly.
    id dropper;
    // The root of the program's accessibility tree, held while it is.
    id accessibilityRoot;
}
@end

// The platform of a context whose backend is iOS.
mwinIOSPlatform* mwinIOSPlatformOf(const mwinContext* context);

// The context of the program running, or null once it ended: what an
// answer UIKit gives later checks first.
mwinContext* mwinIOSCurrentContext(void);

// Nanoseconds on the clock UIKit's events count (the uptime without
// sleep).
uint64_t mwinIOSNow(void);

// Reads the connected scenes' screens into the monitor slots, adding,
// changing and removing monitors as they differ from what was said, and
// forgets them all at the end (ios_output.m).
void mwinIOSReadScreens(mwinIOSPlatform* platform, uint64_t timeNs);
void mwinIOSForgetScreens(mwinIOSPlatform* platform);

// The monitor slot of a screen, or -1.
int32_t mwinIOSMonitorOf(const mwinIOSPlatform* platform, UIScreen* screen);

// A window's view, made retained, whose layer is a CAMetalLayer, and the
// controller that shows it (ios_view.m).
UIView* mwinIOSCreateView(mwinIOSPlatform* platform, uint32_t slot, CGRect frame);
UIViewController* mwinIOSCreateController(mwinIOSPlatform* platform, uint32_t slot, UIView* view);

// An accessibility root request's outcome: the program's root, an
// object of the UIAccessibility protocols, as the view's element.
mwinOutcome mwinIOSSetAccessibilityRoot(mwinIOSPlatform* platform, uint32_t slot, id root);

// Unties a destroyed window's view, which UIKit may still lay out.
void mwinIOSForgetView(UIView* view);

// The contract's modifiers of UIKit's flags (ios_pointer.m).
mwinModifiers mwinIOSModifiersOf(UIKeyModifierFlags flags);

// Touches in a phase: fingers, the Pencil and the pointer's clicks; the
// pointer hovering; the pointer's scrolling (ios_pointer.m).
void mwinIOSTouches(mwinIOSPlatform* platform, uint32_t slot, NSSet<UITouch*>* touches,
                    UIEvent* event, mwinIOSTouchPhase phase);
void mwinIOSHover(mwinIOSPlatform* platform, uint32_t slot, UIHoverGestureRecognizer* hover);
void mwinIOSScroll(mwinIOSPlatform* platform, uint32_t slot, UIPanGestureRecognizer* pan);

// Keys, text and the on-screen keyboard (ios_keys.m): a hardware
// keyboard's presses; text typed into the view and a deletion; what a
// key means, the input mode's language, and what tells of changes;
// text input and the on-screen keyboard's requests.
void mwinIOSPresses(mwinIOSPlatform* platform, uint32_t slot, NSSet<UIPress*>* presses, bool down);
void mwinIOSInsertText(mwinIOSPlatform* platform, uint32_t slot, NSString* text);
void mwinIOSDeleteBackward(mwinIOSPlatform* platform, uint32_t slot);
mwinKey mwinIOSMapKeyCode(const mwinIOSPlatform* platform, mwinKeyCode code);
mwinResult mwinIOSKeyboardLayout(const mwinIOSPlatform* platform, char* buffer, size_t capacity,
                                 size_t* lengthOut);
void mwinIOSWatchKeyboard(mwinIOSPlatform* platform);
void mwinIOSUnwatchKeyboard(mwinIOSPlatform* platform);
UIKeyboardType mwinIOSKeyboardTypeOf(mwinInputPurpose purpose);
mwinOutcome mwinIOSSetTextInput(mwinIOSPlatform* platform, uint32_t slot, bool enabled,
                                mwinRect caret);
mwinOutcome mwinIOSSetVirtualKeyboard(mwinIOSPlatform* platform, uint32_t slot, bool visible,
                                      mwinInputPurpose purpose);

// Compositions (ios_text.m): the text input client's marked text,
// accepted or dropped.
void mwinIOSSetMarkedText(mwinIOSPlatform* platform, uint32_t slot, NSString* text,
                          NSRange selected);
void mwinIOSEndComposition(mwinIOSPlatform* platform, uint32_t slot, bool accept);

// The clipboard and services (ios_services.m): the clipboard's
// requests; an address opened, answered later (-1) or failed; the idle
// timer turned off while wanted.
mwinOutcome mwinIOSWriteClipboard(const mwinIOSPlatform* platform);
mwinOutcome mwinIOSReadClipboard(mwinIOSPlatform* platform);
mwinOutcome mwinIOSReadClipboardData(mwinIOSPlatform* platform, const mwinRequest* request);
int mwinIOSOpenUrl(mwinIOSPlatform* platform, uint32_t slot, uint32_t request);
void mwinIOSKeepAwake(mwinIOSPlatform* platform, bool wanted);

// The system's facts and locales (ios_system.m): read again, and
// watched from the first scene to the backend's stop.
void mwinIOSReadSystem(mwinIOSPlatform* platform);
void mwinIOSWatchSystem(mwinIOSPlatform* platform);
void mwinIOSUnwatchSystem(mwinIOSPlatform* platform);

// File dialogs (ios_dialog.m): a picker shown for a request, answered
// later (-1) or failed; those whose requests went taken away at a pump;
// those of a window, or all (-1), closed without an answer.
int mwinIOSAskDialog(mwinIOSPlatform* platform, uint32_t slot, uint32_t request);
void mwinIOSPumpDialogs(mwinIOSPlatform* platform);
void mwinIOSCloseDialogs(mwinIOSPlatform* platform, int64_t slot);

// Drag and drop (ios_drop.m): a view's drop interaction, and its
// delegate, made retained, and untied from a destroyed window.
id mwinIOSWatchDrops(mwinIOSPlatform* platform, uint32_t slot, UIView* view);
void mwinIOSForgetDrops(id dropper);

// Posts what changed of a window's scale, size, mode, place, safe area
// and monitor, after UIKit laid it out (ios_window.m).
void mwinIOSLayout(mwinIOSPlatform* platform, uint32_t slot);

// The slot of the window a scene shows, or -1.
int32_t mwinIOSSlotOfScene(const mwinIOSPlatform* platform, UIScene* scene);

// The backend's window operations (ios_window.m).
void mwinIOSCreateWindow(mwinContext* context, uint32_t slot);
void mwinIOSDestroyWindow(mwinContext* context, uint32_t slot);
void mwinIOSSubmit(mwinContext* context, uint32_t slot, uint32_t request);

#endif // MAUL_WINDOW_SRC_IOS_H
