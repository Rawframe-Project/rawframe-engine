// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Cursors over canvases.

#include "web_cursor.h"

#include "cursor.h"
#include "icon.h"
#include "web_js.h"

// clang-format off
EM_JS(void, mwinPageSetCursor, (const mwinContext* context, uint32_t slot, int shape, bool hidden,
                                uint32_t image), {
    const state = Module.mwinWeb.get(context);
    const shapes = ['default', 'text', 'pointer', 'crosshair', 'move', 'ew-resize', 'ns-resize',
                    'nesw-resize', 'nwse-resize', 'not-allowed', 'wait', 'progress'];
    state.canvases[slot].canvas.style.cursor =
        hidden ? 'none' : image !== 0 ? state.cursors.get(image) : shapes[shape];
});

// The cursor property's value for images (pixels, width and height
// each) and a hotspot, kept by the page: its handle.
EM_JS(uint32_t, mwinPageMakeCursor, (const mwinContext* context, const uint32_t* images,
                                     uint32_t count, uint32_t x, uint32_t y), {
    const state = Module.mwinWeb.get(context);
    const at = images >> 2;
    const first = HEAPU32[at + 1];
    const urls = [];
    for (let i = 0; i < count; i++) {
        const pixels = HEAPU32[at + i * 3];
        const width = HEAPU32[at + i * 3 + 1];
        const height = HEAPU32[at + i * 3 + 2];
        const canvas = document.createElement('canvas');
        canvas.width = width;
        canvas.height = height;
        // ImageData is RGBA with straight alpha, as the images are.
        const rgba = new Uint8ClampedArray(HEAPU8.buffer, pixels, width * height * 4).slice();
        canvas.getContext('2d').putImageData(new ImageData(rgba, width, height), 0, 0);
        urls.push('url("' + canvas.toDataURL('image/png') + '") ' + (width / first) + 'x');
    }
    const hotspot = ' ' + x + ' ' + y + ', auto';
    // A browser that takes no image-set() in a cursor leaves the probe's
    // value empty.
    const probe = document.createElement('div').style;
    probe.cursor = 'image-set(' + urls.join(', ') + ')' + hotspot;
    const value = probe.cursor || urls[0].replace(/ [0-9.]+x$/, "") + hotspot;
    state.cursors = state.cursors || new Map();
    state.cursorCount = (state.cursorCount || 0) + 1;
    state.cursors.set(state.cursorCount, value);
    return state.cursorCount;
});

EM_JS(void, mwinPageDropCursor, (const mwinContext* context, uint32_t image), {
    Module.mwinWeb.get(context).cursors.delete(image);
});
// clang-format on

// The page's handle of a cursor's value, made the first time; 0 when
// the window shows a shape.
static uint32_t ImageOf(mwinWebPlatform* platform, mwinCursorId id)
{
    mwinCursor* cursor = mwinFindCursor(platform->context, id);
    if (cursor == nullptr)
    {
        return 0;
    }
    if (cursor->nativeId[0] == 0)
    {
        const mwinIconCopy* copy = cursor->images;
        uint32_t images[MWIN_CURSOR_IMAGES * 3];
        for (uint32_t i = 0; i < copy->count; i++)
        {
            images[i * 3] = (uint32_t)(uintptr_t)copy->images[i].pixels;
            images[i * 3 + 1] = copy->images[i].width;
            images[i * 3 + 2] = copy->images[i].height;
        }
        cursor->nativeId[0] = mwinPageMakeCursor(platform->context, images, copy->count,
                                                 cursor->hotspotX, cursor->hotspotY);
    }
    return cursor->nativeId[0];
}

void mwinWebApplyCursor(mwinWebPlatform* platform, uint32_t slot)
{
    mwinWebWindow* window = &platform->windows[slot];
    mwinPageSetCursor(platform->context, slot, window->cursorShape,
                      window->cursorMode != mwin_cursorVisible,
                      ImageOf(platform, window->cursorImage));
}

int mwinWebSetCursorShape(mwinWebPlatform* platform, uint32_t slot, mwinCursorShape shape)
{
    mwinWebWindow* window = &platform->windows[slot];
    window->cursorShape = shape;
    window->cursorImage = (mwinCursorId){0};
    mwinWebApplyCursor(platform, slot);
    return mwin_outcomeDone;
}

int mwinWebSetCursorImage(mwinWebPlatform* platform, uint32_t slot, mwinCursorId cursor)
{
    platform->windows[slot].cursorImage = cursor;
    mwinWebApplyCursor(platform, slot);
    return mwin_outcomeDone;
}

void mwinWebReleaseCursor(mwinContext* context, uint32_t slot)
{
    mwinWebPlatform* platform = context->backendData;
    // Its windows show the default shape from here.
    for (uint32_t i = 0; i < context->limits.windows; i++)
    {
        mwinWebWindow* window = &platform->windows[i];
        if (window->open && window->cursorImage.index1 == slot + 1)
        {
            window->cursorShape = mwin_shapeDefault;
            window->cursorImage = (mwinCursorId){0};
            mwinWebApplyCursor(platform, i);
        }
    }
    mwinCursor* cursor = &context->cursors[slot];
    if (cursor->nativeId[0] != 0)
    {
        mwinPageDropCursor(context, cursor->nativeId[0]);
        cursor->nativeId[0] = 0;
    }
}
