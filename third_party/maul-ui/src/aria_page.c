// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The ARIA adapter's calls into the page (record mui-0008), through
// Emscripten's EM_JS. Each adapter's elements are kept in Module.muiAria
// under a handle: the host element, the adapter's container in it, the
// enabling button, and the elements by slot. The container is invisible
// as Flutter makes its semantics (filter: opacity(0%), transparent
// text): visibility or display would hide it from screen readers too.

#include "aria.h"

#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>

// The enabling button was pressed.
EMSCRIPTEN_KEEPALIVE void muiAriaEnableFromPage(muiAriaAdapter* adapter)
{
    muiAriaAdapter_Enable(adapter);
}

// An element was clicked, focused, or its range set.
EMSCRIPTEN_KEEPALIVE void muiAriaEventFromPage(muiAriaAdapter* adapter, int kind, uint32_t slot,
                                               double value)
{
    muiAriaPerform(adapter, (muiAriaEvent)kind, slot, value);
}

// clang-format off
EM_JS(int, OpenPage, (const char* host, int deferred, const char* label, void* adapter), {
    const element = typeof document === "undefined" ? null
                                                    : document.querySelector(UTF8ToString(host));
    if (!element) {
        return -1;
    }
    const pages = Module.muiAria || (Module.muiAria = []);
    let handle = pages.indexOf(null);
    if (handle < 0) {
        handle = pages.length;
    }
    const root = document.createElement("div");
    root.style.cssText = "position:absolute;left:0;top:0;width:0;height:0;overflow:visible;" +
                         "margin:0;padding:0;border:0;filter:opacity(0%);" +
                         "color:rgba(0,0,0,0);pointer-events:none";
    element.appendChild(root);
    // Announcements where the browser has no ariaNotify, as invisible.
    const region = politeness => {
        const live = document.createElement("div");
        live.id = "mui" + handle + "-" + politeness;
        live.setAttribute("aria-live", politeness);
        live.style.cssText = root.style.cssText;
        element.appendChild(live);
        return live;
    };
    const page = {host: element, root, elements: [], button: null, requested: null,
                  polite: region("polite"), assertive: region("assertive")};
    // The node an event is for: the nearest element of the adapter's.
    const slotOf = target => {
        let at = target;
        while (at && at !== root && at.muiSlot === undefined) {
            at = at.parentElement;
        }
        return at && at.muiSlot !== undefined ? at.muiSlot : -1;
    };
    const send = (kind, target, value) => {
        const slot = slotOf(target);
        if (slot >= 0) {
            _muiAriaEventFromPage(adapter, kind, slot, value);
        }
    };
    root.addEventListener("click", e => send(0, e.target, 0));
    root.addEventListener("focusin", e => {
        // A focus the adapter gave is no client's asking.
        if (page.requested === e.target) {
            page.requested = null;
            return;
        }
        send(1, e.target, 0);
    });
    root.addEventListener("input", e => send(2, e.target, parseFloat(e.target.value)));
    // The program scrolls its own content: a scroll of the host, as a
    // screen reader brings an element into view, is put back.
    page.unscroll = () => {
        element.scrollTop = 0;
        element.scrollLeft = 0;
    };
    element.addEventListener("scroll", page.unscroll);
    if (deferred) {
        // Visually hidden, read and pressed by screen readers.
        const button = document.createElement("button");
        button.textContent = UTF8ToString(label);
        button.style.cssText = "position:absolute;left:0;top:0;width:1px;height:1px;" +
                               "overflow:hidden;clip-path:inset(50%);white-space:nowrap;" +
                               "margin:0;padding:0;border:0";
        button.addEventListener("click", () => _muiAriaEnableFromPage(adapter));
        element.appendChild(button);
        page.button = button;
    }
    pages[handle] = page;
    return handle;
});

EM_JS(void, ClosePage, (int handle), {
    const page = Module.muiAria[handle];
    page.host.removeEventListener("scroll", page.unscroll);
    page.polite.remove();
    page.assertive.remove();
    page.root.remove();
    if (page.button) {
        page.button.remove();
    }
    Module.muiAria[handle] = null;
});

// Whether the button had the focus, which the program's focus then takes.
EM_JS(int, DropButton, (int handle), {
    const page = Module.muiAria[handle];
    const focused = page.button !== null && document.activeElement === page.button;
    if (page.button) {
        page.button.remove();
        page.button = null;
    }
    return focused ? 1 : 0;
});

