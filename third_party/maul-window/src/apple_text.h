// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text helpers the macOS and iOS backends share: where a UTF-16 index
// of an NSString falls in its UTF-8 bytes, as compositions report
// their carets and clauses. Included by Objective-C files only.

#ifndef MAUL_WINDOW_SRC_APPLE_TEXT_H
#define MAUL_WINDOW_SRC_APPLE_TEXT_H

#import <Foundation/Foundation.h>
#include <stdint.h>

// The UTF-8 bytes of the first units of a UTF-16 string, a lone
// surrogate as the three of U+FFFD.
uint32_t mwinAppleBytesBefore(NSString* string, NSUInteger index);

#endif // MAUL_WINDOW_SRC_APPLE_TEXT_H
