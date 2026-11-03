// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The session and system buses for the Linux services.

#include "linux_bus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define BUS "org.freedesktop.DBus"

static DBusConnection* Open(const mwinDBusApi* api, const char* address)
{
    DBusConnection* connection = api->openPrivate(address, nullptr);
    if (connection != nullptr && !api->busRegister(connection, nullptr))
    {
        api->close(connection);
        api->unrefConnection(connection);
        return nullptr;
    }
    return connection;
}

// A bus at a socket, where there is one: the user's bus in the runtime
// directory, or the system's. Anything else would have the library
// start a bus.
static DBusConnection* OpenSocket(const mwinDBusApi* api, const char* directory, const char* name)
{
    char address[512];
    struct stat status;
    int length = directory != nullptr
                     ? snprintf(address, sizeof(address), "unix:path=%s/%s", directory, name)
                     : -1;
    if (length <= 0 || (size_t)length >= sizeof(address) ||
        stat(address + sizeof("unix:path=") - 1, &status) != 0 || !S_ISSOCK(status.st_mode))
    {
        return nullptr;
    }
    return Open(api, address);
}

static DBusConnection* OpenBus(const mwinLinuxBus* bus)
{
    if (bus->system)
    {
        const char* named = getenv("DBUS_SYSTEM_BUS_ADDRESS");
        return named != nullptr && named[0] != '\0'
                   ? Open(&bus->api, named)
                   : OpenSocket(&bus->api, "/run/dbus", "system_bus_socket");
    }
    const char* named = getenv("DBUS_SESSION_BUS_ADDRESS");
    return named != nullptr && named[0] != '\0'
               ? bus->api.busGetPrivate(mwin_dbusSession, nullptr)
               : OpenSocket(&bus->api, getenv("XDG_RUNTIME_DIR"), "bus");
}

bool mwinBusConnect(mwinLinuxBus* bus)
{
    if (!bus->tried)
    {
        bus->tried = true;
        if (mwinLoadDBus(&bus->api) != mwin_success)
        {
            return false;
        }
        bus->connection = OpenBus(bus);
        if (bus->connection == nullptr)
        {
            mwinUnloadDBus(&bus->api);
            return false;
        }
        // The library ends the process when the bus goes, unless told.
        bus->api.setExitOnDisconnect(bus->connection, 0);
    }
    return bus->connection != nullptr;
}

DBusMessage* mwinBusMethod(mwinLinuxBus* bus, const char* destination, const char* path,
                           const char* interface, const char* method)
{
    return mwinBusConnect(bus) ? bus->api.newMethodCall(destination, path, interface, method)
                               : nullptr;
}

bool mwinBusQueue(mwinLinuxBus* bus, DBusMessage* message, mwinBusCall* call, uint64_t nowNs)
{
    call->pending = nullptr;
    // The library's own timeout never fires without a main loop; the
    // call's deadline stands for it.
    bool sent = bus->api.sendWithReply(bus->connection, message, &call->pending, -1) &&
                call->pending != nullptr;
    bus->api.unrefMessage(message);
    call->deadlineNs = nowNs + MWIN_BUS_DEADLINE_NS;
    return sent;
}

bool mwinBusSend(mwinLinuxBus* bus, DBusMessage* message, mwinBusCall* call, uint64_t nowNs)
{
    bool sent = mwinBusQueue(bus, message, call, nowNs);
    if (sent)
    {
        mwinBusPump(bus, 0);
    }
    return sent;
}

void mwinBusQueueTell(mwinLinuxBus* bus, DBusMessage* message)
{
    (void)bus->api.send(bus->connection, message, nullptr);
    bus->api.unrefMessage(message);
}

void mwinBusTell(mwinLinuxBus* bus, DBusMessage* message)
{
    mwinBusQueueTell(bus, message);
    mwinBusPump(bus, 0);
}

void mwinBusPump(mwinLinuxBus* bus, int waitMs)
{
    if (bus->connection == nullptr)
    {
        return;
    }
    (void)bus->api.readWrite(bus->connection, waitMs);
    // Answers complete their calls as they are dispatched; nothing
    // handles what comes unasked, and dispatching lets it go.
    while (bus->api.dispatch(bus->connection) == mwin_dbusDataRemains)
    {
    }
}

DBusMessage* mwinBusAnswer(mwinLinuxBus* bus, mwinBusCall* call, uint64_t nowNs, bool* failed)
{
    *failed = false;
    if (!bus->api.completed(call->pending))
    {
        if (nowNs >= call->deadlineNs)
        {
            mwinBusDrop(bus, call);
            *failed = true;
        }
        return nullptr;
    }
    DBusMessage* reply = bus->api.stealReply(call->pending);
    bus->api.unrefPending(call->pending);
    call->pending = nullptr;
    *failed = reply == nullptr || bus->api.messageType(reply) == mwin_dbusMessageError;
    return reply;
}

