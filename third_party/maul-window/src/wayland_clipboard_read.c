// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland clipboard's and primary selection's reads.

#include "clipboard_data.h"
#include "wayland_clipboard.h"

#include <string.h>
#include <unistd.h>

// How long a read waits for the other client to finish.
#define READ_DEADLINE_NS 5000000000u

// The type of the selection's offer a read asks for, as the offer
// spells it: its best text type, or for data the type it names; NULL
// when the offer has none.
static const char* TypeFor(const mwinWaylandClipboard* clipboard, const mwinSelectionRead* read)
{
    if (read->kind == mwin_requestPrimaryRead)
    {
        return clipboard->primarySelectionType >= 0
                   ? mwinWaylandTextTypeName(clipboard->primarySelectionType)
                   : nullptr;
    }
    if (read->kind == mwin_requestClipboardRead)
    {
        return clipboard->selectionType >= 0 ? mwinWaylandTextTypeName(clipboard->selectionType)
                                             : nullptr;
    }
    const mwinWaylandTypes* types = &clipboard->selectionTypes;
    for (uint16_t at = 0; at < types->length;)
    {
        const char* type = types->bytes + at;
        size_t length = strlen(type);
        if (mwinSameMime(type, length, read->mime, read->mimeLength))
        {
            return type;
        }
        at += (uint16_t)(length + 1);
    }
    return nullptr;
}

// Starts a read for a request, or answers it: an outcome, or -1 while a
// read, this one's or another's, is under way.
static int Start(mwinWaylandPlatform* platform, const mwinRequest* request)
{
    mwinWaylandClipboard* clipboard = &platform->clipboard;
    mwinContext* context = platform->context;
    bool primary = mwinReadsPrimary(request);
    if ((primary ? (void*)clipboard->primaryDevice : (void*)clipboard->device) == nullptr)
    {
        return mwin_outcomeUnsupported;
    }
    if ((primary ? (void*)clipboard->primarySource : (void*)clipboard->source) != nullptr)
    {
        return mwinAnswerOwnRead(context, request);
    }
    if (clipboard->reading.fd >= 0)
    {
        return -1;
    }
    mwinSelectionRead read;
    mwinBeginSelectionRead(&read, request);
    void* offer = primary ? (void*)clipboard->primarySelection : (void*)clipboard->selection;
    const char* type = offer != nullptr ? TypeFor(clipboard, &read) : nullptr;
    if (type == nullptr)
    {
        return mwinMissedSelectionRead(context, &read);
    }
    int writer = mwinWaylandOpenPipe(&clipboard->reading);
    if (writer < 0)
    {
        return mwin_outcomeFailed;
    }
    // The request carries a copy of the writing end.
    uint32_t opcode = primary ? ZWP_PRIMARY_SELECTION_OFFER_V1_RECEIVE : WL_DATA_OFFER_RECEIVE;
    (void)platform->api.proxyMarshalFlags((struct wl_proxy*)offer, opcode, nullptr,
                                          mwinWlVersion(&platform->api, offer), 0, type, writer);
    (void)close(writer);
    clipboard->read = read;
    clipboard->deadlineNs = mwinMonotonicNow() + READ_DEADLINE_NS;
    return -1;
}

static int StartFor(void* platform, const mwinRequest* request)
{
    return Start(platform, request);
}

int mwinWaylandReadSelection(mwinWaylandPlatform* platform, const mwinRequest* request)
{
    return Start(platform, request);
}

// Answers the requests the read answers, ends it, and starts the next.
static void Finish(mwinWaylandPlatform* platform, mwinOutcome outcome)
{
    mwinWaylandClipboard* clipboard = &platform->clipboard;
    mwinFinishSelectionRead(platform->context, &clipboard->read, outcome);
    mwinWaylandClosePipe(&clipboard->reading, platform->context);
    mwinStartWaitingReads(platform->context, StartFor, platform);
}

void mwinWaylandFailRead(mwinWaylandPlatform* platform)
{
    if (platform->clipboard.reading.fd >= 0)
    {
        Finish(platform, mwin_outcomeFailed);
    }
}

void mwinWaylandPumpRead(mwinWaylandPlatform* platform, uint64_t nowNs)
{
    mwinWaylandClipboard* clipboard = &platform->clipboard;
    mwinContext* context = platform->context;
    if (clipboard->reading.fd < 0)
    {
        return;
    }
    int outcome = mwinWaylandReadPipe(&clipboard->reading, context, context->limits.clipboardBytes);
    if (outcome == mwin_outcomeDone)
    {
        outcome = mwinTakeSelectionRead(context, &clipboard->read, clipboard->reading.bytes,
                                        clipboard->reading.length);
    }
    if (outcome < 0 && nowNs >= clipboard->deadlineNs)
    {
        outcome = mwin_outcomeFailed;
    }
    if (outcome >= 0)
    {
        Finish(platform, (mwinOutcome)outcome);
    }
}
