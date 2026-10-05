// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The message box on iOS: a UIAlertController presented over the
// topmost view controller of a scene in the foreground, or over a window
// of its own on a scene that shows none, and waited for by running the
// main run loop, as AppKit's modal alerts do; the program's frames are
// not run inside it. It needs a scene in the foreground, so it fails
// before the application shows one (in the program's init, while the
// first scene connects). The title is the alert's title and the message
// its message; its buttons are English, as on macOS. An alert taken away
// without a button (by the system, say) reads as closed.

#include "message_box.h"

#import <UIKit/UIKit.h>

static NSString* StringOf(const char* text, size_t length)
{
    return [[[NSString alloc] initWithBytes:text length:length
                                   encoding:NSUTF8StringEncoding] autorelease];
}

// A scene in the foreground, or nil.
static UIWindowScene* ShownScene(void)
{
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
    {
        bool shown = scene.activationState == UISceneActivationStateForegroundActive ||
                     scene.activationState == UISceneActivationStateForegroundInactive;
        if (shown && [scene isKindOfClass:[UIWindowScene class]])
        {
            return (UIWindowScene*)scene;
        }
    }
    return nil;
}

// The view controller everything else shows under, in a window that
// shows.
static UIViewController* TopOf(UIWindowScene* scene)
{
    for (UIWindow* window in scene.windows)
    {
        UIViewController* top = window.hidden ? nil : window.rootViewController;
        while (top.presentedViewController != nil)
        {
            top = top.presentedViewController;
        }
        if (top != nil)
        {
            return top;
        }
    }
    return nil;
}

mwinResult mwinPlatformMessageBox(const mwinMessageBoxDef* def, bool* acceptedOut)
{
    static NSString* const buttons[][2] = {{@"OK", nil}, {@"OK", @"Cancel"}, {@"Yes", @"No"}};
    @autoreleasepool
    {
        UIWindowScene* scene = UIApplication.sharedApplication != nil ? ShownScene() : nil;
        if (scene == nil)
        {
            return mwin_errorPlatform;
        }
        UIViewController* top = TopOf(scene);
        UIWindow* own = nil;
        if (top == nil)
        {
            own = [[[UIWindow alloc] initWithWindowScene:scene] autorelease];
            own.rootViewController = [[[UIViewController alloc] init] autorelease];
            own.windowLevel = UIWindowLevelAlert;
            [own makeKeyAndVisible];
            top = own.rootViewController;
        }
        NSString* title = StringOf(def->title, def->titleLength);
        NSString* message = StringOf(def->message, def->messageLength);
        UIAlertController* alert =
            [UIAlertController alertControllerWithTitle:title.length > 0 ? title : nil
                                                message:message.length > 0 ? message : nil
                                         preferredStyle:UIAlertControllerStyleAlert];
        __block int answer = -1;
        for (int i = 0; i < 2 && buttons[def->buttons][i] != nil; i++)
        {
            bool second = i == 1;
            [alert addAction:[UIAlertAction actionWithTitle:buttons[def->buttons][i]
                                                      style:second ? UIAlertActionStyleCancel
                                                                   : UIAlertActionStyleDefault
                                                    handler:^(UIAlertAction* action) {
                                                      (void)action;
                                                      answer = second ? 0 : 1;
                                                    }]];
        }
        [top presentViewController:alert animated:YES completion:nil];
        bool presented = false;
        NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:5.0];
        while (answer < 0)
        {
            // Never shown (another alert in the way, say): it failed.
            if (!presented && alert.presentingViewController == nil &&
                deadline.timeIntervalSinceNow < 0.0)
            {
                own.hidden = YES;
                return mwin_errorPlatform;
            }
            [[NSRunLoop mainRunLoop] runMode:NSDefaultRunLoopMode
                                  beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
            // Taken away without a button: closed.
            presented = presented || alert.presentingViewController != nil;
            if (presented && alert.presentingViewController == nil)
            {
                answer = 0;
            }
        }
        own.hidden = YES;
        *acceptedOut = answer == 1;
    }
    return mwin_success;
}
