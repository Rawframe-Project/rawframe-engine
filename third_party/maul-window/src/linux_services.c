// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Linux services.

#include "linux_services.h"

#include "linux_locale.h"
#include "monotonic.h"

#include "maul-window/services.h"

#include <errno.h>
#include <spawn.h>
#include <string.h>
#include <sys/wait.h>

extern char** environ;

// How long an xdg-open run may take before it is taken to show what it
// opened.
#define OPENER_DEADLINE_NS 5000000000u

// xdg-open's status when the desktop has no tool for it.
#define XDG_NO_TOOL 3

void mwinLinuxServicesStart(mwinLinuxServices* services, mwinContext* context)
{
    *services = (mwinLinuxServices){.context = context};
    mwinDialogsStart(&services->dialogs, context, &services->bus);
    char locales[MWIN_LINUX_LOCALE_BYTES];
    size_t capacity = context->limits.localeBytes;
    size_t length =
        mwinLinuxLocales(locales, capacity < sizeof(locales) ? capacity : sizeof(locales));
    (void)mwinSetLocales(context, locales, length, mwinMonotonicNow());
    mwinSettingsStart(&services->settings, context, &services->bus, mwinMonotonicNow());
    mwinPowerStart(&services->power, context, &services->bus, mwinMonotonicNow());
}

// Runs xdg-open on a target, followed until it ends: -1, or the outcome
// when it could not start.
static int Open(mwinLinuxServices* services, const char* target, mwinServiceAnswer to)
{
    mwinLinuxOpener* opener = nullptr;
    for (size_t i = 0; i < MWIN_LINUX_OPENERS && opener == nullptr; i++)
    {
        opener = services->openers[i].pid == 0 ? &services->openers[i] : nullptr;
    }
    if (opener == nullptr)
    {
        return mwin_outcomeFailed;
    }
    char* arguments[] = {(char*)"xdg-open", (char*)target, nullptr};
    pid_t child = 0;
    int spawned = posix_spawnp(&child, arguments[0], nullptr, nullptr, arguments, environ);
    if (spawned != 0)
    {
        return spawned == ENOENT ? mwin_outcomeUnsupported : mwin_outcomeFailed;
    }
    *opener = (mwinLinuxOpener){child, to, mwinMonotonicNow() + OPENER_DEADLINE_NS};
    return -1;
}

int mwinLinuxOpenUrl(mwinLinuxServices* services, uint32_t slot, uint32_t request)
{
    const mwinRequest* entry = &services->context->windows[slot].requests[request];
    return Open(services, entry->value.text.bytes, mwinAnswerTo(services->context, slot, request));
}

// Opens the folder that holds a path.
static int OpenFolder(mwinLinuxServices* services, const mwinRequest* entry, mwinServiceAnswer to)
{
    char folder[MWIN_ADDRESS_BYTES + 1];
    const char* last = strrchr(entry->value.text.bytes, '/');
    size_t length = last > entry->value.text.bytes ? (size_t)(last - entry->value.text.bytes) : 1;
    memcpy(folder, entry->value.text.bytes, length);
    folder[length] = '\0';
    return Open(services, folder, to);
}

// A path as a file URI, each byte outside the unreserved ones and '/'
// percent-encoded.
static void FileUri(const char* path, uint32_t length, char* uri)
{
    static const char digits[] = "0123456789ABCDEF";
    memcpy(uri, "file://", 7);
    size_t at = 7;
    for (uint32_t i = 0; i < length; i++)
    {
        unsigned char c = (unsigned char)path[i];
        bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                     c == '-' || c == '.' || c == '_' || c == '~' || c == '/';
        if (plain)
        {
            uri[at++] = (char)c;
        }
        else
        {
            uri[at++] = '%';
            uri[at++] = digits[c >> 4];
            uri[at++] = digits[c & 15u];
        }
    }
    uri[at] = '\0';
}

// Asks the file manager to show the path: false when it could not ask.
static bool ShowItem(mwinLinuxServices* services, const mwinRequest* entry, mwinLinuxReveal* reveal)
{
    mwinLinuxBus* bus = &services->bus;
    DBusMessage* message =
        mwinBusMethod(bus, "org.freedesktop.FileManager1", "/org/freedesktop/FileManager1",
                      "org.freedesktop.FileManager1", "ShowItems");
    if (message == nullptr)
    {
        return false;
    }
    char uri[7 + 3 * MWIN_ADDRESS_BYTES + 1];
    FileUri(entry->value.text.bytes, entry->value.text.length, uri);
    const char* item = uri;
    const char* startup = "";
    mwinDBusIter arguments;
    mwinDBusIter items;
    bus->api.iterInitAppend(message, &arguments);
    bool built = bus->api.openContainer(&arguments, mwin_dbusTypeArray, "s", &items) &&
                 bus->api.appendBasic(&items, mwin_dbusTypeString, (const void*)&item) &&
                 bus->api.closeContainer(&arguments, &items) &&
                 bus->api.appendBasic(&arguments, mwin_dbusTypeString, (const void*)&startup);
    if (!built)
    {
        bus->api.unrefMessage(message);
        return false;
    }
    return mwinBusSend(bus, message, &reveal->call, mwinMonotonicNow());
}

