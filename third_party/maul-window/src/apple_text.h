// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text helpers the macOS and iOS backends share: another program's text
// taken by its UTF-16 units, and a string's UTF-8 whose UTF-16 indexes,
// as compositions report their carets and clauses, mwinBytesBefore
// finds. Included by Objective-C files only.

#ifndef MAUL_WINDOW_SRC_APPLE_TEXT_H
#define MAUL_WINDOW_SRC_APPLE_TEXT_H

#include "core.h"

#import <Foundation/Foundation.h>
#include <stdint.h>

// Another program's text as the clipboard's (mwinTakeClipboardUtf16) or
// the drop's (mwinSetDroppedTextUtf16), every unit of it: U+0000 kept,
// a lone surrogate as U+FFFD. UTF8String would end the text at U+0000,
// and gives none for a lone surrogate.
mwinOutcome mwinAppleTakeClipboardText(mwinContext* context, NSString* text);
void mwinAppleSetDroppedText(mwinContext* context, NSString* text);

// A string as UTF-8, every unit of it, a lone surrogate as U+FFFD, one
// UTF-16 unit as mwinBytesBefore counts it: *lengthOut bytes in a block
// of the context's of *sizeOut bytes, which the caller lets go. NULL for
// an empty string, or with no memory.
char* mwinAppleUtf8Of(mwinContext* context, NSString* string, size_t* lengthOut, size_t* sizeOut);

#endif // MAUL_WINDOW_SRC_APPLE_TEXT_H
