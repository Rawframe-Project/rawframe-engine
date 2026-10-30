// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The test backend: a platform with no screen. What mwinContext_PostTestEvent reports
// and the requests the program makes wait until the next pump, as a
// platform's messages would; the pump delivers the reports in order,
// then answers the requests in order as the answer set for their kind
// says. Carrying a request out posts the notifications a platform would
// send.

#include "backend_test.h"

#include "allocator.h"
#include "backend.h"
#include "core.h"
#include "cursor.h"

#include "maul-unicode/encoding.h"
#include "maul-window/test.h"

#include <math.h>
#include <string.h>

// Where the platform block's parts lie, laid out with checked
// arithmetic: the platform, a pending answer per request slot of every
// window, and what each gamepad was given.
typedef struct PlatformParts
{
    mwinLayout layout;
    size_t pending;
    size_t pads;
    size_t cursors;
    size_t texts;
} PlatformParts;

static uint32_t PendingCapacity(const mwinLimits* limits)
{
    return (uint32_t)limits->windows * limits->requestsPerWindow;
}

static PlatformParts PartsOf(const mwinLimits* limits)
{
    PlatformParts parts = {0};
    (void)mwinLayoutAdd(&parts.layout, 1, sizeof(mwinTestPlatform), alignof(mwinTestPlatform));
    parts.pending = mwinLayoutAdd(&parts.layout, PendingCapacity(limits), sizeof(mwinTestPending),
                                  alignof(mwinTestPending));
    parts.pads =
        mwinLayoutAdd(&parts.layout, limits->gamepads, sizeof(mwinTestPad), alignof(mwinTestPad));
    parts.cursors = mwinLayoutAdd(&parts.layout, limits->windows, sizeof(mwinTestCursor),
                                  alignof(mwinTestCursor));
    parts.texts =
        mwinLayoutAdd(&parts.layout, limits->windows, sizeof(mwinTestText), alignof(mwinTestText));
    return parts;
}

static size_t PlatformBytes(const mwinContext* context)
{
    return PartsOf(&context->limits).layout.size;
}

static mwinResult Start(mwinContext* context)
{
    PlatformParts parts = PartsOf(&context->limits);
    unsigned char* block =
        parts.layout.overflow
            ? nullptr
            : mwinAllocate(&context->allocator, parts.layout.size, alignof(max_align_t));
    if (block == nullptr)
    {
        return mwin_errorCapacity;
    }
    memset(block, 0, parts.layout.size);
    mwinTestPlatform* platform = (mwinTestPlatform*)block;
    platform->pending = (mwinTestPending*)(block + parts.pending);
    platform->pendingCapacity = PendingCapacity(&context->limits);
    platform->pads = (mwinTestPad*)(block + parts.pads);
    platform->cursors = (mwinTestCursor*)(block + parts.cursors);
    platform->texts = (mwinTestText*)(block + parts.texts);
    platform->scale = 1.0f;
    context->backendData = platform;
    return mwin_success;
}

static void Stop(mwinContext* context)
{
    mwinTestReleaseClipboard(context, mwinTestPlatformOf(context));
    mwinRelease(&context->allocator, context->backendData, PlatformBytes(context),
                alignof(max_align_t));
    context->backendData = nullptr;
}

static uint64_t Now(const mwinContext* context)
{
    return mwinTestPlatformOf(context)->timeNs;
}

// Whether a waiting request is still the one it was queued as.
static bool IsCurrent(const mwinContext* context, const mwinTestPending* pending)
{
    const mwinWindow* window = &context->windows[pending->slot];
    const mwinRequest* request = &window->requests[pending->request];
    return window->status == mwin_slotLive && window->generation == pending->windowGeneration &&
           request->status == mwin_requestActive &&
           request->generation == pending->requestGeneration;
}

