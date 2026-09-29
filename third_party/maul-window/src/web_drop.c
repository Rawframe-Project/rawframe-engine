// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Web drag and drop.

#include "web_drop.h"

#include "allocator.h"

#include <emscripten/em_js.h>

EM_JS_DEPS(mwin_web_drop, "$stringToUTF8,$lengthBytesUTF8");

// clang-format off
EM_JS(void, WatchDrops, (const mwinContext* context, uint32_t slot), {
    const state = Module.mwinWeb.get(context);
    const entry = state.canvases[slot];
    const canvas = entry.canvas;
    state.drops = state.drops || [];
    const where = e => {
        const box = canvas.getBoundingClientRect();
        return [e.clientX - box.left - canvas.clientLeft, e.clientY - box.top - canvas.clientTop];
    };
    // What a drag carries, as mwinDragContents.
    const contents = e => {
        const types = e.dataTransfer ? Array.from(e.dataTransfer.types) : [];
        return (types.includes('Files') ? 1 : 0) | (types.includes('text/plain') ? 2 : 0);
    };
    let last = null;
    const drag = (e, phase) => {
        const carried = contents(e);
        if (carried === 0) {
            return;
        }
        e.preventDefault();
        e.dataTransfer.dropEffect = 'copy';
        const [x, y] = where(e);
        const same = phase === 1 && last !== null && last[0] === x && last[1] === y;
        last = phase === 2 ? null : [x, y];
        if (!same) {
            state.push(26, slot, phase, x, y, carried);
        }
    };
    const drop = e => {
        if (contents(e) === 0) {
            return;
        }
        e.preventDefault();
        last = null;
        const [x, y] = where(e);
        const transfer = e.dataTransfer;
        const text = transfer.types.includes('text/plain') ? transfer.getData('text/plain') : null;
        state.drops.push({names: Array.from(transfer.files || []).map(file => file.name),
                          bytes: text === null ? null : new TextEncoder().encode(text)});
        state.push(27, slot, 0, x, y);
    };
    const listen = (type, handler) => {
        canvas.addEventListener(type, handler);
        entry.listeners.push(() => canvas.removeEventListener(type, handler));
    };
    listen('dragenter', e => drag(e, 0));
    listen('dragover', e => drag(e, 1));
    listen('dragleave', e => drag(e, 2));
    listen('drop', drop);
});

EM_JS(uint32_t, NameCount, (const mwinContext* context), {
    return Module.mwinWeb.get(context).drops[0].names.length;
});

// A file's name into out: its length, or -1 when it does not fit.
EM_JS(int, Name, (const mwinContext* context, uint32_t index, char* out, uint32_t capacity), {
    const name = Module.mwinWeb.get(context).drops[0].names[index];
    if (lengthBytesUTF8(name) >= capacity) {
        return -1;
    }
    return stringToUTF8(name, out, capacity);
});

// The text's bytes, or -1 for none.
EM_JS(int, TextLength, (const mwinContext* context), {
    const bytes = Module.mwinWeb.get(context).drops[0].bytes;
    return bytes === null ? -1 : bytes.length;
});

// Takes the drop off the page, its text into out unless it is NULL.
EM_JS(void, TakeDrop, (const mwinContext* context, char* out), {
    const drop = Module.mwinWeb.get(context).drops.shift();
    if (out && drop.bytes) {
        HEAPU8.set(drop.bytes, out);
    }
});
// clang-format on

void mwinWebWatchDrops(const mwinContext* context, uint32_t slot)
{
    WatchDrops(context, slot);
}

// Gathers the drop the page took, into the context's drop.
static void Gather(mwinWebPlatform* platform)
{
    mwinContext* context = platform->context;
    uint32_t capacity = context->limits.textBytesPerWindow + 1u;
    mwinBeginDrop(context);
    uint32_t count = NameCount(context);
    for (uint32_t i = 0; i < count; i++)
    {
        int length = Name(context, i, platform->text, capacity);
        if (length < 0)
        {
            context->dropping.truncated = true;
            continue;
        }
        mwinAddDroppedFile(context, platform->text, (size_t)length);
    }
    int length = TextLength(context);
    char* bytes = length > 0 && (uint32_t)length <= context->limits.dropBytes
                      ? mwinAllocate(&context->allocator, (size_t)length, 1)
                      : nullptr;
    TakeDrop(context, bytes);
    if (length >= 0 && (length == 0 || bytes != nullptr))
    {
        mwinSetDroppedText(context, bytes, (size_t)length);
    }
    else if (length > 0)
    {
        context->dropping.truncated = true;
    }
    if (bytes != nullptr)
    {
        mwinRelease(&context->allocator, bytes, (size_t)length, 1);
    }
}

void mwinWebHandleDropRecord(mwinWebPlatform* platform, const mwinWebRecord* record)
{
    mwinContext* context = platform->context;
    uint32_t slot = (uint32_t)record->slot;
    bool live = context->windows[slot].status == mwin_slotLive;
    uint64_t timeNs = mwinWebNanoseconds(record->timeMs);
    mwinPosition position = {record->x, record->y};
    if (record->kind == mwin_webDropped)
    {
        Gather(platform);
        if (live)
        {
            mwinFinishDrop(context, slot, position, timeNs);
        }
        return;
    }
    static const mwinEventType types[] = {mwin_eventDragEntered, mwin_eventDragMoved,
                                          mwin_eventDragLeft};
    if (live)
    {
        mwinEvent event = {.type = types[record->code], .timeNs = timeNs};
        event.data.drag = (mwinDragEvent){position, (mwinDragContents)record->z};
        mwinPost(context, slot, &event);
    }
}
