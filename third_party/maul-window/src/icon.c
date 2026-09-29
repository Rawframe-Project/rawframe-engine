// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Window icons.

#include "icon.h"

#include "allocator.h"

#include <string.h>

static bool IsImage(const mwinIconImage* image)
{
    return image->pixels != nullptr && image->width > 0 && image->height > 0 &&
           image->width <= MWIN_ICON_SIZE && image->height <= MWIN_ICON_SIZE &&
           image->stride >= (size_t)image->width * 4;
}

static mwinIconCopy* Copy(const mwinContext* context, const mwinIconImage* images, uint32_t count)
{
    size_t size = sizeof(mwinIconCopy) + count * sizeof(mwinIconCopyImage);
    for (uint32_t i = 0; i < count; i++)
    {
        size += (size_t)images[i].width * images[i].height * 4;
    }
    mwinIconCopy* icon = mwinAllocate(&context->allocator, size, alignof(max_align_t));
    if (icon == nullptr)
    {
        return nullptr;
    }
    icon->size = size;
    icon->count = count;
    uint8_t* at = (uint8_t*)(icon->images + count);
    for (uint32_t i = 0; i < count; i++)
    {
        const mwinIconImage* image = &images[i];
        size_t row = (size_t)image->width * 4;
        for (uint32_t y = 0; y < image->height; y++)
        {
            memcpy(at + y * row, image->pixels + y * image->stride, row);
        }
        icon->images[i] = (mwinIconCopyImage){image->width, image->height, at};
        at += row * image->height;
    }
    return icon;
}

mwinResult mwinRequestIcon(mwinContext* context, mwinWindowId window, const mwinIconImage* images,
                           uint32_t count, mwinRequestId* requestOut)
{
    bool valid =
        context != nullptr && count <= MWIN_ICON_IMAGES && (images != nullptr || count == 0);
    for (uint32_t i = 0; valid && i < count; i++)
    {
        valid = IsImage(&images[i]);
    }
    if (!valid)
    {
        return mwin_errorInvalid;
    }
    mwinIconCopy* icon = Copy(context, images, count);
    if (icon == nullptr)
    {
        return mwin_errorCapacity;
    }
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, mwin_requestIcon, &slot, &request);
    if (status != mwin_success)
    {
        mwinRelease(&context->allocator, icon, icon->size, alignof(max_align_t));
        return status;
    }
    context->windows[slot].requests[request].value.icon = icon;
    mwinSubmitRequest(context, slot, request, requestOut);
    return mwin_success;
}

void mwinReleaseIconCopy(const mwinContext* context, mwinRequest* request)
{
    if (request->kind == mwin_requestIcon && request->value.icon != nullptr)
    {
        mwinIconCopy* icon = request->value.icon;
        mwinRelease(&context->allocator, icon, icon->size, alignof(max_align_t));
        request->value.icon = nullptr;
    }
}

const mwinIconCopyImage* mwinIconFor(const mwinIconCopy* icon, uint32_t size)
{
    const mwinIconCopyImage* best = nullptr;
    for (uint32_t i = 0; i < icon->count; i++)
    {
        const mwinIconCopyImage* image = &icon->images[i];
        uint32_t side = image->width > image->height ? image->width : image->height;
        uint32_t bestSide = best == nullptr              ? 0
                            : best->width > best->height ? best->width
                                                         : best->height;
        bool fits = side >= size;
        bool bestFits = bestSide >= size;
        if (best == nullptr || (fits && (!bestFits || side < bestSide)) ||
            (!fits && !bestFits && side > bestSide))
        {
            best = image;
        }
    }
    return best;
}
