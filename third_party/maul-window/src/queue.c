// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The event stream: each window's records wait in one ring per class,
// and the stream hands out the oldest record of all rings by a sequence
// number the context counts.
//
// A state notification replaces one of its class still waiting for the
// same window (the newer one moves to the end, so order stays true),
// which bounds the waiting notifications of a window by the number of
// classes plus its requests in flight; the context refuses limits below
// that bound. Discrete input never merges: when its ring is full, or
// its text does not fit, the record is lost and the window gets
// mwin_eventInputStateReset. Motion, raw deltas and the wheel merge into
// the newest waiting record of their kind when their ring is full.

#include "core.h"
#include "text.h"

#include "maul-unicode/encoding.h"

#include <string.h>

// The class of a notification that a newer one of the same class
// replaces, or 0 for records that never coalesce.
static int CoalesceClass(mwinEventType type)
{
    switch (type)
    {
    case mwin_eventResized:
        return 1;
    case mwin_eventPixelSizeChanged:
        return 2;
    case mwin_eventScaleChanged:
        return 3;
    case mwin_eventMoved:
        return 4;
    case mwin_eventModeChanged:
        return 5;
    case mwin_eventFocusGained:
    case mwin_eventFocusLost:
        return 6;
    case mwin_eventOccluded:
    case mwin_eventRevealed:
        return 7;
    case mwin_eventShown:
    case mwin_eventHidden:
        return 8;
    case mwin_eventCloseRequested:
        return 9;
    case mwin_eventInputStateReset:
        return 10;
    case mwin_eventSuspending:
    case mwin_eventSuspended:
    case mwin_eventResuming:
    case mwin_eventResumed:
        return 11;
    case mwin_eventSurfaceLost:
    case mwin_eventSurfaceRestored:
        return 12;
    case mwin_eventMonitorChanged:
        return 13;
    case mwin_eventDisplayChanged:
        return 14;
    case mwin_eventSafeAreaChanged:
        return 15;
    case mwin_eventVirtualKeyboardChanged:
        return 16;
    case mwin_eventThemeChanged:
        return 17;
    case mwin_eventPowerChanged:
        return 18;
    case mwin_eventLocaleChanged:
        return 19;
    case mwin_eventKeyboardLayoutChanged:
        return 20;
    case mwin_eventImePreedit:
        return 21;
    case mwin_eventGamepadChanged:
        return 22;
    default:
        return 0;
    }
}

// The classes a record may take, beyond a window's own: the critical
// ring comes before everything.
enum
{
    ClassCritical = MWIN_CLASSES,
};

static int ClassOf(mwinEventType type)
{
    switch (type)
    {
    case mwin_eventSuspending:
    case mwin_eventSuspended:
    case mwin_eventResuming:
    case mwin_eventResumed:
    case mwin_eventSurfaceLost:
    case mwin_eventSurfaceRestored:
        return ClassCritical;
    case mwin_eventCursorMoved:
    case mwin_eventTouchMoved:
    case mwin_eventPenMoved:
    case mwin_eventDragMoved:
        return mwin_classMotion;
    case mwin_eventDragEntered:
    case mwin_eventDragLeft:
    case mwin_eventDropped:
        return mwin_classDiscrete;
    case mwin_eventRawPointerDelta:
        return mwin_classRaw;
    case mwin_eventWheel:
        return mwin_classWheel;
    default:
        return (type >= mwin_eventKeyDown && type <= mwin_eventPenButtonUp) ||
                       type == mwin_eventImePreedit
                   ? mwin_classDiscrete
                   : mwin_classNotification;
    }
}

static uint16_t At(const mwinRing* ring, uint16_t index)
{
    return (uint16_t)((ring->head + index) % ring->capacity);
}

// Removes the record at position index, keeping the others in order.
static void RemoveAt(mwinRing* ring, uint16_t index)
{
    for (uint16_t i = index; i + 1 < ring->count; i++)
    {
        ring->events[At(ring, i)] = ring->events[At(ring, (uint16_t)(i + 1))];
        ring->sequences[At(ring, i)] = ring->sequences[At(ring, (uint16_t)(i + 1))];
    }
    ring->count -= 1;
}

