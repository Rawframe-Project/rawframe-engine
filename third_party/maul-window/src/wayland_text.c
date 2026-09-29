// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Input methods on Wayland, through text-input-v3.

#include "wayland_text.h"

#include <math.h>
#include <string.h>

static void Post(mwinWaylandPlatform* platform, const mwinEvent* event)
{
    mwinPost(platform->context, (uint32_t)platform->text.focus, event);
}

// Ends a composition the window shows.
static void EndComposition(mwinWaylandPlatform* platform, uint32_t slot)
{
    if (!platform->context->windows[slot].state.composing)
    {
        return;
    }
    mwinEvent end = {0};
    end.type = mwin_eventImePreedit;
    end.timeNs = mwinMonotonicNow();
    end.data.preedit.caret = -1;
    mwinPost(platform->context, slot, &end);
}

// Tells the input method whether the focused window takes text, and
// where its caret is.
static void Apply(mwinWaylandPlatform* platform)
{
    const mwinWaylandApi* api = &platform->api;
    const mwinWaylandText* text = &platform->text;
    if (text->focus < 0)
    {
        return;
    }
    const mwinWaylandWindow* window = &platform->windows[text->focus];
    struct wl_proxy* object = (struct wl_proxy*)text->textInput;
    uint32_t version = mwinWlVersion(api, object);
    if (window->textInput)
    {
        (void)mwinWlRequest(api, object, ZWP_TEXT_INPUT_V3_ENABLE, nullptr, 0);
        api->proxyMarshalFlags(object, ZWP_TEXT_INPUT_V3_SET_CONTENT_TYPE, nullptr, version, 0,
                               ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE,
                               ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL);
        mwinRect caret = window->caret;
        api->proxyMarshalFlags(object, ZWP_TEXT_INPUT_V3_SET_CURSOR_RECTANGLE, nullptr, version, 0,
                               (int32_t)lroundf(caret.x), (int32_t)lroundf(caret.y),
                               (int32_t)lroundf(caret.width), (int32_t)lroundf(caret.height));
    }
    else
    {
        (void)mwinWlRequest(api, object, ZWP_TEXT_INPUT_V3_DISABLE, nullptr, 0);
    }
    (void)mwinWlRequest(api, object, ZWP_TEXT_INPUT_V3_COMMIT, nullptr, 0);
}

// Keeps a string of the input method's until done; NULL is empty.
static void Keep(mwinWaylandString* kept, const char* text, uint32_t capacity)
{
    size_t length = text != nullptr ? strlen(text) : 0;
    kept->set = true;
    kept->lost = length > capacity;
    kept->length = kept->lost ? 0 : (uint32_t)length;
    if (kept->length > 0)
    {
        memcpy(kept->bytes, text, kept->length);
    }
}

static void OnEnter(void* data, struct zwp_text_input_v3* object, struct wl_surface* surface)
{
    (void)object;
    mwinWaylandPlatform* platform = data;
    platform->text.focus = mwinWaylandSlotOf(platform, surface);
    Apply(platform);
}

static void OnLeave(void* data, struct zwp_text_input_v3* object, struct wl_surface* surface)
{
    (void)object;
    (void)surface;
    mwinWaylandPlatform* platform = data;
    mwinWaylandText* text = &platform->text;
    // The compositor ignores the text input until the next enter.
    if (text->focus >= 0)
    {
        EndComposition(platform, (uint32_t)text->focus);
    }
    text->focus = -1;
}

static void OnPreedit(void* data, struct zwp_text_input_v3* object, const char* string,
                      int32_t cursorBegin, int32_t cursorEnd)
{
    (void)object;
    mwinWaylandPlatform* platform = data;
    Keep(&platform->text.preedit, string, platform->context->limits.textBytesPerWindow);
    platform->text.cursorBegin = cursorBegin;
    platform->text.cursorEnd = cursorEnd;
}

static void OnCommit(void* data, struct zwp_text_input_v3* object, const char* string)
{
    (void)object;
    mwinWaylandPlatform* platform = data;
    Keep(&platform->text.commit, string, platform->context->limits.textBytesPerWindow);
}

static void OnDeleteSurrounding(void* data, struct zwp_text_input_v3* object, uint32_t before,
                                uint32_t after)
{
    // The program's text is not shared with the input method, so there
    // is nothing around the caret to delete.
    (void)data;
    (void)object;
    (void)before;
    (void)after;
}

