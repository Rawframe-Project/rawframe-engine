// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The session and system buses for the Linux services: private
// connections of the context's own, made the first time a service needs
// one, and method
// calls answered later, read without blocking at each pump. The
// library's own timeouts need a main loop the program does not have,
// so each call has a deadline of its own.
//
// The session bus is the one DBUS_SESSION_BUS_ADDRESS names, else the
// user's bus in XDG_RUNTIME_DIR; the system bus the one
// DBUS_SYSTEM_BUS_ADDRESS names, else the socket in /run/dbus. With
// neither there is none, and the library is never let start a bus of
// its own.

#ifndef MAUL_WINDOW_SRC_LINUX_BUS_H
#define MAUL_WINDOW_SRC_LINUX_BUS_H

#include "dbus_api.h"

// How long a call may wait for its answer.
#define MWIN_BUS_DEADLINE_NS 10000000000u

typedef struct mwinLinuxBus
{
    mwinDBusApi api;
    DBusConnection* connection;
    // The system bus, else the session bus; set before it connects.
    bool system;
    // A connection was tried, whether or not there is one.
    bool tried;
} mwinLinuxBus;

// A call on its way: its answer, when it came, and its deadline.
typedef struct mwinBusCall
{
    DBusPendingCall* pending;
    uint64_t deadlineNs;
} mwinBusCall;

// The connection, made now if it was never tried; false with none.
bool mwinBusConnect(mwinLinuxBus* bus);

// A method call to fill in; NULL when there is no bus or memory.
DBusMessage* mwinBusMethod(mwinLinuxBus* bus, const char* destination, const char* path,
                           const char* interface, const char* method);

// Sends a method call and lets it go: false when it was not sent.
bool mwinBusSend(mwinLinuxBus* bus, DBusMessage* message, mwinBusCall* call, uint64_t nowNs);

// Sends a method call whose answer does not matter, and lets it go.
void mwinBusTell(mwinLinuxBus* bus, DBusMessage* message);

// Reads and writes what the connection can, waiting up to waitMs for
// something to come, and completes the calls answered.
void mwinBusPump(mwinLinuxBus* bus, int waitMs);

// The answer to a call: NULL while it waits, else the reply, which the
// caller lets go; with *failed set for an error, a call that passed its
// deadline (answered by no reply) or one whose connection went.
DBusMessage* mwinBusAnswer(mwinLinuxBus* bus, mwinBusCall* call, uint64_t nowNs, bool* failed);

// Lets a call go unanswered.
void mwinBusDrop(mwinLinuxBus* bus, mwinBusCall* call);

// Closes the connection, which lets the calls on it go, and the library.
void mwinBusClose(mwinLinuxBus* bus);

#endif // MAUL_WINDOW_SRC_LINUX_BUS_H