static bool SameWindow(mwinWindowId a, mwinWindowId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

static bool SameGamepad(mwinGamepadId a, mwinGamepadId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

// Whether a waiting notification is about the same thing as a new one of
// its class: the same window, for a monitor's change the same monitor,
// and for a gamepad's change or reset the same gamepad.
static bool SameSubject(const mwinEvent* a, const mwinEvent* b)
{
    bool gamepad = a->type == mwin_eventGamepadChanged ||
                   (a->type == mwin_eventInputStateReset && a->window.index1 == 0);
    return SameWindow(a->window, b->window) &&
           (a->type != mwin_eventMonitorChanged ||
            (a->data.monitor.index1 == b->data.monitor.index1 &&
             a->data.monitor.generation == b->data.monitor.generation)) &&
           (!gamepad || SameGamepad(a->data.gamepad, b->data.gamepad));
}

// Appends a record; false when the ring is full.
static bool Append(mwinContext* context, mwinRing* ring, const mwinEvent* event)
{
    int coalesce = CoalesceClass(event->type);
    for (uint16_t i = 0; coalesce != 0 && i < ring->count; i++)
    {
        const mwinEvent* waiting = &ring->events[At(ring, i)];
        if (CoalesceClass(waiting->type) == coalesce && SameSubject(waiting, event))
        {
            RemoveAt(ring, i);
            break;
        }
    }
    if (ring->count == ring->capacity)
    {
        return false;
    }
    uint16_t slot = At(ring, ring->count);
    ring->events[slot] = *event;
    ring->events[slot].samples = 1;
    ring->sequences[slot] = context->sequence++;
    ring->count += 1;
    return true;
}

// The window state a notification reports.
static void Apply(mwinWindowState* state, const mwinEvent* event)
{
    switch (event->type)
    {
    case mwin_eventWindowCreated:
        state->created = true;
        state->surfaceGeneration += 1;
        break;
    case mwin_eventResized:
        state->size = event->data.size;
        break;
    case mwin_eventPixelSizeChanged:
        state->pixelSize = event->data.pixelSize;
        break;
    case mwin_eventScaleChanged:
        state->scale = event->data.scale.scale;
        break;
    case mwin_eventMoved:
        state->position = event->data.position;
        break;
    case mwin_eventModeChanged:
        state->mode = event->data.mode;
        break;
    case mwin_eventFocusGained:
    case mwin_eventFocusLost:
        state->focused = event->type == mwin_eventFocusGained;
        break;
    case mwin_eventOccluded:
    case mwin_eventRevealed:
        state->occluded = event->type == mwin_eventOccluded;
        break;
    case mwin_eventShown:
    case mwin_eventHidden:
        state->visible = event->type == mwin_eventShown;
        break;
    case mwin_eventSurfaceLost:
        state->surfaceLost = true;
        break;
    case mwin_eventSurfaceRestored:
        state->surfaceLost = false;
        state->surfaceGeneration += 1;
        break;
    case mwin_eventDisplayChanged:
        state->monitor = event->data.monitor;
        break;
    case mwin_eventSafeAreaChanged:
        state->safeArea = event->data.insets;
        break;
    case mwin_eventVirtualKeyboardChanged:
        state->virtualKeyboard = event->data.rect;
        break;
    case mwin_eventImePreedit:
        state->composing = event->data.preedit.length > 0;
        break;
    default:
        break;
    }
}

// Whether two input records are of one kind, so one can absorb the other.
static bool SameKind(const mwinEvent* a, const mwinEvent* b)
{
    const mwinGamepadAxisEvent* axis = &a->data.gamepadAxis;
    return a->type == b->type &&
           (a->type != mwin_eventTouchMoved || a->data.touch.id == b->data.touch.id) &&
           (a->type != mwin_eventGamepadAxisMoved ||
            (SameGamepad(axis->gamepad, b->data.gamepadAxis.gamepad) &&
             axis->axis == b->data.gamepadAxis.axis && axis->raw == b->data.gamepadAxis.raw));
}

// Merges a record into the newest waiting one of its kind: the newer
// position wins, deltas and wheel turns add up. False when there is none.
static bool Merge(mwinRing* ring, const mwinEvent* event)
{
    for (uint16_t i = ring->count; i > 0; i--)
    {
        mwinEvent* waiting = &ring->events[At(ring, (uint16_t)(i - 1))];
        if (!SameKind(waiting, event))
        {
            continue;
        }
        uint16_t samples = waiting->samples;
        mwinEvent merged = *event;
        if (event->type == mwin_eventRawPointerDelta)
        {
            merged.data.delta.x += waiting->data.delta.x;
            merged.data.delta.y += waiting->data.delta.y;
        }
        else if (event->type == mwin_eventWheel)
        {
            merged.data.wheel.x += waiting->data.wheel.x;
            merged.data.wheel.y += waiting->data.wheel.y;
        }
        *waiting = merged;
        waiting->samples = samples < UINT16_MAX ? (uint16_t)(samples + 1) : samples;
        return true;
    }
    return false;
}

static void PostReset(mwinContext* context, uint32_t slot, uint64_t timeNs)
{
    mwinEvent reset = {0};
    reset.type = mwin_eventInputStateReset;
    reset.window = mwinWindowIdOf(context, slot);
    reset.timeNs = timeNs;
    // A reset replaces one still waiting, so it always has room.
    (void)Append(context, &context->windows[slot].rings[mwin_classNotification], &reset);
}

// Whether a composition's offsets and segments lie within its text.
static bool IsPreeditValid(const mwinPreeditEvent* preedit)
{
    if (preedit->caret < -1 || preedit->caret > (int64_t)preedit->length ||
        preedit->selectionStart > preedit->selectionEnd ||
        preedit->selectionEnd > preedit->length ||
        preedit->segmentCount > MWIN_MAX_PREEDIT_SEGMENTS ||
        (preedit->segments == nullptr && preedit->segmentCount != 0))
    {
        return false;
    }
    for (uint32_t i = 0; i < preedit->segmentCount; i++)
    {
        const mwinPreeditSegment* segment = &preedit->segments[i];
        if (segment->start > preedit->length || preedit->length - segment->start < segment->length)
        {
            return false;
        }
    }
    return true;
}

// Moves a record's text, and a composition's segments before it, into
// the window's storage; false when the record is lost. The text is
// validated here once more, though backends repair what they receive.
static bool TakeText(mwinContext* context, uint32_t slot, mwinEvent* record)
{
    const char* text = record->data.text.text;
    uint32_t length = record->data.text.length;
    bool preedit = record->type == mwin_eventImePreedit;
    uint32_t segments = preedit ? record->data.preedit.segmentCount : 0;
    if ((text == nullptr && length != 0) || muniValidateUtf8(text, length).status != muni_success ||
        (preedit && !IsPreeditValid(&record->data.preedit)))
    {
        return false;
    }
    uint32_t segmentBytes = segments * (uint32_t)sizeof(mwinPreeditSegment);
    if (length + segmentBytes == 0)
    {
        record->data.text.text = "";
        return true;
    }
    unsigned char* block = mwinReserveText(&context->windows[slot].text, segmentBytes + length,
                                           alignof(mwinPreeditSegment));
    if (block == nullptr)
    {
        PostReset(context, slot, record->timeNs);
        return false;
    }
    if (segments > 0)
    {
        memcpy(block, record->data.preedit.segments, segmentBytes);
        record->data.preedit.segments = (const mwinPreeditSegment*)block;
    }
    if (length > 0)
    {
        memcpy(block + segmentBytes, text, length);
    }
    record->data.text.text = (const char*)block + segmentBytes;
    return true;
}

// Whether a key record goes, because a composition consumes its key: a
// character key that goes down while one runs, and its release.
static bool Consumed(mwinWindow* window, const mwinEvent* record)
{
    if ((record->type != mwin_eventKeyDown && record->type != mwin_eventKeyUp) ||
        record->data.key.code >= 256)
    {
        return false;
    }
    uint8_t bit = (uint8_t)(1u << (record->data.key.code & 7u));
    uint8_t* held = &window->consumedKeys[record->data.key.code >> 3];
    if (record->type == mwin_eventKeyUp)
    {
        bool consumed = (*held & bit) != 0;
        *held = (uint8_t)(*held & ~bit);
        return consumed;
    }
    mwinKey key = record->data.key.key;
    if (window->state.composing && key != 0 && key < MWIN_KEY_NAMED)
    {
        *held = (uint8_t)(*held | bit);
        return true;
    }
    return false;
}

void mwinPost(mwinContext* context, uint32_t slot, const mwinEvent* event)
{
    mwinWindow* window = &context->windows[slot];
    if (window->status != mwin_slotLive)
    {
        return;
    }
    mwinEvent record = *event;
    record.window = mwinWindowIdOf(context, slot);
    bool text = record.type == mwin_eventTextInput || record.type == mwin_eventImePreedit;
    if ((text && !TakeText(context, slot, &record)) || Consumed(window, &record))
    {
        return;
    }
    Apply(&window->state, &record);
    int kind = ClassOf(record.type);
    mwinRing* ring = kind == ClassCritical ? &context->critical : &window->rings[kind];
    bool added = Append(context, ring, &record) ||
                 ((kind == mwin_classMotion || kind == mwin_classRaw || kind == mwin_classWheel) &&
                  Merge(ring, &record));
    if (!added || record.type == mwin_eventFocusLost)
    {
        PostReset(context, slot, record.timeNs);
    }
}

void mwinPostGlobal(mwinContext* context, const mwinEvent* event)
{
    mwinEvent record = *event;
    record.window = (mwinWindowId){0};
    bool critical = ClassOf(record.type) == ClassCritical;
    // Coalescing keeps both rings within their bound.
    (void)Append(context, critical ? &context->critical : &context->global, &record);
    for (uint32_t i = 0; record.type == mwin_eventSuspending && i < context->limits.windows; i++)
    {
        if (context->windows[i].status == mwin_slotLive)
        {
            PostReset(context, i, record.timeNs);
        }
    }
}

void mwinPostGamepadRecord(mwinContext* context, const mwinEvent* event)
{
    bool axis = event->type == mwin_eventGamepadAxisMoved;
    mwinRing* ring = &context->gamepadRings[axis ? mwin_padRingAxes : mwin_padRingButtons];
    if (Append(context, ring, event) || (axis && Merge(ring, event)))
    {
        return;
    }
    // The program cannot follow the gamepad from its records: it reads
    // the state again.
    mwinEvent reset = {.type = mwin_eventInputStateReset, .timeNs = event->timeNs};
    reset.data.gamepad = axis ? event->data.gamepadAxis.gamepad : event->data.gamepadButton.gamepad;
    (void)Append(context, &context->global, &reset);
}

// Frees everything a slot's rings hold but its completions, and queues
// its destroyed record.
void mwinPostDestroyed(mwinContext* context, uint32_t slot, uint64_t timeNs)
{
    mwinWindow* window = &context->windows[slot];
    mwinWindowId id = mwinWindowIdOf(context, slot);
    for (uint16_t i = context->critical.count; i > 0; i--)
    {
        if (SameWindow(context->critical.events[At(&context->critical, (uint16_t)(i - 1))].window,
                       id))
        {
            RemoveAt(&context->critical, (uint16_t)(i - 1));
        }
    }
    for (int kind = mwin_classDiscrete; kind < MWIN_CLASSES; kind++)
    {
        window->rings[kind].count = 0;
    }
    mwinDropText(&window->text);
    memset(window->consumedKeys, 0, sizeof(window->consumedKeys));
    // Completions stay: every request is answered, even a cancelled one.
    mwinRing* ring = &window->rings[mwin_classNotification];
    for (uint16_t i = ring->count; i > 0; i--)
    {
        if (ring->events[At(ring, (uint16_t)(i - 1))].type != mwin_eventRequestCompleted)
        {
            RemoveAt(ring, (uint16_t)(i - 1));
        }
    }
    mwinEvent event = {0};
    event.type = mwin_eventWindowDestroyed;
    event.window = mwinWindowIdOf(context, slot);
    event.timeNs = timeNs;
    (void)Append(context, ring, &event);
}

void mwinBeginPump(mwinContext* context)
{
    for (uint32_t i = 0; i < context->limits.windows; i++)
    {
        mwinReclaimText(&context->windows[i].text);
    }
}

// The ring holding the next record of the stream, or NULL: the critical
// ring's first, else the oldest of all others. windowOut receives the
// window whose ring it is, or NULL.
static mwinRing* FindOldest(mwinContext* context, mwinWindow** windowOut)
{
    *windowOut = nullptr;
    if (context->critical.count > 0)
    {
        return &context->critical;
    }
    mwinRing* oldest = context->global.count > 0 ? &context->global : nullptr;
    uint64_t oldestSequence = oldest != nullptr ? oldest->sequences[oldest->head] : UINT64_MAX;
    for (int kind = mwin_padRingButtons; kind <= mwin_padRingAxes; kind++)
    {
        mwinRing* ring = &context->gamepadRings[kind];
        if (ring->count > 0 && ring->sequences[ring->head] < oldestSequence)
        {
            oldest = ring;
            oldestSequence = ring->sequences[ring->head];
        }
    }
    for (uint32_t i = 0; i < context->limits.windows; i++)
    {
        for (int kind = 0; kind < MWIN_CLASSES; kind++)
        {
            mwinRing* ring = &context->windows[i].rings[kind];
            if (ring->count > 0 && ring->sequences[ring->head] < oldestSequence)
            {
                *windowOut = &context->windows[i];
                oldest = ring;
                oldestSequence = ring->sequences[ring->head];
            }
        }
    }
    return oldest;
}

mwinResult mwinNextEvent(mwinContext* context, mwinEvent* eventOut)
{
    if (context == nullptr || eventOut == nullptr)
    {
        return mwin_errorInvalid;
    }
    mwinWindow* window = nullptr;
    mwinRing* oldest = FindOldest(context, &window);
    if (oldest == nullptr)
    {
        return mwin_empty;
    }
    *eventOut = oldest->events[oldest->head];
    oldest->head = (uint16_t)((oldest->head + 1) % oldest->capacity);
    oldest->count -= 1;
    if (window != nullptr &&
        (eventOut->type == mwin_eventTextInput || eventOut->type == mwin_eventImePreedit))
    {
        // A composition's segments come before its text.
        const mwinPreeditEvent* preedit = &eventOut->data.preedit;
        bool segments = eventOut->type == mwin_eventImePreedit && preedit->segmentCount > 0;
        const void* start = segments ? (const void*)preedit->segments : preedit->text;
        if (preedit->length > 0 || segments)
        {
            mwinDrainText(&window->text, start, preedit->text + preedit->length);
        }
    }
    if (window != nullptr && window->status == mwin_slotDestroyed &&
        eventOut->type == mwin_eventWindowDestroyed)
    {
        window->status = mwin_slotFree;
    }
    if (eventOut->type == mwin_eventRequestCompleted)
    {
        mwinReleaseRequest(context, eventOut->data.completion.request);
    }
    if (eventOut->type == mwin_eventMonitorRemoved)
    {
        mwinReleaseMonitor(context, eventOut->data.monitor);
    }
    if (eventOut->type == mwin_eventGamepadRemoved)
    {
        mwinReleaseGamepad(context, eventOut->data.gamepad);
    }
    return mwin_success;
}