static void Queue(mwinContext* context, uint32_t slot, uint32_t request)
{
    mwinTestPlatform* platform = mwinTestPlatformOf(context);
    if (platform->pendingCount == platform->pendingCapacity)
    {
        // Entries of answered requests go; the active requests left fit,
        // since the list has room for every request slot.
        uint32_t kept = 0;
        for (uint32_t i = 0; i < platform->pendingCount; i++)
        {
            if (IsCurrent(context, &platform->pending[i]))
            {
                platform->pending[kept++] = platform->pending[i];
            }
        }
        platform->pendingCount = kept;
    }
    const mwinWindow* window = &context->windows[slot];
    platform->pending[platform->pendingCount++] =
        (mwinTestPending){slot, request, window->generation, window->requests[request].generation};
}

static void CreateWindow(mwinContext* context, uint32_t slot)
{
    int32_t request = mwinFindActiveRequest(&context->windows[slot],
                                            context->limits.requestsPerWindow, mwin_requestCreate);
    Queue(context, slot, (uint32_t)request);
}

static void DestroyWindow(mwinContext* context, uint32_t slot)
{
    mwinTestPlatformOf(context)->cursors[slot] = (mwinTestCursor){0};
    mwinTestPlatformOf(context)->texts[slot] = (mwinTestText){0};
}

// Shows a cursor made from images over the window in a slot, taking the
// image for the platform's scale and marking it made.
static mwinOutcome ShowCursor(mwinContext* context, uint32_t slot, mwinCursorId id)
{
    mwinTestPlatform* platform = mwinTestPlatformOf(context);
    mwinCursor* cursor = mwinFindCursor(context, id);
    if (cursor == nullptr)
    {
        return mwin_outcomeFailed;
    }
    uint32_t image = mwinCursorImageFor(cursor, platform->scale);
    cursor->nativeId[image] = 1;
    platform->cursors[slot] = (mwinTestCursor){id, image};
    return mwin_outcomeDone;
}

static void ReleaseCursor(mwinContext* context, uint32_t cursor)
{
    mwinTestPlatform* platform = mwinTestPlatformOf(context);
    for (uint32_t slot = 0; slot < context->limits.windows; slot++)
    {
        if (platform->cursors[slot].cursor.index1 == cursor + 1)
        {
            platform->cursors[slot] = (mwinTestCursor){0};
        }
    }
}

static void Submit(mwinContext* context, uint32_t slot, uint32_t request)
{
    Queue(context, slot, request);
}

static void PostType(mwinContext* context, uint32_t slot, mwinEventType type)
{
    mwinEvent event = {0};
    event.type = type;
    event.timeNs = Now(context);
    mwinPost(context, slot, &event);
}

// Reports a new logical size, and the pixel size the scale gives it.
static void PostSize(mwinContext* context, uint32_t slot, mwinSize size)
{
    float scale = mwinTestPlatformOf(context)->scale;
    mwinEvent event = {0};
    event.timeNs = Now(context);
    event.type = mwin_eventResized;
    event.data.size = size;
    mwinPost(context, slot, &event);
    event.type = mwin_eventPixelSizeChanged;
    event.data.pixelSize = (mwinPixelSize){(uint32_t)lroundf(size.width * scale),
                                           (uint32_t)lroundf(size.height * scale)};
    mwinPost(context, slot, &event);
}

static void PostMode(mwinContext* context, uint32_t slot, mwinWindowMode mode)
{
    mwinEvent event = {0};
    event.type = mwin_eventModeChanged;
    event.timeNs = Now(context);
    event.data.mode = mode;
    mwinPost(context, slot, &event);
}

// The primary monitor's slot, or -1 when none is connected.
static int32_t PrimaryMonitor(const mwinContext* context)
{
    mwinMonitorId primary = {0};
    size_t count = 0;
    (void)mwinContext_GetMonitors(context, &primary, 1, &count);
    return count > 0 ? (int32_t)(primary.index1 - 1) : -1;
}

