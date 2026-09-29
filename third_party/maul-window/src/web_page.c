// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The page's side of the web backend. The state of a context is an
// object in Module.mwinWeb, a Map keyed by the context's address: its
// canvases, and a queue of records, each an array of the fields of an
// mwinWebRecord.

#include "web_page.h"

#include <emscripten/em_js.h>
#include <stddef.h>

static_assert(offsetof(mwinWebRecord, x) == 16 && offsetof(mwinWebRecord, timeMs) == 40,
              "the page writes records at these offsets");

EM_JS_DEPS(mwin_web_page, "$UTF8ToString,$stringToUTF8,$lengthBytesUTF8,$getWasmTableEntry");

// A function of this file's own, not JavaScript: the backend's call to it
// is what takes this file, and all the JavaScript in it, into a program
// linked from the static library.
bool mwinWebHasPage(void);

// clang-format off
EM_JS(bool, HasDocument, (void), {
    return typeof document !== 'undefined' && typeof window !== 'undefined';
});

EM_JS(void, mwinWebAttach, (mwinContext* context, mwinWebLifecycle lifecycle), {
    const map = Module.mwinWeb || (Module.mwinWeb = new Map());
    const state = {queue: [], strings: [], canvases: [], listeners: []};
    map.set(context, state);
    const push = (kind, slot, code = 0, x = 0, y = 0, z = 0, w = 0, v = 0, u = 0, extra = 0) =>
        state.queue.push([kind, slot, code, extra, x, y, z, w, v, u, performance.now()]);
    state.push = push;
    const listen = (target, type, handler) => {
        target.addEventListener(type, handler);
        state.listeners.push(() => target.removeEventListener(type, handler));
    };
    // The page going away or coming back: the backend answers at once,
    // before the browser goes on.
    const live = running => getWasmTableEntry(lifecycle)(context, running ? 1 : 0);
    listen(document, 'visibilitychange', () => {
        push(6, -1, document.hidden ? 0 : 1);
        live(!document.hidden);
    });
    listen(window, 'pagehide', () => live(false));
    listen(window, 'pageshow', e => e.persisted && live(true));
    listen(document, 'freeze', () => live(false));
    listen(document, 'resume', () => live(true));
    listen(document, 'fullscreenchange', () => state.canvases.forEach((entry, slot) => {
        const now = entry && document.fullscreenElement === entry.canvas;
        if (entry && now !== entry.fullscreen) {
            entry.fullscreen = now;
            push(4, slot, now ? 1 : 0);
        }
    }));
    listen(window, 'languagechange', () => push(8, -1, 0));
    for (const query of ['(prefers-color-scheme: dark)', '(prefers-reduced-motion: reduce)']) {
        listen(matchMedia(query), 'change', () => push(7, -1, 0));
    }
    // devicePixelRatio, looked at whenever it may have changed: a media
    // query for the current ratio, a resize of the page (zooming), a
    // canvas's new pixels, whichever the browser reports first, and at
    // each pump for a browser that reports none.
    state.ratio = devicePixelRatio;
    // A canvas's drawing buffer in the pixels of its CSS box; the
    // browser's count of them where it agrees with the ratio (headless
    // Chrome's does not when it emulates one).
    state.fit = (slot, width, height, device) => {
        const entry = state.canvases[slot];
        const trusted = device && Math.abs(device.inlineSize - width * devicePixelRatio) <= 1 &&
                        Math.abs(device.blockSize - height * devicePixelRatio) <= 1;
        const pixelWidth = trusted ? device.inlineSize : Math.round(width * devicePixelRatio);
        const pixelHeight = trusted ? device.blockSize : Math.round(height * devicePixelRatio);
        entry.width = width;
        entry.height = height;
        if (pixelWidth > 0 && pixelHeight > 0) {
            entry.canvas.width = pixelWidth;
            entry.canvas.height = pixelHeight;
            push(1, slot, 0, width, height, pixelWidth, pixelHeight);
        }
        entry.place();
    };
    state.checkScale = () => {
        if (devicePixelRatio !== state.ratio) {
            state.ratio = devicePixelRatio;
            push(2, -1, 0, devicePixelRatio);
            state.canvases.forEach((entry, slot) => entry && state.fit(slot, entry.width, entry.height));
        }
    };
    const watchScale = () => {
        const query = matchMedia('(resolution: ' + devicePixelRatio + 'dppx)');
        const handler = () => {
            state.checkScale();
            watchScale();
        };
        query.addEventListener('change', handler, {once: true});
        state.stopScale = () => query.removeEventListener('change', handler);
    };
    watchScale();
    listen(window, 'resize', state.checkScale);
    listen(window, 'resize', () => state.canvases.forEach(entry => entry && entry.place()));
    // A canvas taken out of the document has no surface until it is back.
    const surfaces = new MutationObserver(() => state.canvases.forEach((entry, slot) => {
        if (entry && entry.canvas.isConnected === entry.lost) {
            entry.lost = !entry.canvas.isConnected;
            push(entry.lost ? 22 : 23, slot);
        }
    }));
    surfaces.observe(document, {childList: true, subtree: true});
    state.listeners.push(() => surfaces.disconnect());
    // Stops watching a canvas; one the backend made goes, one of the
    // page's gets its style back.
    state.close = slot => {
        const entry = state.canvases[slot];
        entry.observer.disconnect();
        entry.listeners.forEach(remove => remove());
        entry.host.remove();
        if (entry.made) {
            entry.canvas.remove();
        } else {
            entry.canvas.style.cssText = entry.style;
        }
        state.canvases[slot] = null;
    };
});

