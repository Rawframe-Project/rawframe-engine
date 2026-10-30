// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Window icons.

#include "icon.h"

#include "allocator.h"

#include <stdckdint.h>
#include <string.h>

mwinResult mwinCheckIconImage(const mwinIconImage* image, uint32_t most)
{
    if (image->pixels == nullptr || image->width == 0 || image->height == 0)
    {
        return mwin_errorInvalid;
    }
    if (image->width > most || image->height > most)
    {
        return mwin_errorLimit;
    }
    return image->stride >= (size_t)image->width * 4 ? mwin_success : mwin_errorInvalid;
}

mwinIconCopy* mwinCopyIconImages(const mwinContext* context, const mwinIconImage* images,
                                 uint32_t count)
{
    // The copy, its images and their pixels, in checked arithmetic.
    size_t size = 0;
    bool overflow = ckd_mul(&size, (size_t)count, sizeof(mwinIconCopyImage)) ||
                    ckd_add(&size, size, sizeof(mwinIconCopy));
    for (uint32_t i = 0; i < count && !overflow; i++)
    {
        size_t pixels = 0;
        overflow = ckd_mul(&pixels, (size_t)images[i].width * 4, images[i].height) ||
                   ckd_add(&size, size, pixels);
    }
    mwinIconCopy* icon =
        overflow ? nullptr : mwinAllocate(&context->allocator, size, alignof(max_align_t));
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

mwinResult mwinWindow_RequestIcon(mwinContext* context, mwinWindowId window,
                                  const mwinIconImage* images, uint32_t count,
                                  mwinRequestId* requestOut)
{
    mwinClearOut(requestOut, sizeof *requestOut);
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    if (context == nullptr || (images == nullptr && count != 0))
    {
        return mwinMisuse(context);
    }
    mwinResult status = count > MWIN_MAX_ICON_IMAGES ? mwin_errorLimit : mwin_success;
    for (uint32_t i = 0; status == mwin_success && i < count; i++)
    {
        status = mwinCheckIconImage(&images[i], MWIN_MAX_ICON_SIZE);
    }
    if (status != mwin_success)
    {
        return mwinRefuse(context, status);
    }
    mwinIconCopy* icon = mwinCopyIconImages(context, images, count);
    if (icon == nullptr)
    {
        return mwin_errorCapacity;
    }
    uint32_t slot = 0;
    int32_t request = 0;
    status = mwinBeginRequest(context, window, mwin_requestIcon, &slot, &request);
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