static void Create(mwinContext* context, uint32_t slot)
{
    mwinWindow* window = &context->windows[slot];
    PostType(context, slot, mwin_eventWindowCreated);
    int32_t monitor = PrimaryMonitor(context);
    if (monitor >= 0)
    {
        mwinEvent display = {0};
        display.type = mwin_eventDisplayChanged;
        display.timeNs = Now(context);
        display.data.monitor = mwinMonitorIdOf(context, (uint32_t)monitor);
        mwinPost(context, slot, &display);
    }
    mwinEvent event = {0};
    event.type = mwin_eventScaleChanged;
    event.timeNs = Now(context);
    event.data.scale = (mwinScaleChange){mwinTestPlatformOf(context)->scale, window->def.size};
    mwinPost(context, slot, &event);
    PostSize(context, slot, window->def.size);
    PostMode(context, slot, window->def.mode);
    if (window->def.kind != mwin_windowNormal)
    {
        // A popup is where it was put against its owner.
        mwinEvent moved = {0};
        moved.type = mwin_eventMoved;
        moved.timeNs = Now(context);
        moved.data.position = window->def.position;
        mwinPost(context, slot, &moved);
    }
    if (window->def.visible)
    {
        PostType(context, slot, mwin_eventShown);
    }
}

// Moves focus to a window, from the one that had it.
static void Focus(mwinContext* context, uint32_t slot)
{
    for (uint32_t i = 0; i < context->limits.windows; i++)
    {
        if (i != slot && context->windows[i].status == mwin_slotLive &&
            context->windows[i].state.focused)
        {
            PostType(context, i, mwin_eventFocusLost);
        }
    }
    PostType(context, slot, mwin_eventFocusGained);
}

// Carries out a request the answers say to do, and says how it ended.
static mwinOutcome CarryOut(mwinContext* context, uint32_t slot, const mwinRequest* request)
{
    mwinWindow* window = &context->windows[slot];
    switch (request->kind)
    {
    case mwin_requestCreate:
        Create(context, slot);
        break;
    case mwin_requestTitle:
        memmove(window->title, window->pendingTitle, window->pendingTitleLength);
        window->titleLength = window->pendingTitleLength;
        break;
    case mwin_requestSize:
        PostSize(context, slot, request->value.size);
        break;
    case mwin_requestPosition:
    {
        mwinEvent event = {0};
        event.type = mwin_eventMoved;
        event.timeNs = Now(context);
        event.data.position = request->value.position;
        mwinPost(context, slot, &event);
        break;
    }
    case mwin_requestMode:
        PostMode(context, slot, request->value.mode);
        break;
    case mwin_requestVisible:
        PostType(context, slot, request->value.visible ? mwin_eventShown : mwin_eventHidden);
        break;
    case mwin_requestFocus:
        Focus(context, slot);
        break;
    case mwin_requestTextInput:
    case mwin_requestVirtualKeyboard:
        mwinTestCarryOutText(context, slot, request);
        break;
    case mwin_requestClipboardWrite:
    case mwin_requestClipboardRead:
    case mwin_requestClipboardWriteData:
    case mwin_requestClipboardReadData:
    case mwin_requestPrimaryWrite:
    case mwin_requestPrimaryRead:
        return mwinTestUseClipboard(context, request);
    case mwin_requestOpenUrl:
    case mwin_requestRevealFile:
        mwinTestOpen(context, request);
        break;
    case mwin_requestIcon:
        mwinTestSetIcon(context, request);
        break;
    case mwin_requestCursorShape:
        mwinTestPlatformOf(context)->cursors[slot] = (mwinTestCursor){0};
        break;
    case mwin_requestCursorImage:
        return ShowCursor(context, slot, request->value.cursor);
    default:
        break; // the cursor changes on screen, with nothing to report
    }
    return mwin_outcomeDone;
}

static bool IsLifecycle(mwinEventType type)
{
    return type >= mwin_eventSuspending && type <= mwin_eventResumed;
}