EM_JS(void, mwinWebDetach, (const mwinContext* context), {
    const state = Module.mwinWeb && Module.mwinWeb.get(context);
    if (!state) {
        return;
    }
    state.canvases.forEach((entry, slot) => entry && state.close(slot));
    state.listeners.forEach(remove => remove());
    state.stopScale();
    Module.mwinWeb.delete(context);
});

EM_JS(bool, mwinWebNext, (const mwinContext* context, mwinWebRecord* out), {
    const record = Module.mwinWeb.get(context).queue.shift();
    if (!record) {
        return false;
    }
    HEAP32.set(record.slice(0, 4), out >> 2);
    HEAPF32.set(record.slice(4, 10), (out + 16) >> 2);
    HEAPF64[(out + 40) >> 3] = record[10];
    return true;
});
EM_JS(int, mwinWebOpenCanvas, (const mwinContext* context, uint32_t slot, const char* selector,
                              size_t length, float width, float height, bool visible, char* out,
                              uint32_t capacity, float* box), {
    const state = Module.mwinWeb.get(context);
    const made = length === 0;
    const canvas = made ? document.createElement('canvas')
                        : document.querySelector(UTF8ToString(selector, length));
    if (!(canvas instanceof HTMLCanvasElement)) {
        return -1;
    }
    const entry = {canvas, made, style: canvas.style.cssText, fullscreen: false, lost: false,
                   listeners: []};
    if (!canvas.id) {
        canvas.id = 'mwin-' + context + '-' + slot;
    }
    const name = '#' + CSS.escape(canvas.id);
    const suffix = '-accessibility';
    if (lengthBytesUTF8(name + suffix) + 1 > capacity) {
        return -1;
    }
    if (made) {
        canvas.style.cssText = 'display:block;outline:none;width:' + width + 'px;height:' +
                               height + 'px';
        document.body.appendChild(canvas);
    }
    // Keys come to a canvas that can take focus; touches are the
    // program's, not the page's to scroll.
    if (!canvas.hasAttribute('tabindex')) {
        canvas.tabIndex = 0;
    }
    canvas.style.touchAction = 'none';
    if (!visible) {
        canvas.style.display = 'none';
    }
    // The host of the program's accessibility elements (accessibility.h):
    // over the canvas, letting the pointer through to it, gone with it.
    // Its selector is the canvas's and the suffix.
    const host = document.createElement('div');
    host.id = canvas.id + suffix;
    host.style.cssText = 'position:absolute;overflow:hidden;pointer-events:none;margin:0;' +
                         'padding:0;border:0';
    canvas.after(host);
    entry.host = host;
    entry.place = () => {
        host.style.left = canvas.offsetLeft + 'px';
        host.style.top = canvas.offsetTop + 'px';
        host.style.width = canvas.offsetWidth + 'px';
        host.style.height = canvas.offsetHeight + 'px';
        host.style.display = canvas.style.display === 'none' ? 'none' : 'block';
    };
    entry.place();
    // The canvas and its text field (web_text.c) have the focus as one.
    const mine = target => target !== null && (target === canvas || target === entry.textarea);
    entry.focusIn = e => mine(e.relatedTarget) || state.push(3, slot, 1);
    entry.focusOut = e => mine(e.relatedTarget) || state.push(3, slot, 0);
    canvas.addEventListener('focusin', entry.focusIn);
    canvas.addEventListener('focusout', entry.focusOut);
    entry.listeners.push(() => {
        canvas.removeEventListener('focusin', entry.focusIn);
        canvas.removeEventListener('focusout', entry.focusOut);
    });
    entry.width = canvas.clientWidth || width;
    entry.height = canvas.clientHeight || height;
    canvas.width = Math.max(1, Math.round(entry.width * devicePixelRatio));
    canvas.height = Math.max(1, Math.round(entry.height * devicePixelRatio));
    HEAPF32.set([entry.width, entry.height, canvas.width, canvas.height], box >> 2);
    // The drawing buffer follows the box.
    entry.observer = new ResizeObserver(entries => {
        const found = entries[entries.length - 1];
        const css = found.contentBoxSize[0];
        const device = found.devicePixelContentBoxSize && found.devicePixelContentBoxSize[0];
        // A new box may come before the new ratio: again a frame later.
        state.checkScale();
        requestAnimationFrame(state.checkScale);
        state.fit(slot, css.inlineSize, css.blockSize, device);
    });
    try {
        entry.observer.observe(canvas, {box: 'device-pixel-content-box'});
    } catch (error) {
        entry.observer.observe(canvas);
    }
    state.canvases[slot] = entry;
    return stringToUTF8(name, out, capacity);
});

