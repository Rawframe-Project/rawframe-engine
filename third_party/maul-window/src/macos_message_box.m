// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The message box on macOS: an NSAlert run modally, which needs no
// window and may come before any context. The title is the alert's
// headline and the message its text below; a box with only a message
// has it as the headline. Its buttons are English, since AppKit names
// only its own OK.

#include "message_box.h"

#import <AppKit/AppKit.h>

static NSString* StringOf(const char* text, size_t length)
{
    return [[[NSString alloc] initWithBytes:text length:length
                                   encoding:NSUTF8StringEncoding] autorelease];
}

mwinResult mwinPlatformMessageBox(const mwinMessageBoxDef* def, bool* acceptedOut)
{
    static const NSAlertStyle styles[] = {NSAlertStyleInformational, NSAlertStyleWarning,
                                          NSAlertStyleCritical};
    static NSString* const buttons[][2] = {{@"OK", nil}, {@"OK", @"Cancel"}, {@"Yes", @"No"}};
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        // A program without a bundle may show no window until it is a
        // regular application.
        if (NSApp.activationPolicy == NSApplicationActivationPolicyProhibited)
        {
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        }
        NSAlert* alert = [[[NSAlert alloc] init] autorelease];
        alert.alertStyle = styles[def->kind];
        NSString* title = StringOf(def->title, def->titleLength);
        NSString* message = StringOf(def->message, def->messageLength);
        alert.messageText = title.length > 0 ? title : message;
        alert.informativeText = title.length > 0 ? message : @"";
        for (int i = 0; i < 2 && buttons[def->buttons][i] != nil; i++)
        {
            [alert addButtonWithTitle:buttons[def->buttons][i]];
        }
        // Escape answers the second button.
        if (alert.buttons.count > 1)
        {
            alert.buttons[1].keyEquivalent = @"\033";
        }
        if (@available(macOS 14.0, *))
        {
            [NSApp activate];
        }
        *acceptedOut = [alert runModal] == NSAlertFirstButtonReturn;
    }
    return mwin_success;
}
