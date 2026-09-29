// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web services.

#include "web_services.h"

#include <emscripten/em_js.h>

EM_JS_DEPS(mwin_web_services, "$UTF8ToString");

// clang-format off
// A new tab, cut from the page before it loads. With noopener the page
// could not learn whether a blocker stopped the tab; this way it can.
EM_JS(int, OpenAddress, (const char* address, uint32_t length), {
    if (typeof window === 'undefined' || !window.open) {
        return 1;
    }
    const opened = window.open(UTF8ToString(address, length, true), '_blank');
    if (!opened) {
        return 2;
    }
    opened.opener = null;
    return 0;
});

EM_JS(bool, HasWakeLock, (void), {
    return typeof navigator !== 'undefined' && !!navigator.wakeLock;
});

// The wish, the lock held and whether one is asked for live on the
// page; a lock is asked for while wished and the page shows.
EM_JS(void, Hold, (const mwinContext* context, bool wanted), {
    const state = Module.mwinWeb.get(context);
    if (!state.wake) {
        const wake = {wanted: false, lock: null, asking: false};
        wake.ask = () => {
            if (!wake.wanted || wake.lock || wake.asking || document.visibilityState !== 'visible') {
                return;
            }
            wake.asking = true;
            navigator.wakeLock.request('screen').then(lock => {
                wake.asking = false;
                if (!wake.wanted) {
                    lock.release();
                    return;
                }
                // Released before it came, as the page was hidden: asked
                // for again, if the page shows.
                if (lock.released) {
                    wake.ask();
                    return;
                }
                wake.lock = lock;
                lock.addEventListener('release', () => {
                    if (wake.lock === lock) {
                        wake.lock = null;
                    }
                });
            }, () => { wake.asking = false; });
        };
        document.addEventListener('visibilitychange', wake.ask);
        state.listeners.push(() => {
            document.removeEventListener('visibilitychange', wake.ask);
            wake.wanted = false;
            if (wake.lock) {
                wake.lock.release();
            }
        });
        state.wake = wake;
    }
    const wake = state.wake;
    wake.wanted = wanted;
    if (wanted) {
        wake.ask();
    } else if (wake.lock) {
        const lock = wake.lock;
        wake.lock = null;
        lock.release();
    }
});
// clang-format on

mwinOutcome mwinWebOpenUrl(const mwinRequest* request)
{
    return (mwinOutcome)OpenAddress(request->value.text.bytes, request->value.text.length);
}

mwinOutcome mwinWebCanKeepAwake(void)
{
    return HasWakeLock() ? mwin_outcomeDone : mwin_outcomeUnsupported;
}

void mwinWebKeepAwake(mwinWebPlatform* platform, bool wanted)
{
    if (wanted != platform->awake && (!wanted || HasWakeLock()))
    {
        platform->awake = wanted;
        Hold(platform->context, wanted);
    }
}
