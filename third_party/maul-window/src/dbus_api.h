// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// libdbus-1, opened at run time into a table each context loads for
// itself, the first time a service asks for the session bus: every
// Linux desktop has it, and a program that asks for no service never
// loads it. The library's headers are not needed to build: its types
// are declared here as the stable ABI it has kept since 1.0, with only
// what the services use.

#ifndef MAUL_WINDOW_SRC_DBUS_API_H
#define MAUL_WINDOW_SRC_DBUS_API_H

#include "maul-window/base.h"

typedef struct DBusConnection DBusConnection;
typedef struct DBusMessage DBusMessage;
typedef struct DBusPendingCall DBusPendingCall;

// What a filter did with a message: 1 handled, 2 not; the library's
// DBusHandlerResult.
typedef int mwinDBusHandled;
typedef mwinDBusHandled mwinDBusFilter(DBusConnection* connection, DBusMessage* message,
                                       void* data);

// The library's iterator is a public struct of pointers and integers,
// 72 bytes on 64-bit systems; this holds it with room to spare.
typedef struct mwinDBusIter
{
    void* opaque[16];
} mwinDBusIter;

// The library's boolean.
typedef uint32_t mwinDBusBool;

enum
{
    mwin_dbusSession = 0,
    mwin_dbusDataRemains = 0,
    mwin_dbusMessageError = 3,
    mwin_dbusNotHandled = 1,
    mwin_dbusTypeArray = 'a',
    mwin_dbusTypeBoolean = 'b',
    mwin_dbusTypeByte = 'y',
    mwin_dbusTypeDouble = 'd',
    mwin_dbusTypeStruct = 'r',
    mwin_dbusTypeDictEntry = 'e',
    mwin_dbusTypeInt32 = 'i',
    mwin_dbusTypeObjectPath = 'o',
    mwin_dbusTypeString = 's',
    mwin_dbusTypeUint32 = 'u',
    mwin_dbusTypeUint64 = 't',
    mwin_dbusTypeVariant = 'v',
};

typedef struct mwinDBusApi
{
    void* library;
    DBusConnection* (*busGetPrivate)(int type, void* error);
    DBusConnection* (*openPrivate)(const char* address, void* error);
    mwinDBusBool (*busRegister)(DBusConnection* connection, void* error);
    void (*setExitOnDisconnect)(DBusConnection* connection, mwinDBusBool exit);
    void (*close)(DBusConnection* connection);
    void (*unrefConnection)(DBusConnection* connection);
    mwinDBusBool (*readWrite)(DBusConnection* connection, int timeoutMs);
    void (*flush)(DBusConnection* connection);
    int (*dispatch)(DBusConnection* connection);
    DBusMessage* (*newMethodCall)(const char* destination, const char* path, const char* interface,
                                  const char* method);
    void (*unrefMessage)(DBusMessage* message);
    void (*iterInitAppend)(DBusMessage* message, mwinDBusIter* iter);
    mwinDBusBool (*appendBasic)(mwinDBusIter* iter, int type, const void* value);
    mwinDBusBool (*openContainer)(mwinDBusIter* iter, int type, const char* signature,
                                  mwinDBusIter* sub);
    mwinDBusBool (*closeContainer)(mwinDBusIter* iter, mwinDBusIter* sub);
    mwinDBusBool (*appendFixedArray)(mwinDBusIter* iter, int type, const void* values, int count);
    mwinDBusBool (*addFilter)(DBusConnection* connection, mwinDBusFilter* filter, void* data,
                              void (*release)(void* data));
    void (*removeFilter)(DBusConnection* connection, mwinDBusFilter* filter, void* data);
    mwinDBusBool (*isSignal)(DBusMessage* message, const char* interface, const char* name);
    const char* (*path)(DBusMessage* message);
    const char* (*uniqueName)(DBusConnection* connection);
    mwinDBusBool (*next)(mwinDBusIter* iter);
    void (*recurse)(mwinDBusIter* iter, mwinDBusIter* sub);
    mwinDBusBool (*send)(DBusConnection* connection, DBusMessage* message, uint32_t* serial);
    mwinDBusBool (*sendWithReply)(DBusConnection* connection, DBusMessage* message,
                                  DBusPendingCall** pending, int timeoutMs);
    mwinDBusBool (*completed)(DBusPendingCall* pending);
    DBusMessage* (*stealReply)(DBusPendingCall* pending);
    void (*cancel)(DBusPendingCall* pending);
    void (*unrefPending)(DBusPendingCall* pending);
    int (*messageType)(DBusMessage* message);
    const char* (*errorName)(DBusMessage* message);
    mwinDBusBool (*iterInit)(DBusMessage* message, mwinDBusIter* iter);
    int (*argType)(mwinDBusIter* iter);
    void (*getBasic)(mwinDBusIter* iter, void* value);
} mwinDBusApi;

// Opens the library and finds its functions: mwin_success, or
// mwin_errorUnsupported where there is no libdbus-1.
MWIN_NODISCARD mwinResult mwinLoadDBus(mwinDBusApi* api);

void mwinUnloadDBus(mwinDBusApi* api);

#endif // MAUL_WINDOW_SRC_DBUS_API_H