// Reports about the application rather than a window.
static bool IsGlobal(mwinEventType type)
{
    return IsLifecycle(type) || type == mwin_eventKeyboardLayoutChanged;
}

// Delivers the gathered drop to its window, or drops it with the window.
static void Deliver(mwinContext* context, const mwinEvent* report)
{
    mwinTestPlatformOf(context)->dropWaiting = false;
    if (mwinFindWindow(context, report->window) != nullptr)
    {
        mwinFinishDrop(context, report->window.index1 - 1, report->data.drop.position,
                       report->timeNs);
    }
    else
    {
        mwinBeginDrop(context);
    }
}

static void Pump(mwinContext* context)
{
    mwinTestPlatform* platform = mwinTestPlatformOf(context);
    for (uint32_t i = 0; i < platform->reportCount; i++)
    {
        const mwinEvent* report = &platform->reports[i];
        if (IsGlobal(report->type))
        {
            mwinPostGlobal(context, report);
        }
        else if (report->type == mwin_eventDropped)
        {
            // Its window may have gone: the drop still ends.
            Deliver(context, report);
        }
        else if (mwinFindWindow(context, report->window) != nullptr)
        {
            mwinPost(context, report->window.index1 - 1, report);
        }
        // A platform waits for the program to handle these before it goes
        // on, as Android does for onPause.
        if (IsLifecycle(report->type) || report->type == mwin_eventSurfaceLost ||
            report->type == mwin_eventSurfaceRestored)
        {
            mwinRunCriticalFrame(context);
        }
    }
    platform->reportCount = 0;
    platform->reportTextUsed = 0;
    platform->reportSegmentsUsed = 0;
    if (platform->hold)
    {
        return;
    }
    // Answering may queue nothing new, so the list is walked once.
    uint32_t count = platform->pendingCount;
    platform->pendingCount = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        mwinTestPending pending = platform->pending[i];
        if (!IsCurrent(context, &pending))
        {
            continue;
        }
        const mwinRequest* request = &context->windows[pending.slot].requests[pending.request];
        mwinOutcome outcome = platform->answers[request->kind];
        if (outcome == mwin_outcomeDone)
        {
            outcome = CarryOut(context, pending.slot, request);
        }
        if (request->kind == mwin_requestFileDialog)
        {
            outcome = mwinTestAnswerDialog(context, pending.slot, pending.request, outcome);
        }
        mwinComplete(context, pending.slot, pending.request, outcome);
    }
}

static mwinResult Run(mwinContext* context)
{
    return mwinRunLoop(context, Pump);
}

// The characters of the keys from Enter to Slash on a US layout, in code
// order; a space marks a key that types nothing.
static const char s_punctuation[] = "     -=[]\\\\;'`,./";

// A US layout.
static mwinKey MapKeyCode(const mwinContext* context, mwinKeyCode code)
{
    (void)context;
    if (code >= mwin_codeKeyA && code <= mwin_codeKeyZ)
    {
        return 'a' + (code - mwin_codeKeyA);
    }
    if (code >= mwin_codeDigit1 && code <= mwin_codeDigit0)
    {
        return code == mwin_codeDigit0 ? '0' : '1' + (code - mwin_codeDigit1);
    }
    if (code == mwin_codeSpace)
    {
        return ' ';
    }
    if (code >= mwin_codeMinus && code <= mwin_codeSlash)
    {
        return (mwinKey)(unsigned char)s_punctuation[code - mwin_codeEnter];
    }
    return MWIN_KEY_NAMED | code;
}

static mwinResult KeyboardLayout(const mwinContext* context, char* buffer, size_t capacity,
                                 size_t* lengthOut)
{
    (void)context;
    static const char name[] = "English (US)";
    size_t length = sizeof(name) - 1;
    return mwinGiveBytes(name, length, buffer, capacity, lengthOut);
}

static void NativeHandles(const mwinContext* context, uint32_t slot, mwinNativeHandles* out)
{
    (void)context;
    (void)slot;
    out->platform = mwin_platformTest;
}

