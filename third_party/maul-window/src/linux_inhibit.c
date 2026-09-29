// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Keeping the display awake over the session bus.

#include "linux_inhibit.h"

#include "monotonic.h"

#include <stdio.h>
#include <string.h>

// The portal's flag for inhibiting idleness.
#define PORTAL_IDLE 8u

// How long the end waits for a call on its way, a look at a time.
#define STOP_WAIT_NS 250000000u
#define STOP_POLL_MS 10

static const char s_reason[] = "Keeping the display awake";

// The program's name, which a desktop may show beside the reason.
static void ProgramName(char* name, size_t size)
{
    FILE* file = fopen("/proc/self/comm", "r");
    size_t length = file != nullptr ? fread(name, 1, size - 1, file) : 0;
    if (file != nullptr)
    {
        (void)fclose(file);
    }
    while (length > 0 && (name[length - 1] == '\n' || name[length - 1] == '\0'))
    {
        length--;
    }
    (void)snprintf(name + length, size - length, "%s", length > 0 ? "" : "maul-window");
}

static bool AppendString(const mwinDBusApi* api, mwinDBusIter* iter, const char* text)
{
    return api->appendBasic(iter, mwin_dbusTypeString, (const void*)&text);
}

// The portal's options: the reason, as a string variant.
static bool AppendOptions(const mwinDBusApi* api, mwinDBusIter* arguments)
{
    mwinDBusIter options;
    mwinDBusIter entry;
    mwinDBusIter value;
    return api->openContainer(arguments, mwin_dbusTypeArray, "{sv}", &options) &&
           api->openContainer(&options, mwin_dbusTypeDictEntry, nullptr, &entry) &&
           AppendString(api, &entry, "reason") &&
           api->openContainer(&entry, mwin_dbusTypeVariant, "s", &value) &&
           AppendString(api, &value, s_reason) && api->closeContainer(&entry, &value) &&
           api->closeContainer(&options, &entry) && api->closeContainer(arguments, &options);
}

static bool Ask(mwinLinuxBus* bus, mwinLinuxInhibit* inhibit, mwinInhibitBy by, uint64_t nowNs)
{
    const mwinDBusApi* api = &bus->api;
    bool screenSaver = by == mwin_inhibitScreenSaver;
    DBusMessage* message = screenSaver ? mwinBusMethod(bus, "org.freedesktop.ScreenSaver",
                                                       "/org/freedesktop/ScreenSaver",
                                                       "org.freedesktop.ScreenSaver", "Inhibit")
                                       : mwinBusMethod(bus, "org.freedesktop.portal.Desktop",
                                                       "/org/freedesktop/portal/desktop",
                                                       "org.freedesktop.portal.Inhibit", "Inhibit");
    if (message == nullptr)
    {
        return false;
    }
    char name[32];
    ProgramName(name, sizeof(name));
    uint32_t flags = PORTAL_IDLE;
    mwinDBusIter arguments;
    api->iterInitAppend(message, &arguments);
    bool built =
        screenSaver ? AppendString(api, &arguments, name) && AppendString(api, &arguments, s_reason)
                    : AppendString(api, &arguments, "") &&
                          api->appendBasic(&arguments, mwin_dbusTypeUint32, (const void*)&flags) &&
                          AppendOptions(api, &arguments);
    if (!built)
    {
        api->unrefMessage(message);
        return false;
    }
    if (!mwinBusSend(bus, message, &inhibit->call, nowNs))
    {
        return false;
    }
    inhibit->asking = by;
    return true;
}

// Takes who holds the display from an answer; false for one without
// what it should carry.
static bool Held(const mwinDBusApi* api, mwinLinuxInhibit* inhibit, DBusMessage* reply)
{
    mwinDBusIter iter;
    int type = api->iterInit(reply, &iter) ? api->argType(&iter) : 0;
    if (inhibit->asking == mwin_inhibitScreenSaver && type == mwin_dbusTypeUint32)
    {
        api->getBasic(&iter, (void*)&inhibit->cookie);
        return true;
    }
    const char* handle = nullptr;
    if (inhibit->asking == mwin_inhibitPortal && type == mwin_dbusTypeObjectPath)
    {
        api->getBasic(&iter, (void*)&handle);
    }
    size_t length = handle != nullptr ? strlen(handle) : 0;
    if (length == 0 || length >= sizeof(inhibit->handle))
    {
        return false;
    }
    memcpy(inhibit->handle, handle, length + 1);
    return true;
}

