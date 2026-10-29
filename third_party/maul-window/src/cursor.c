// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Cursors made from images.

#include "cursor.h"

#include "allocator.h"

#define CURSOR_DEF_COOKIE 0x6D776375u // "mwcu"

mwinCursorDef mwinDefaultCursorDef(void)
{
    return (mwinCursorDef){.cookie = CURSOR_DEF_COOKIE};
}

// A def's status: its images cursor images, each wider than the one
// before, with the hotspot on the first; mwin_errorLimit for more images
// or a larger side than the named bounds.
static mwinResult CheckDef(const mwinCursorDef* def)
{
    if (def->cookie != CURSOR_DEF_COOKIE || def->images == nullptr || def->imageCount == 0)
    {
        return mwin_errorInvalid;
    }
    if (def->imageCount > MWIN_MAX_CURSOR_IMAGES)
    {
        return mwin_errorLimit;
    }
    for (uint32_t i = 0; i < def->imageCount; i++)
    {
        mwinResult status = mwinCheckIconImage(&def->images[i], MWIN_MAX_CURSOR_SIZE);
        if (status != mwin_success)
        {
            return status;
        }
        if (i > 0 && def->images[i].width <= def->images[i - 1].width)
        {
            return mwin_errorInvalid;
        }
    }
    bool onFirst = def->hotspotX < def->images[0].width && def->hotspotY < def->images[0].height;
    return onFirst ? mwin_success : mwin_errorInvalid;
}

mwinResult mwinCreateCursor(mwinContext* context, const mwinCursorDef* def, mwinCursorId* cursorOut)
{
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    if (context == nullptr || def == nullptr || cursorOut == nullptr)
    {
        return mwinMisuse(context);
    }
    mwinResult status = CheckDef(def);
    if (status != mwin_success)
    {
        return mwinRefuse(context, status);
    }
    uint32_t slot = 0;
    while (slot < context->limits.cursors && context->cursors[slot].images != nullptr)
    {
        slot++;
    }
    if (slot == context->limits.cursors)
    {
        return mwin_errorCapacity;
    }
    mwinIconCopy* images = mwinCopyIconImages(context, def->images, def->imageCount);
    if (images == nullptr)
    {
        return mwin_errorCapacity;
    }
    mwinCursor* cursor = &context->cursors[slot];
    uint32_t generation = cursor->generation + 1 == 0 ? 1 : cursor->generation + 1;
    *cursor = (mwinCursor){
        .generation = generation,
        .images = images,
        .hotspotX = def->hotspotX,
        .hotspotY = def->hotspotY,
    };
    *cursorOut = (mwinCursorId){slot + 1, generation};
    return mwin_success;
}

mwinCursor* mwinFindCursor(const mwinContext* context, mwinCursorId cursor)
{
    if (cursor.index1 == 0 || cursor.index1 > context->limits.cursors)
    {
        return nullptr;
    }
    mwinCursor* found = &context->cursors[cursor.index1 - 1];
    return found->images != nullptr && found->generation == cursor.generation ? found : nullptr;
}

// Ends the cursor in a slot: the backend's objects, then the images.
static void Release(mwinContext* context, uint32_t slot)
{
    if (context->backend->releaseCursor != nullptr)
    {
        context->backend->releaseCursor(context, slot);
    }
    mwinCursor* cursor = &context->cursors[slot];
    mwinRelease(&context->allocator, cursor->images, cursor->images->size, alignof(max_align_t));
    *cursor = (mwinCursor){.generation = cursor->generation};
}

mwinResult mwinDestroyCursor(mwinContext* context, mwinCursorId cursor)
{
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    if (context == nullptr)
    {
        return mwin_errorInvalid;
    }
    if (mwinFindCursor(context, cursor) == nullptr)
    {
        return mwin_errorStale;
    }
    Release(context, cursor.index1 - 1);
    return mwin_success;
}

void mwinReleaseCursors(mwinContext* context)
{
    for (uint32_t slot = 0; slot < context->limits.cursors; slot++)
    {
        if (context->cursors[slot].images != nullptr)
        {
            Release(context, slot);
        }
    }
}

mwinResult mwinRequestCursorImage(mwinContext* context, mwinWindowId window, mwinCursorId cursor,
                                  mwinRequestId* requestOut)
{
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    if (context == nullptr)
    {
        return mwin_errorInvalid;
    }
    if (mwinFindCursor(context, cursor) == nullptr)
    {
        return mwin_errorStale;
    }
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, mwin_requestCursorImage, &slot, &request);
    if (status == mwin_success)
    {
        context->windows[slot].requests[request].value.cursor = cursor;
        mwinSubmitRequest(context, slot, request, requestOut);
    }
    return status;
}

uint32_t mwinCursorImageFor(const mwinCursor* cursor, float scale)
{
    const mwinIconCopy* images = cursor->images;
    float wanted = (float)images->images[0].width * (scale > 1.0f ? scale : 1.0f);
    for (uint32_t i = 0; i < images->count; i++)
    {
        if ((float)images->images[i].width >= wanted)
        {
            return i;
        }
    }
    return images->count - 1;
}

void mwinCursorHotspotOf(const mwinCursor* cursor, uint32_t image, uint32_t* xOut, uint32_t* yOut)
{
    const mwinIconCopyImage* first = &cursor->images->images[0];
    const mwinIconCopyImage* chosen = &cursor->images->images[image];
    // Scaled to the image, rounded down, and kept on it.
    uint32_t x = (uint32_t)((uint64_t)cursor->hotspotX * chosen->width / first->width);
    uint32_t y = (uint32_t)((uint64_t)cursor->hotspotY * chosen->height / first->height);
    *xOut = x < chosen->width ? x : chosen->width - 1;
    *yOut = y < chosen->height ? y : chosen->height - 1;
}