EM_JS(void, mwinWebCloseCanvas, (const mwinContext* context, uint32_t slot), {
    Module.mwinWeb.get(context).close(slot);
});

EM_JS(void, mwinWebSetTitle, (const mwinContext* context, uint32_t slot, const char* title,
                              size_t length), {
    const text = UTF8ToString(title, length);
    document.title = text;
    Module.mwinWeb.get(context).canvases[slot].canvas.setAttribute('aria-label', text);
});

EM_JS(void, mwinWebSetSize, (const mwinContext* context, uint32_t slot, float width,
                             float height), {
    const canvas = Module.mwinWeb.get(context).canvases[slot].canvas;
    canvas.style.width = width + 'px';
    canvas.style.height = height + 'px';
});

EM_JS(void, mwinWebSetVisible, (const mwinContext* context, uint32_t slot, bool visible), {
    const entry = Module.mwinWeb.get(context).canvases[slot];
    entry.canvas.style.display = visible ? 'block' : 'none';
    entry.place();
});

EM_JS(void, mwinWebSetOpacity, (const mwinContext* context, uint32_t slot, float opacity), {
    Module.mwinWeb.get(context).canvases[slot].canvas.style.opacity = opacity;
});

EM_JS(bool, mwinWebFocus, (const mwinContext* context, uint32_t slot), {
    const canvas = Module.mwinWeb.get(context).canvases[slot].canvas;
    canvas.focus();
    return document.activeElement === canvas;
});

EM_JS(int, mwinWebSetFullscreen, (const mwinContext* context, uint32_t slot, bool fullscreen), {
    const state = Module.mwinWeb.get(context);
    const canvas = state.canvases[slot].canvas;
    if ((document.fullscreenElement === canvas) === !!fullscreen) {
        return 1;
    }
    if (!canvas.requestFullscreen) {
        return -1;
    }
    // Without a user's gesture the browser refuses.
    const asked = fullscreen ? canvas.requestFullscreen() : document.exitFullscreen();
    asked.catch(() => state.push(5, slot));
    return 0;
});

EM_JS(float, mwinWebScale, (void), {
    return devicePixelRatio;
});

EM_JS(void, mwinWebCheckScale, (const mwinContext* context), {
    Module.mwinWeb.get(context).checkScale();
});

EM_JS(void, mwinWebScreen, (float* out), {
    HEAPF32.set([screen.width, screen.height, screen.availWidth, screen.availHeight], out >> 2);
});

EM_JS(int, mwinWebTheme, (void), {
    return matchMedia('(prefers-color-scheme: dark)').matches ? 2 : 1;
});

EM_JS(bool, mwinWebReducedMotion, (void), {
    return matchMedia('(prefers-reduced-motion: reduce)').matches;
});

EM_JS(uint32_t, mwinWebLocales, (char* out, uint32_t capacity), {
    const list = (navigator.languages || [navigator.language]).join(',');
    const needed = lengthBytesUTF8(list);
    if (needed < capacity) {
        stringToUTF8(list, out, capacity);
    }
    return needed;
});
// clang-format on

bool mwinWebHasPage(void)
{
    return HasDocument();
}