// Reads the answer to a call: false while it waits.
static bool Answered(mwinLinuxBus* bus, mwinLinuxInhibit* inhibit, uint64_t nowNs)
{
    bool failed = false;
    DBusMessage* reply = mwinBusAnswer(bus, &inhibit->call, nowNs, &failed);
    if (reply == nullptr && !failed)
    {
        return false;
    }
    mwinInhibitBy asked = inhibit->asking;
    bool held = !failed && Held(&bus->api, inhibit, reply);
    if (reply != nullptr)
    {
        bus->api.unrefMessage(reply);
    }
    inhibit->asking = mwin_inhibitNone;
    inhibit->held = held ? asked : mwin_inhibitNone;
    // The portal where the screensaver would not; nothing after it.
    if (!held &&
        (asked != mwin_inhibitScreenSaver || !Ask(bus, inhibit, mwin_inhibitPortal, nowNs)))
    {
        inhibit->refused = true;
    }
    return true;
}

static void Release(mwinLinuxBus* bus, mwinLinuxInhibit* inhibit)
{
    const mwinDBusApi* api = &bus->api;
    bool screenSaver = inhibit->held == mwin_inhibitScreenSaver;
    DBusMessage* message =
        screenSaver
            ? mwinBusMethod(bus, "org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver",
                            "org.freedesktop.ScreenSaver", "UnInhibit")
            : mwinBusMethod(bus, "org.freedesktop.portal.Desktop", inhibit->handle,
                            "org.freedesktop.portal.Request", "Close");
    inhibit->held = mwin_inhibitNone;
    if (message == nullptr)
    {
        return;
    }
    mwinDBusIter arguments;
    api->iterInitAppend(message, &arguments);
    if (!screenSaver ||
        api->appendBasic(&arguments, mwin_dbusTypeUint32, (const void*)&inhibit->cookie))
    {
        mwinBusTell(bus, message);
    }
    else
    {
        api->unrefMessage(message);
    }
}

void mwinInhibitPump(mwinLinuxBus* bus, mwinLinuxInhibit* inhibit, bool wanted, uint64_t nowNs)
{
    if (wanted != inhibit->wanted)
    {
        inhibit->wanted = wanted;
        inhibit->refused = false;
    }
    // An answer may have the portal asked in the screensaver's stead.
    if (inhibit->asking != mwin_inhibitNone &&
        (!Answered(bus, inhibit, nowNs) || inhibit->asking != mwin_inhibitNone))
    {
        return;
    }
    if (wanted && inhibit->held == mwin_inhibitNone && !inhibit->refused &&
        !Ask(bus, inhibit, mwin_inhibitScreenSaver, nowNs))
    {
        inhibit->refused = true;
    }
    else if (!wanted && inhibit->held != mwin_inhibitNone)
    {
        Release(bus, inhibit);
    }
}

void mwinInhibitStop(mwinLinuxBus* bus, mwinLinuxInhibit* inhibit)
{
    // A call still on its way is waited for a moment, so what it holds
    // can be let go; one it has the portal make instead is not.
    uint64_t deadlineNs = mwinMonotonicNow() + STOP_WAIT_NS;
    while (inhibit->asking != mwin_inhibitNone && !bus->api.completed(inhibit->call.pending) &&
           mwinMonotonicNow() < deadlineNs)
    {
        mwinBusPump(bus, STOP_POLL_MS);
    }
    inhibit->wanted = false;
    if (inhibit->asking != mwin_inhibitNone)
    {
        (void)Answered(bus, inhibit, mwinMonotonicNow());
    }
    mwinBusDrop(bus, &inhibit->call);
    if (inhibit->held != mwin_inhibitNone)
    {
        Release(bus, inhibit);
        bus->api.flush(bus->connection);
    }
    *inhibit = (mwinLinuxInhibit){0};
}