static mwinResult RumbleGamepad(mwinContext* context, uint32_t slot, float low, float high,
                                uint32_t durationMs)
{
    mwinTestRumble* rumble = &mwinTestPlatformOf(context)->pads[slot].rumble;
    *rumble = (mwinTestRumble){low, high, durationMs, rumble->count + 1};
    return mwin_success;
}

static mwinResult RumbleTriggers(mwinContext* context, uint32_t slot, float left, float right,
                                 uint32_t durationMs)
{
    mwinTestRumble* rumble = &mwinTestPlatformOf(context)->pads[slot].triggers;
    *rumble = (mwinTestRumble){left, right, durationMs, rumble->count + 1};
    return mwin_success;
}

static mwinResult SetMotion(mwinContext* context, uint32_t slot, bool enabled)
{
    mwinTestPlatformOf(context)->pads[slot].motion = enabled;
    return mwin_success;
}

const mwinBackendOps mwinTestBackend = {
    Start,
    Stop,
    Run,
    CreateWindow,
    DestroyWindow,
    Submit,
    Now,
    MapKeyCode,
    KeyboardLayout,
    NativeHandles,
    RumbleGamepad,
    ReleaseCursor,
    RumbleTriggers,
    SetMotion,
    mwinTestKeyReachOf,
};

mwinResult mwinContext_SetTestAnswer(mwinContext* context, mwinRequestKind kind,
                                     mwinOutcome outcome)
{
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    bool cancelled = outcome == mwin_outcomeCancelled && kind != mwin_requestFileDialog;
    if (context == nullptr || kind >= MWIN_TEST_KINDS || outcome == mwin_outcomeSuperseded ||
        cancelled || outcome > mwin_outcomeFailed)
    {
        return mwinMisuse(context);
    }
    mwinTestPlatform* platform = mwinTestPlatformOf(context);
    if (platform == nullptr)
    {
        return mwin_errorUnsupported;
    }
    platform->answers[kind] = outcome;
    return mwin_success;
}

mwinResult mwinContext_HoldTestRequests(mwinContext* context, bool hold)
{
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    if (context == nullptr)
    {
        return mwinMisuse(context);
    }
    mwinTestPlatform* platform = mwinTestPlatformOf(context);
    if (platform == nullptr)
    {
        return mwin_errorUnsupported;
    }
    platform->hold = hold;
    return mwin_success;
}

// Whether a record type is one a test may report: not the core's own,
// and not what the test platform's setters report.
static bool IsReportable(mwinEventType type)
{
    return type != mwin_eventNone && type != mwin_eventWindowCreated &&
           type != mwin_eventWindowDestroyed && type != mwin_eventRequestCompleted &&
           type != mwin_eventInputStateReset &&
           (type <= mwin_eventImePreedit ||
            (type >= mwin_eventDragEntered && type <= mwin_eventDragLeft)) &&
           !(type >= mwin_eventMonitorAdded && type <= mwin_eventMonitorChanged) &&
           !(type >= mwin_eventThemeChanged && type <= mwin_eventLocaleChanged);
}

static bool HasText(mwinEventType type)
{
    return type == mwin_eventTextInput || type == mwin_eventImePreedit;
}

// A report's text and segments: mwin_errorLimit for more segments than
// MWIN_MAX_PREEDIT_SEGMENTS, mwin_errorInvalid for others ill formed.
static mwinResult CheckText(const mwinEvent* event)
{
    if (!HasText(event->type))
    {
        return mwin_success;
    }
    const mwinPreeditEvent* preedit = &event->data.preedit;
    bool composition = event->type == mwin_eventImePreedit;
    if (composition && preedit->segments == nullptr && preedit->segmentCount != 0)
    {
        return mwin_errorInvalid;
    }
    if (composition && preedit->segmentCount > MWIN_MAX_PREEDIT_SEGMENTS)
    {
        return mwin_errorLimit;
    }
    bool valid = (preedit->text != nullptr || preedit->length == 0) &&
                 muniValidateUtf8(preedit->text, preedit->length).status == muni_success;
    return valid ? mwin_success : mwin_errorInvalid;
}

