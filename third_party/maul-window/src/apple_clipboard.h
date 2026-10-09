// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The clipboard's types on Apple's systems (apple_clipboard.m), which
// the macOS and iOS backends share: a MIME type's pasteboard type, as
// UTType names it (mwin-0029). Included by Objective-C files only.

#ifndef MAUL_WINDOW_SRC_APPLE_CLIPBOARD_H
#define MAUL_WINDOW_SRC_APPLE_CLIPBOARD_H

#import <Foundation/Foundation.h>

// The pasteboard type of a MIME type, autoreleased: the system's own,
// or a dynamic one for a MIME type it does not know, which another
// program derives alike; nil for none.
NSString* mwinAppleClipboardType(const char* mime, size_t length);

#endif // MAUL_WINDOW_SRC_APPLE_CLIPBOARD_H