int mwinLinuxRevealFile(mwinLinuxServices* services, uint32_t slot, uint32_t request)
{
    const mwinRequest* entry = &services->context->windows[slot].requests[request];
    mwinServiceAnswer to = mwinAnswerTo(services->context, slot, request);
    for (size_t i = 0; i < MWIN_LINUX_REVEALS; i++)
    {
        mwinLinuxReveal* reveal = &services->reveals[i];
        if (reveal->call.pending == nullptr)
        {
            if (ShowItem(services, entry, reveal))
            {
                reveal->to = to;
                return -1;
            }
            break;
        }
    }
    return OpenFolder(services, entry, to);
}

// The outcome of an xdg-open run that ended; one reaped by the system,
// whose status is lost, is taken as done.
static mwinOutcome Ended(int reaped, int status)
{
    if (reaped < 0 || (WIFEXITED(status) && WEXITSTATUS(status) == 0))
    {
        return mwin_outcomeDone;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == XDG_NO_TOOL ? mwin_outcomeUnsupported
                                                                   : mwin_outcomeFailed;
}

static void PumpOpeners(mwinLinuxServices* services, uint64_t nowNs)
{
    for (size_t i = 0; i < MWIN_LINUX_OPENERS; i++)
    {
        mwinLinuxOpener* opener = &services->openers[i];
        if (opener->pid == 0)
        {
            continue;
        }
        int status = 0;
        pid_t reaped = waitpid(opener->pid, &status, WNOHANG);
        if (reaped == opener->pid || (reaped < 0 && errno == ECHILD))
        {
            mwinAnswer(services->context, &opener->to, Ended(reaped, status));
            opener->pid = 0;
        }
        else if (nowNs >= opener->deadlineNs)
        {
            mwinAnswer(services->context, &opener->to, mwin_outcomeDone);
        }
    }
}

static void PumpReveals(mwinLinuxServices* services, uint64_t nowNs)
{
    for (size_t i = 0; i < MWIN_LINUX_REVEALS; i++)
    {
        mwinLinuxReveal* reveal = &services->reveals[i];
        bool failed = false;
        DBusMessage* reply = reveal->call.pending != nullptr
                                 ? mwinBusAnswer(&services->bus, &reveal->call, nowNs, &failed)
                                 : nullptr;
        if (reply != nullptr)
        {
            services->bus.api.unrefMessage(reply);
        }
        if (reply == nullptr && !failed)
        {
            continue;
        }
        const mwinContext* context = services->context;
        const mwinRequest* entry = &context->windows[reveal->to.slot].requests[reveal->to.request];
        bool waiting =
            entry->status == mwin_requestActive && entry->generation == reveal->to.generation;
        // No file manager on the bus: its folder instead.
        int outcome =
            failed && waiting ? OpenFolder(services, entry, reveal->to) : mwin_outcomeDone;
        if (outcome >= 0)
        {
            mwinAnswer(services->context, &reveal->to, (mwinOutcome)outcome);
        }
        reveal->to.waiting = false;
    }
}

mwinOutcome mwinLinuxCanKeepAwake(mwinLinuxServices* services)
{
    return mwinBusConnect(&services->bus) ? mwin_outcomeDone : mwin_outcomeUnsupported;
}

void mwinLinuxServicesPump(mwinLinuxServices* services, uint64_t nowNs, bool awake)
{
    mwinBusPump(&services->bus, 0);
    mwinSettingsPump(&services->settings, nowNs);
    mwinPowerPump(&services->power, nowNs);
    PumpReveals(services, nowNs);
    PumpOpeners(services, nowNs);
    mwinDialogsPump(&services->dialogs, nowNs);
    // Only a program that asked has a bus to keep the display awake on.
    if (services->bus.connection != nullptr)
    {
        mwinInhibitPump(&services->bus, &services->inhibit, awake, nowNs);
    }
}

void mwinLinuxServicesStop(mwinLinuxServices* services)
{
    for (size_t i = 0; i < MWIN_LINUX_REVEALS; i++)
    {
        mwinBusDrop(&services->bus, &services->reveals[i].call);
    }
    mwinDialogsStop(&services->dialogs);
    mwinSettingsStop(&services->settings);
    mwinPowerStop(&services->power);
    if (services->bus.connection != nullptr)
    {
        mwinInhibitStop(&services->bus, &services->inhibit);
    }
    mwinBusClose(&services->bus);
    // One last look, so runs that ended are not left unreaped.
    for (size_t i = 0; i < MWIN_LINUX_OPENERS; i++)
    {
        if (services->openers[i].pid != 0)
        {
            (void)waitpid(services->openers[i].pid, nullptr, WNOHANG);
        }
    }
}
