// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The iOS clipboard and services (ios.h): text on the general
// pasteboard, written and read at once while the request is submitted
// (reading what another application put there, iOS may ask the user
// first, and the answer waits for them; a refusal reads as no text);
// addresses opened by UIKit, answered when it says whether it could;
// and the display kept awake by turning the idle timer off while a
// window that asks for it shows. There is no file manager to show a
// file in.

#include "answer.h"
#include "ios.h"

#include <string.h>

mwinOutcome mwinIOSWriteClipboard(const mwinIOSPlatform* platform)
{
    const mwinContext* context = platform->context;
    NSString* text = [[NSString alloc] initWithBytes:context->clipboardOffer
                                              length:context->clipboardOfferLength
                                            encoding:NSUTF8StringEncoding];
    if (text == nil)
    {
        return mwin_outcomeFailed;
    }
    UIPasteboard.generalPasteboard.string = text;
    [text release];
    return mwin_outcomeDone;
}

mwinOutcome mwinIOSReadClipboard(mwinIOSPlatform* platform)
{
    mwinContext* context = platform->context;
    UIPasteboard* pasteboard = UIPasteboard.generalPasteboard;
    // Only text is asked for: asking whether there is any asks nobody.
    NSString* text = pasteboard.hasStrings ? pasteboard.string : nil;
    if (text == nil)
    {
        return mwinTakeClipboardText(context, nullptr, 0);
    }
    // Each unit is at least a byte of UTF-8: text past the limit is not
    // converted.
    if (text.length > context->limits.clipboardBytes)
    {
        return mwin_outcomeTooLarge;
    }
    const char* bytes = text.UTF8String;
    return bytes != nullptr ? mwinTakeClipboardText(context, bytes, strlen(bytes))
                            : mwin_outcomeFailed;
}

int mwinIOSOpenUrl(mwinIOSPlatform* platform, uint32_t slot, uint32_t request)
{
    mwinContext* context = platform->context;
    const mwinRequest* asked = &context->windows[slot].requests[request];
    NSString* text = [[[NSString alloc] initWithBytes:asked->value.text.bytes
                                               length:asked->value.text.length
                                             encoding:NSUTF8StringEncoding] autorelease];
    NSURL* address = text != nil ? [NSURL URLWithString:text] : nil;
    if (address == nil)
    {
        return mwin_outcomeFailed;
    }
    __block mwinServiceAnswer answer = mwinAnswerTo(context, slot, request);
    [UIApplication.sharedApplication openURL:address
        options:@{}
        completionHandler:^(BOOL opened) {
          // The program may have ended since.
          if (mwinIOSCurrentContext() == context)
          {
              mwinAnswer(context, &answer, opened ? mwin_outcomeDone : mwin_outcomeFailed);
          }
        }];
    return -1;
}

void mwinIOSKeepAwake(mwinIOSPlatform* platform, bool wanted)
{
    if (wanted == platform->awake)
    {
        return;
    }
    platform->awake = wanted;
    UIApplication.sharedApplication.idleTimerDisabled = wanted;
}