void mwinBusDrop(mwinLinuxBus* bus, mwinBusCall* call)
{
    if (call->pending != nullptr)
    {
        bus->api.cancel(call->pending);
        bus->api.unrefPending(call->pending);
        call->pending = nullptr;
    }
}

bool mwinBusEntry(const mwinDBusApi* api, mwinDBusIter* entries, const char** keyOut,
                  mwinDBusIter* valueOut)
{
    *keyOut = nullptr;
    if (api->argType(entries) != mwin_dbusTypeDictEntry)
    {
        return false;
    }
    api->recurse(entries, valueOut);
    if (api->argType(valueOut) != mwin_dbusTypeString)
    {
        return false;
    }
    api->getBasic(valueOut, (void*)keyOut);
    return api->next(valueOut);
}

void mwinBusClose(mwinLinuxBus* bus)
{
    if (bus->connection != nullptr)
    {
        bus->api.close(bus->connection);
        bus->api.unrefConnection(bus->connection);
    }
    if (bus->api.library != nullptr)
    {
        mwinUnloadDBus(&bus->api);
    }
    *bus = (mwinLinuxBus){0};
}

// Keeps a unique name, or none for one too long.
static void Keep(mwinBusOwner* owner, const char* unique)
{
    size_t length = unique != nullptr ? strlen(unique) : 0;
    length = length < sizeof(owner->unique) ? length : 0;
    memcpy(owner->unique, unique != nullptr ? unique : "", length);
    owner->unique[length] = '\0';
}

void mwinBusFollow(mwinLinuxBus* bus, mwinBusOwner* owner, const char* name, uint64_t nowNs)
{
    *owner = (mwinBusOwner){.name = name};
    char rule[256];
    (void)snprintf(rule, sizeof(rule),
                   "type='signal',sender='" BUS "',interface='" BUS "',member='NameOwnerChanged',"
                   "arg0='%s'",
                   name);
    const char* text = rule;
    mwinDBusIter arguments;
    DBusMessage* match = mwinBusMethod(bus, BUS, "/org/freedesktop/DBus", BUS, "AddMatch");
    if (match != nullptr)
    {
        bus->api.iterInitAppend(match, &arguments);
        (void)bus->api.appendBasic(&arguments, mwin_dbusTypeString, (const void*)&text);
        mwinBusQueueTell(bus, match);
    }
    DBusMessage* ask = mwinBusMethod(bus, BUS, "/org/freedesktop/DBus", BUS, "GetNameOwner");
    if (ask != nullptr)
    {
        bus->api.iterInitAppend(ask, &arguments);
        if (bus->api.appendBasic(&arguments, mwin_dbusTypeString, (const void*)&name))
        {
            (void)mwinBusQueue(bus, ask, &owner->call, nowNs);
        }
        else
        {
            bus->api.unrefMessage(ask);
        }
    }
}

bool mwinBusOwnerChanged(mwinLinuxBus* bus, mwinBusOwner* owner, DBusMessage* message)
{
    const mwinDBusApi* api = &bus->api;
    const char* sender = api->sender(message);
    if (owner->name == nullptr || !api->isSignal(message, BUS, "NameOwnerChanged") ||
        sender == nullptr || strcmp(sender, BUS) != 0)
    {
        return false;
    }
    // NameOwnerChanged(s name, s old, s new).
    const char* names[3] = {nullptr, nullptr, nullptr};
    mwinDBusIter arguments;
    bool read = api->iterInit(message, &arguments);
    for (size_t i = 0; i < 3 && read; i++)
    {
        read = api->argType(&arguments) == mwin_dbusTypeString;
        if (read)
        {
            api->getBasic(&arguments, (void*)&names[i]);
            (void)api->next(&arguments);
        }
    }
    if (!read || strcmp(names[0], owner->name) != 0)
    {
        return false;
    }
    mwinBusDrop(bus, &owner->call);
    Keep(owner, names[2]);
    return true;
}

bool mwinBusFromOwner(mwinLinuxBus* bus, mwinBusOwner* owner, DBusMessage* message)
{
    const mwinDBusApi* api = &bus->api;
    if (owner->call.pending != nullptr && api->completed(owner->call.pending))
    {
        bool failed = false;
        DBusMessage* reply = mwinBusAnswer(bus, &owner->call, 0, &failed);
        mwinDBusIter arguments;
        const char* unique = nullptr;
        // An error says the name has no owner.
        if (reply != nullptr && !failed && api->iterInit(reply, &arguments) &&
            api->argType(&arguments) == mwin_dbusTypeString)
        {
            api->getBasic(&arguments, (void*)&unique);
        }
        Keep(owner, unique);
        if (reply != nullptr)
        {
            api->unrefMessage(reply);
        }
    }
    const char* sender = api->sender(message);
    return owner->unique[0] != '\0' && sender != nullptr && strcmp(sender, owner->unique) == 0;
}

void mwinBusUnfollow(mwinLinuxBus* bus, mwinBusOwner* owner)
{
    mwinBusDrop(bus, &owner->call);
    owner->unique[0] = '\0';
}