// Copies a report, its text and its segments into the platform's queue.
mwinResult mwinTestQueueReport(mwinTestPlatform* platform, const mwinEvent* event)
{
    uint32_t length = HasText(event->type) ? event->data.text.length : 0;
    uint32_t segments = event->type == mwin_eventImePreedit ? event->data.preedit.segmentCount : 0;
    if (platform->reportCount == MWIN_TEST_REPORTS ||
        MWIN_TEST_REPORT_TEXT - platform->reportTextUsed < length ||
        MWIN_TEST_REPORT_SEGMENTS - platform->reportSegmentsUsed < segments)
    {
        return mwin_errorCapacity;
    }
    mwinEvent* record = &platform->reports[platform->reportCount++];
    *record = *event;
    record->timeNs = platform->timeNs;
    if (HasText(event->type))
    {
        char* copy = platform->reportText + platform->reportTextUsed;
        if (length > 0)
        {
            memcpy(copy, event->data.text.text, length);
        }
        record->data.text.text = copy;
        platform->reportTextUsed += length;
    }
    if (segments > 0)
    {
        mwinPreeditSegment* copy = platform->reportSegments + platform->reportSegmentsUsed;
        memcpy(copy, event->data.preedit.segments, segments * sizeof(mwinPreeditSegment));
        record->data.preedit.segments = copy;
        platform->reportSegmentsUsed += segments;
    }
    return mwin_success;
}

mwinResult mwinContext_PostTestEvent(mwinContext* context, const mwinEvent* event)
{
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    if (context == nullptr || event == nullptr || !IsReportable(event->type))
    {
        return mwinMisuse(context);
    }
    mwinResult checked = CheckText(event);
    if (checked != mwin_success)
    {
        return mwinRefuse(context, checked);
    }
    mwinTestPlatform* platform = mwinTestPlatformOf(context);
    if (platform == nullptr)
    {
        return mwin_errorUnsupported;
    }
    if ((!IsGlobal(event->type) && mwinFindWindow(context, event->window) == nullptr) ||
        (event->type == mwin_eventDisplayChanged &&
         mwinFindMonitor(context, event->data.monitor) < 0))
    {
        return mwin_errorStale;
    }
    return mwinTestQueueReport(platform, event);
}

mwinResult mwinContext_SetTestTime(mwinContext* context, uint64_t timeNs)
{
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    mwinTestPlatform* platform = mwinTestPlatformOf(context);
    if (context == nullptr || (platform != nullptr && timeNs < platform->timeNs))
    {
        return mwinMisuse(context);
    }
    if (platform == nullptr)
    {
        return mwin_errorUnsupported;
    }
    platform->timeNs = timeNs;
    return mwin_success;
}

mwinResult mwinContext_SetTestScale(mwinContext* context, float scale)
{
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    if (context == nullptr || !isfinite(scale) || scale <= 0.0f)
    {
        return mwinMisuse(context);
    }
    mwinTestPlatform* platform = mwinTestPlatformOf(context);
    if (platform == nullptr)
    {
        return mwin_errorUnsupported;
    }
    platform->scale = scale;
    return mwin_success;
}

mwinResult mwinWindow_GetTestTitle(const mwinContext* context, mwinWindowId window, char* bufferOut,
                                   size_t capacity, size_t* lengthOut)
{
    mwinClearOut(bufferOut, capacity);
    mwinClearOut(lengthOut, sizeof *lengthOut);
    if (context == nullptr || lengthOut == nullptr || (bufferOut == nullptr && capacity != 0))
    {
        return mwinMisuse(context);
    }
    if (mwinTestPlatformOf(context) == nullptr)
    {
        return mwin_errorUnsupported;
    }
    const mwinWindow* found = mwinFindWindow(context, window);
    if (found == nullptr)
    {
        return mwin_errorStale;
    }
    size_t length = found->titleLength;
    return mwinGiveBytes(found->title, length, bufferOut, capacity, lengthOut);
}

