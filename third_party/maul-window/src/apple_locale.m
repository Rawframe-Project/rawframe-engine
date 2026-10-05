// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The preferred languages on Apple's systems (apple_locale.h).

#include "apple_locale.h"

#import <Foundation/Foundation.h>
#include <string.h>

void mwinAppleReadLocales(mwinContext* context, uint64_t nowNs)
{
    NSMutableArray* tags = [[NSLocale.preferredLanguages mutableCopy] autorelease];
    const char* list = "";
    while (true)
    {
        list = [tags componentsJoinedByString:@","].UTF8String;
        if (list == nullptr || strlen(list) <= context->limits.localeBytes || tags.count == 0)
        {
            break;
        }
        [tags removeLastObject];
    }
    (void)mwinSetLocales(context, list != nullptr ? list : "", list != nullptr ? strlen(list) : 0,
                         nowNs);
}
