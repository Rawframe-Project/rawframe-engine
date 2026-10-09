// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// libdbus-1, opened at run time into a table the AT-SPI application
// loads for itself (record mui-0008), as Maul Window opens it: every
// Linux desktop has it, and a program that never makes the application
// never loads it. The library's headers are not needed to build: its
// types are declared here as the stable ABI it has kept since 1.0, with
// only what the adapter and its tests use.

#ifndef MAUL_UI_SRC_DBUS_API_H
#define MAUL_UI_SRC_DBUS_API_H

#include "maul-ui/base.h"

#include <stdint.h>

typedef struct DBusConnection DBusConnection;
typedef struct DBusMessage DBusMessage;
typedef struct DBusPendingCall DBusPendingCall;

// What a filter did with a message: the library's DBusHandlerResult.
typedef int muiDBusHandled;
typedef muiDBusHandled muiDBusFilter(DBusConnection* connection, DBusMessage* message, void* data);

// The library's iterator is a public struct of pointers and integers,
// 72 bytes on 64-bit systems; this holds it with room to spare.
typedef struct muiDBusIter
{
    void* opaque[16];
} muiDBusIter;

// The library's boolean.
typedef uint32_t muiDBusBool;

enum
{
    mui_dbusSession = 0,
    mui_dbusDataRemains = 0,
    mui_dbusHandled = 0,
    mui_dbusNotHandled = 1,
    mui_dbusMethodCall = 1,
    mui_dbusMethodReturn = 2,
    mui_dbusMessageError = 3,
    mui_dbusSignal = 4,
    mui_dbusTypeInvalid = 0,
    mui_dbusTypeArray = 'a',
    mui_dbusTypeBoolean = 'b',
    mui_dbusTypeDouble = 'd',
    mui_dbusTypeInt16 = 'n',
    mui_dbusTypeInt32 = 'i',
    mui_dbusTypeObjectPath = 'o',
    mui_dbusTypeString = 's',
    mui_dbusTypeStruct = 'r',
    mui_dbusTypeDictEntry = 'e',
    mui_dbusTypeUint32 = 'u',
    mui_dbusTypeVariant = 'v',
};

typedef struct muiDBusApi
{
    void* library;
    DBusConnection* (*busGetPrivate)(int type, void* error);
    DBusConnection* (*openPrivate)(const char* address, void* error);
    muiDBusBool (*busRegister)(DBusConnection* connection, void* error);
    int (*requestName)(DBusConnection* connection, const char* name, unsigned int flags,
                       void* error);
    void (*addMatch)(DBusConnection* connection, const char* rule, void* error);
    void (*setExitOnDisconnect)(DBusConnection* connection, muiDBusBool exit);
    void (*close)(DBusConnection* connection);
    void (*unrefConnection)(DBusConnection* connection);
    muiDBusBool (*getUnixFd)(DBusConnection* connection, int* fd);
    muiDBusBool (*readWrite)(DBusConnection* connection, int timeoutMs);
    int (*dispatch)(DBusConnection* connection);
    const char* (*uniqueName)(DBusConnection* connection);
    muiDBusBool (*addFilter)(DBusConnection* connection, muiDBusFilter* filter, void* data,
                             void (*release)(void* data));
    void (*removeFilter)(DBusConnection* connection, muiDBusFilter* filter, void* data);
    muiDBusBool (*send)(DBusConnection* connection, DBusMessage* message, uint32_t* serial);
    muiDBusBool (*sendWithReply)(DBusConnection* connection, DBusMessage* message,
                                 DBusPendingCall** pending, int timeoutMs);
    DBusMessage* (*sendWithReplyAndBlock)(DBusConnection* connection, DBusMessage* message,
                                          int timeoutMs, void* error);
    muiDBusBool (*completed)(DBusPendingCall* pending);
    DBusMessage* (*stealReply)(DBusPendingCall* pending);
    void (*cancel)(DBusPendingCall* pending);
    void (*unrefPending)(DBusPendingCall* pending);
    DBusMessage* (*newMethodCall)(const char* destination, const char* path, const char* interface,
                                  const char* method);
    DBusMessage* (*newMethodReturn)(DBusMessage* call);
    DBusMessage* (*newError)(DBusMessage* call, const char* name, const char* text);
    DBusMessage* (*newSignal)(const char* path, const char* interface, const char* name);
    void (*unrefMessage)(DBusMessage* message);
    int (*messageType)(DBusMessage* message);
    const char* (*path)(DBusMessage* message);
    const char* (*interface)(DBusMessage* message);
    const char* (*member)(DBusMessage* message);
    const char* (*sender)(DBusMessage* message);
    const char* (*errorName)(DBusMessage* message);
    void (*iterInitAppend)(DBusMessage* message, muiDBusIter* iter);
    muiDBusBool (*appendBasic)(muiDBusIter* iter, int type, const void* value);
    muiDBusBool (*openContainer)(muiDBusIter* iter, int type, const char* signature,
                                 muiDBusIter* sub);
    muiDBusBool (*closeContainer)(muiDBusIter* iter, muiDBusIter* sub);
    muiDBusBool (*iterInit)(DBusMessage* message, muiDBusIter* iter);
    int (*argType)(muiDBusIter* iter);
    void (*getBasic)(muiDBusIter* iter, void* value);
    muiDBusBool (*next)(muiDBusIter* iter);
    void (*recurse)(muiDBusIter* iter, muiDBusIter* sub);
} muiDBusApi;

// Opens the library and finds its functions: false where there is no
// libdbus-1.
bool muiLoadDBus(muiDBusApi* api);

#endif // MAUL_UI_SRC_DBUS_API_H
