// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// X11 window icons.

#include "x11_icon.h"

#include "allocator.h"
#include "icon.h"

mwinOutcome mwinX11SetIcon(mwinX11Platform* platform, xcb_window_t window,
                           const mwinRequest* request)
{
    const mwinX11Api* api = &platform->api;
    const mwinIconCopy* icon = request->value.icon;
    xcb_atom_t property = platform->atoms[mwin_atomNetWmIcon];
    if (icon->count == 0)
    {
        api->deleteProperty(platform->connection, window, property);
        return mwin_outcomeDone;
    }
    size_t count = 0;
    for (uint32_t i = 0; i < icon->count; i++)
    {
        count += 2 + (size_t)icon->images[i].width * icon->images[i].height;
    }
    // The request's length, in 4-byte units, with its header of six.
    if (count + 6 > api->maximumRequestLength(platform->connection))
    {
        return mwin_outcomeTooLarge;
    }
    const mwinAllocator* allocator = &platform->context->allocator;
    uint32_t* data = mwinAllocate(allocator, count * sizeof(uint32_t), alignof(uint32_t));
    if (data == nullptr)
    {
        return mwin_outcomeFailed;
    }
    uint32_t* at = data;
    for (uint32_t i = 0; i < icon->count; i++)
    {
        const mwinIconCopyImage* image = &icon->images[i];
        *at++ = image->width;
        *at++ = image->height;
        size_t pixels = (size_t)image->width * image->height;
        for (size_t p = 0; p < pixels; p++)
        {
            const uint8_t* rgba = image->pixels + p * 4;
            *at++ = (uint32_t)rgba[3] << 24 | (uint32_t)rgba[0] << 16 | (uint32_t)rgba[1] << 8 |
                    rgba[2];
        }
    }
    api->changeProperty(platform->connection, XCB_PROP_MODE_REPLACE, window, property,
                        XCB_ATOM_CARDINAL, 32, (uint32_t)count, data);
    mwinRelease(allocator, data, count * sizeof(uint32_t), alignof(uint32_t));
    return mwin_outcomeDone;
}
