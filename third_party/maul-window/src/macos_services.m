// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The macOS clipboard and services (macos.h): text, and data by MIME
// type as UTType names it (mwin-0029), on the general pasteboard,
// written and read at once while the request is submitted;
// addresses opened by NSWorkspace in the user's default program; files
// shown by the Finder, selected; and the display kept awake by a power
// assertion while a window that asks for it shows.

#include "apple_clipboard.h"
#include "clipboard_data.h"
#include "macos.h"

#import <IOKit/pwr_mgt/IOPMLib.h>
#include <string.h>

mwinOutcome mwinMacWriteClipboard(const mwinMacPlatform* platform)
{
    const mwinContext* context = platform->context;
    const mwinClipboardCopy* copy = context->clipboardData;
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    [pasteboard clearContents];
    bool set = true;
    if (mwinOffersClipboardText(context))
    {
        NSString* text = [[NSString alloc] initWithBytes:context->clipboardOffer
                                                  length:context->clipboardOfferLength
                                                encoding:NSUTF8StringEncoding];
        set = text != nil && [pasteboard setString:text forType:NSPasteboardTypeString];
        [text release];
    }
    for (uint32_t i = 0; set && copy != nullptr && i < copy->count; i++)
    {
        const mwinClipboardDataItem* item = &copy->items[i];
        NSString* type = mwinAppleClipboardType(item->mime, item->mimeLength);
        NSData* data = [NSData dataWithBytes:mwinClipboardBytesOf(copy, item) length:item->length];
        set = type != nil && [pasteboard setData:data forType:type];
    }
    return set ? mwin_outcomeDone : mwin_outcomeFailed;
}

mwinOutcome mwinMacReadClipboardData(mwinMacPlatform* platform, const mwinRequest* request)
{
    NSString* type = mwinAppleClipboardType(request->value.text.bytes, request->value.text.length);
    NSData* data = type != nil ? [[NSPasteboard generalPasteboard] dataForType:type] : nil;
    return data != nil ? mwinTakeClipboardData(platform->context, data.bytes, data.length)
                       : mwin_outcomeFailed;
}

mwinOutcome mwinMacReadClipboard(mwinMacPlatform* platform)
{
    mwinContext* context = platform->context;
    NSString* text = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    // No text, as a program that put only an image leaves it.
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

static NSString* StringOf(const mwinRequest* request)
{
    return [[[NSString alloc] initWithBytes:request->value.text.bytes
                                     length:request->value.text.length
                                   encoding:NSUTF8StringEncoding] autorelease];
}

mwinOutcome mwinMacOpenUrl(const mwinRequest* request)
{
    NSString* text = StringOf(request);
    NSURL* address = text != nil ? [NSURL URLWithString:text] : nil;
    return address != nil && [[NSWorkspace sharedWorkspace] openURL:address] ? mwin_outcomeDone
                                                                             : mwin_outcomeFailed;
}

mwinOutcome mwinMacRevealFile(const mwinRequest* request)
{
    NSString* path = StringOf(request);
    if (path == nil || ![[NSFileManager defaultManager] fileExistsAtPath:path])
    {
        return mwin_outcomeFailed;
    }
    [[NSWorkspace sharedWorkspace]
        activateFileViewerSelectingURLs:@[ [NSURL fileURLWithPath:path] ]];
    return mwin_outcomeDone;
}

void mwinMacKeepAwake(mwinMacPlatform* platform, bool wanted)
{
    if (wanted == platform->awake)
    {
        return;
    }
    if (wanted)
    {
        IOPMAssertionID assertion = kIOPMNullAssertionID;
        platform->awake = IOPMAssertionCreateWithName(
                              kIOPMAssertPreventUserIdleDisplaySleep, kIOPMAssertionLevelOn,
                              CFSTR("Maul Window keeps awake"), &assertion) == kIOReturnSuccess;
        platform->awakeAssertion = assertion;
        return;
    }
    (void)IOPMAssertionRelease(platform->awakeAssertion);
    platform->awakeAssertion = kIOPMNullAssertionID;
    platform->awake = false;
}