mwinResult mwinAddTestMonitor(mwinContext* context, const mwinMonitorInfo* info,
                              mwinMonitorId* monitorOut)
{
    mwinClearOut(monitorOut, sizeof *monitorOut);
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    if (context == nullptr || info == nullptr || monitorOut == nullptr)
    {
        return mwinMisuse(context);
    }
    if (mwinTestPlatformOf(context) == nullptr)
    {
        return mwin_errorUnsupported;
    }
    int32_t slot = mwinAddMonitor(context, info, Now(context));
    if (slot < 0)
    {
        return mwin_errorCapacity;
    }
    *monitorOut = mwinMonitorIdOf(context, (uint32_t)slot);
    return mwin_success;
}

mwinResult mwinMonitor_SetTestInfo(mwinContext* context, mwinMonitorId monitor,
                                   const mwinMonitorInfo* info)
{
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    if (context == nullptr || info == nullptr)
    {
        return mwinMisuse(context);
    }
    if (mwinTestPlatformOf(context) == nullptr)
    {
        return mwin_errorUnsupported;
    }
    int32_t slot = mwinFindMonitor(context, monitor);
    if (slot < 0)
    {
        return mwin_errorStale;
    }
    mwinChangeMonitor(context, (uint32_t)slot, info, Now(context));
    return mwin_success;
}

mwinResult mwinRemoveTestMonitor(mwinContext* context, mwinMonitorId monitor)
{
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    if (context == nullptr)
    {
        return mwinMisuse(context);
    }
    if (mwinTestPlatformOf(context) == nullptr)
    {
        return mwin_errorUnsupported;
    }
    int32_t slot = mwinFindMonitor(context, monitor);
    if (slot < 0)
    {
        return mwin_errorStale;
    }
    mwinRemoveMonitor(context, (uint32_t)slot, Now(context));
    return mwin_success;
}

mwinResult mwinContext_SetTestSystemFacts(mwinContext* context, const mwinSystemFacts* facts)
{
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    if (context == nullptr || facts == nullptr)
    {
        return mwinMisuse(context);
    }
    if (mwinTestPlatformOf(context) == nullptr)
    {
        return mwin_errorUnsupported;
    }
    mwinSetSystemFacts(context, facts, Now(context));
    return mwin_success;
}

mwinResult mwinContext_SetTestLocales(mwinContext* context, const char* locales, size_t length)
{
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    if (context == nullptr)
    {
        return mwinMisuse(context);
    }
    if (mwinTestPlatformOf(context) == nullptr)
    {
        return mwin_errorUnsupported;
    }
    if (length > context->limits.localeBytes)
    {
        return mwin_errorCapacity;
    }
    return mwinSetLocales(context, locales, length, Now(context)) ? mwin_success
                                                                  : mwinMisuse(context);
}

mwinResult mwinWindow_GetTestCursor(const mwinContext* context, mwinWindowId window,
                                    mwinCursorId* cursorOut, uint32_t* imageOut)
{
    mwinClearOut(cursorOut, sizeof *cursorOut);
    mwinClearOut(imageOut, sizeof *imageOut);
    if (context == nullptr || cursorOut == nullptr)
    {
        return mwin_errorInvalid;
    }
    const mwinTestPlatform* platform = mwinTestPlatformOf(context);
    if (platform == nullptr)
    {
        return mwin_errorUnsupported;
    }
    if (mwinFindWindow(context, window) == nullptr)
    {
        return mwin_errorStale;
    }
    const mwinTestCursor* shown = &platform->cursors[window.index1 - 1];
    *cursorOut = shown->cursor;
    if (imageOut != nullptr)
    {
        *imageOut = shown->image;
    }
    return mwin_success;
}