// Moves the DOM focus to an element: only from within the adapter's
// elements, or when asked, as the canvas keeps it for the keys
// otherwise.
EM_JS(void, Focus, (int handle, uint32_t slot, int always), {
    const page = Module.muiAria[handle];
    const element = page.elements[slot];
    const active = document.activeElement;
    if (element && active !== element && (always || page.root.contains(active))) {
        page.requested = element;
        element.focus({preventScroll: true});
    }
});

// Says a text, through ariaNotify where the browser has it, else a live
// region: emptied first so that the same text is said again, and emptied
// after 300 ms, as Flutter does, so that it is not read again as the
// page's content.
EM_JS(void, Announce, (int handle, const char* text, int assertive), {
    const page = Module.muiAria[handle];
    const said = UTF8ToString(text);
    if (typeof page.root.ariaNotify === "function") {
        page.root.ariaNotify(said, {priority: assertive ? "high" : "normal"});
        return;
    }
    const region = assertive ? page.assertive : page.polite;
    region.textContent = "";
    setTimeout(() => { region.textContent = said; }, 0);
    setTimeout(() => {
        if (region.textContent === said) {
            region.textContent = "";
        }
    }, 300);
});

EM_JS(void, Make, (int handle, uint32_t slot, int range, const char* id), {
    const element = document.createElement(range ? "input" : "div");
    if (range) {
        element.type = "range";
    }
    element.id = UTF8ToString(id);
    element.muiSlot = slot;
    element.style.cssText = "position:absolute;margin:0;padding:0;border:0;" +
                            "box-sizing:border-box;overflow:visible";
    Module.muiAria[handle].elements[slot] = element;
});

EM_JS(void, Remove, (int handle, uint32_t slot), {
    const elements = Module.muiAria[handle].elements;
    elements[slot].remove();
    elements[slot] = undefined;
});

// Puts an element at an index among its parent's: where it is, or before
// the one there. Placed in rising index after the leaving ones are
// gone, each parent's elements end in order.
EM_JS(void, Place, (int handle, uint32_t slot, int parent, uint32_t index), {
    const page = Module.muiAria[handle];
    const element = page.elements[slot];
    const into = parent < 0 ? page.root : page.elements[parent];
    const there = into.children[index] || null;
    if (there !== element) {
        into.insertBefore(element, there);
    }
});

EM_JS(void, Box, (int handle, uint32_t slot, double x, double y, double width, double height), {
    const style = Module.muiAria[handle].elements[slot].style;
    style.left = x + "px";
    style.top = y + "px";
    style.width = width + "px";
    style.height = height + "px";
});

// An attribute, or none for a NULL value. A range's value is its
// property, as the attribute only gives its first value.
EM_JS(void, Attribute, (int handle, uint32_t slot, const char* name, const char* value), {
    const element = Module.muiAria[handle].elements[slot];
    const key = UTF8ToString(name);
    if (key === "value" && element.tagName === "INPUT") {
        element.value = value ? UTF8ToString(value) : "";
    } else if (value) {
        element.setAttribute(key, UTF8ToString(value));
    } else {
        element.removeAttribute(key);
    }
});

EM_JS(void, Text, (int handle, uint32_t slot, const char* text), {
    Module.muiAria[handle].elements[slot].textContent = text ? UTF8ToString(text) : "";
});
// clang-format on

int muiAriaPageOpen(const char* host, bool deferred, const char* label, muiAriaAdapter* adapter)
{
    return OpenPage(host, deferred ? 1 : 0, label, adapter);
}

void muiAriaPageClose(int page)
{
    ClosePage(page);
}

bool muiAriaPageDropButton(int page)
{
    return DropButton(page) != 0;
}

void muiAriaPageFocus(int page, uint32_t slot, bool always)
{
    Focus(page, slot, always ? 1 : 0);
}

void muiAriaPageAnnounce(int page, const char* text, bool assertive)
{
    Announce(page, text, assertive ? 1 : 0);
}

void muiAriaPageMake(int page, uint32_t slot, bool range, const char* id)
{
    Make(page, slot, range ? 1 : 0, id);
}

void muiAriaPageRemove(int page, uint32_t slot)
{
    Remove(page, slot);
}

void muiAriaPagePlace(int page, uint32_t slot, uint32_t parent, uint32_t index)
{
    Place(page, slot, parent == ARIA_NO_SLOT ? -1 : (int)parent, index);
}

void muiAriaPageBox(int page, uint32_t slot, float x, float y, float width, float height)
{
    Box(page, slot, (double)x, (double)y, (double)width, (double)height);
}

void muiAriaPageAttribute(int page, uint32_t slot, const char* name, const char* value)
{
    Attribute(page, slot, name, value);
}

void muiAriaPageText(int page, uint32_t slot, const char* text)
{
    Text(page, slot, text);
}
