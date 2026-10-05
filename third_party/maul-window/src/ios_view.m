// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// iOS windows' views (ios.h): a view backed by a CAMetalLayer, which
// fills its window through the view controller that shows it. A layer
// of the view's own class draws at a scale of one unless told: the view
// takes its screen's scale whenever UIKit lays it out, and the window
// then posts what changed. The status bar is hidden, the window being
// the program's to draw; the safe area says what the system still
// covers. Touches, the pointer's hover and its scrolling come to the
// view (ios_pointer.m), and drags (ios_drop.m); text is typed and
// composed into it (ios_text.m), and a hardware keyboard's presses pass
// through it to the controller (ios_keys.m). It holds the program's
// accessibility root as its element.

#include "accessibility.h"
#include "ios.h"

@implementation MwinIOSView
+ (Class)layerClass
{
    return [CAMetalLayer class];
}

- (void)layoutSubviews
{
    [super layoutSubviews];
    if (platform == nullptr)
    {
        return;
    }
    UIScreen* screen = self.window.windowScene.screen;
    if (screen != nil && self.contentScaleFactor != screen.scale)
    {
        self.contentScaleFactor = screen.scale;
    }
    mwinIOSLayout(platform, slot);
}

static void Touches(MwinIOSView* view, NSSet<UITouch*>* touches, UIEvent* event,
                    mwinIOSTouchPhase phase)
{
    if (view->platform != nullptr)
    {
        mwinIOSTouches(view->platform, view->slot, touches, event, phase);
    }
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    Touches(self, touches, event, mwin_iosTouchBegan);
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    Touches(self, touches, event, mwin_iosTouchMoved);
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    Touches(self, touches, event, mwin_iosTouchEnded);
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    Touches(self, touches, event, mwin_iosTouchCancelled);
}

- (void)dealloc
{
    [noKeyboard release];
    [tokenizer release];
    [marked release];
    [dropper release];
    [accessibilityRoot release];
    [super dealloc];
}

// The recognizers' actions.
- (void)mwinHover:(UIHoverGestureRecognizer*)hover
{
    if (platform != nullptr)
    {
        mwinIOSHover(platform, slot, hover);
    }
}

- (void)mwinScroll:(UIPanGestureRecognizer*)pan
{
    if (platform != nullptr)
    {
        mwinIOSScroll(platform, slot, pan);
    }
}

// The accessibility client's way in: the program's root is the view's
// element. The first question tells the program that a client came,
// root or none.
- (NSArray*)accessibilityElements
{
    if (platform != nullptr)
    {
        mwinNoteAccessibilityAsked(platform->context, slot);
    }
    return accessibilityRoot != nil ? @[ accessibilityRoot ] : [super accessibilityElements];
}

// The light or dark style, and the text size, come with the traits.
- (void)traitCollectionDidChange:(UITraitCollection*)previous
{
    [super traitCollectionDidChange:previous];
    if (platform != nullptr)
    {
        mwinIOSReadSystem(platform);
    }
}

- (void)safeAreaInsetsDidChange
{
    [super safeAreaInsetsDidChange];
    if (platform != nullptr)
    {
        mwinIOSLayout(platform, slot);
    }
}
@end

// The controller takes a hardware keyboard's presses, which come to it
// from the view, and hands them on.
@interface MwinIOSController : UIViewController
{
  @public
    mwinIOSPlatform* platform;
    uint32_t slot;
}
@end

@implementation MwinIOSController
- (BOOL)prefersStatusBarHidden
{
    return YES;
}

static void Presses(MwinIOSController* controller, NSSet<UIPress*>* presses, bool down)
{
    if (controller->platform != nullptr)
    {
        mwinIOSPresses(controller->platform, controller->slot, presses, down);
    }
}

- (void)pressesBegan:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event
{
    Presses(self, presses, true);
    [super pressesBegan:presses withEvent:event];
}

- (void)pressesEnded:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event
{
    Presses(self, presses, false);
    [super pressesEnded:presses withEvent:event];
}

- (void)pressesCancelled:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event
{
    Presses(self, presses, false);
    [super pressesCancelled:presses withEvent:event];
}
@end

UIView* mwinIOSCreateView(mwinIOSPlatform* platform, uint32_t slot, CGRect frame)
{
    MwinIOSView* view = [[MwinIOSView alloc] initWithFrame:frame];
    view->platform = platform;
    view->slot = slot;
    view.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    view.multipleTouchEnabled = YES;
    view->noKeyboard = [[UIView alloc] initWithFrame:CGRectZero];
    view->dropper = mwinIOSWatchDrops(platform, slot, view);
    // No shortcuts bar over a hardware keyboard.
    view.inputAssistantItem.leadingBarButtonGroups = @[];
    view.inputAssistantItem.trailingBarButtonGroups = @[];
    // The pointer's motion without a button, and its scrolling alone; the
    // touches still reach the view.
    UIHoverGestureRecognizer* hover =
        [[UIHoverGestureRecognizer alloc] initWithTarget:view action:@selector(mwinHover:)];
    hover.allowedTouchTypes = @[ @(UITouchTypeIndirectPointer) ];
    hover.cancelsTouchesInView = NO;
    [view addGestureRecognizer:hover];
    [hover release];
    UIPanGestureRecognizer* pan =
        [[UIPanGestureRecognizer alloc] initWithTarget:view action:@selector(mwinScroll:)];
    pan.allowedScrollTypesMask = UIScrollTypeMaskAll;
    pan.allowedTouchTypes = @[];
    pan.cancelsTouchesInView = NO;
    [view addGestureRecognizer:pan];
    [pan release];
    return view;
}

UIViewController* mwinIOSCreateController(mwinIOSPlatform* platform, uint32_t slot, UIView* view)
{
    MwinIOSController* controller = [[MwinIOSController alloc] init];
    controller->platform = platform;
    controller->slot = slot;
    controller.view = view;
    return controller;
}

mwinOutcome mwinIOSSetAccessibilityRoot(mwinIOSPlatform* platform, uint32_t slot, id root)
{
    MwinIOSView* view = (MwinIOSView*)platform->windows[slot].view;
    [root retain];
    [view->accessibilityRoot release];
    view->accessibilityRoot = root;
    // Clients read the view's elements again.
    UIAccessibilityPostNotification(UIAccessibilityLayoutChangedNotification, nil);
    return mwin_outcomeDone;
}

void mwinIOSForgetView(UIView* view)
{
    [view resignFirstResponder];
    ((MwinIOSView*)view)->platform = nullptr;
    mwinIOSForgetDrops(((MwinIOSView*)view)->dropper);
    ((MwinIOSController*)view.nextResponder)->platform = nullptr;
}