// Posts the composition done applies: the new one, or its end.
static void PostPreedit(mwinWaylandPlatform* platform, uint64_t timeNs)
{
    const mwinWaylandText* text = &platform->text;
    if (text->preedit.length == 0)
    {
        EndComposition(platform, (uint32_t)text->focus);
        return;
    }
    mwinPreeditSegment segment = {0, text->preedit.length, mwin_preeditUnderline};
    mwinEvent event = {0};
    event.type = mwin_eventImePreedit;
    event.timeNs = timeNs;
    mwinPreeditEvent* preedit = &event.data.preedit;
    preedit->text = text->preedit.bytes;
    preedit->length = text->preedit.length;
    bool shown = text->cursorBegin >= 0 && text->cursorEnd >= 0;
    preedit->caret = shown ? text->cursorEnd : -1;
    preedit->selectionStart = shown ? (uint32_t)text->cursorBegin : 0;
    preedit->selectionEnd = shown ? (uint32_t)text->cursorEnd : 0;
    preedit->segments = &segment;
    preedit->segmentCount = 1;
    Post(platform, &event);
}

static void OnDone(void* data, struct zwp_text_input_v3* object, uint32_t serial)
{
    (void)object;
    (void)serial;
    mwinWaylandPlatform* platform = data;
    mwinWaylandText* text = &platform->text;
    uint64_t timeNs = mwinMonotonicNow();
    if (text->focus >= 0)
    {
        if (text->commit.lost || text->preedit.lost)
        {
            mwinEvent reset = {0};
            reset.type = mwin_eventInputStateReset;
            reset.timeNs = timeNs;
            Post(platform, &reset);
        }
        if (text->commit.length > 0)
        {
            mwinEvent event = {0};
            event.type = mwin_eventTextInput;
            event.timeNs = timeNs;
            event.data.text = (mwinTextEvent){text->commit.bytes, text->commit.length};
            Post(platform, &event);
        }
        PostPreedit(platform, timeNs);
    }
    text->commit.set = text->commit.lost = false;
    text->commit.length = 0;
    text->preedit.set = text->preedit.lost = false;
    text->preedit.length = 0;
}

static const struct zwp_text_input_v3_listener s_textListener = {
    OnEnter, OnLeave, OnPreedit, OnCommit, OnDeleteSurrounding, OnDone,
};

void mwinWaylandAttachText(mwinWaylandPlatform* platform)
{
    mwinWaylandText* text = &platform->text;
    text->focus = -1;
    if (platform->textInputs == nullptr || platform->seat == nullptr || text->textInput != nullptr)
    {
        return;
    }
    text->textInput = mwinWlCreateFor(&platform->api, platform->textInputs,
                                      ZWP_TEXT_INPUT_MANAGER_V3_GET_TEXT_INPUT,
                                      &zwp_text_input_v3_interface, platform->seat);
    mwinWlListen(&platform->api, text->textInput, &s_textListener, platform);
}

void mwinWaylandDetachText(mwinWaylandPlatform* platform)
{
    mwinWaylandText* text = &platform->text;
    if (text->textInput != nullptr)
    {
        (void)mwinWlRequest(&platform->api, text->textInput, ZWP_TEXT_INPUT_V3_DESTROY, nullptr,
                            WL_MARSHAL_FLAG_DESTROY);
        text->textInput = nullptr;
    }
    text->focus = -1;
}

mwinOutcome mwinWaylandSetTextInput(mwinWaylandPlatform* platform, uint32_t slot, bool enabled,
                                    mwinRect caret)
{
    if (platform->text.textInput == nullptr)
    {
        return mwin_outcomeUnsupported;
    }
    mwinWaylandWindow* window = &platform->windows[slot];
    window->textInput = enabled;
    window->caret = caret;
    if (!enabled)
    {
        EndComposition(platform, slot);
    }
    if (platform->text.focus == (int32_t)slot)
    {
        Apply(platform);
    }
    return mwin_outcomeDone;
}

void mwinWaylandForgetTextFocus(mwinWaylandPlatform* platform, uint32_t slot)
{
    if (platform->text.focus == (int32_t)slot)
    {
        platform->text.focus = -1;
    }
}
