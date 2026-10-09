// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The clipboard's types on Apple's systems.

#include "apple_clipboard.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

NSString* mwinAppleClipboardType(const char* mime, size_t length)
{
    NSString* name = [[[NSString alloc] initWithBytes:mime
                                               length:length
                                             encoding:NSASCIIStringEncoding] autorelease];
    return name != nil ? [UTType typeWithMIMEType:name].identifier : nil;
}
