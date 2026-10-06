// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The AT-SPI adapter's objects (record mui-0008): the application's root
// and every node of its windows, found from their paths, written as
// references, and answering the Accessible interface and introspection.

#include "allocator.h"
#include "atspi.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define ERROR_UNKNOWN_OBJECT "org.freedesktop.DBus.Error.UnknownObject"
#define ERROR_UNKNOWN_METHOD "org.freedesktop.DBus.Error.UnknownMethod"
#define ERROR_INVALID_ARGS   "org.freedesktop.DBus.Error.InvalidArgs"
#define ERROR_NO_MEMORY      "org.freedesktop.DBus.Error.NoMemory"

#define INTERFACE_ACCESSIBLE  "org.a11y.atspi.Accessible"
#define INTERFACE_APPLICATION "org.a11y.atspi.Application"
#define INTERFACE_COMPONENT   "org.a11y.atspi.Component"
#define INTERFACE_PROPERTIES  "org.freedesktop.DBus.Properties"
#define INTERFACE_INTROSPECT  "org.freedesktop.DBus.Introspectable"

// AT-SPI's application role.
#define ROLE_APPLICATION 75

// Reads digits of a base from a path into a number; the end, or NULL
// for none or an overflow.
static const char* ReadNumber(const char* at, unsigned base, uint64_t* out)
{
    uint64_t value = 0;
    const char* start = at;
    for (;; at++)
    {
        unsigned digit = *at >= '0' && *at <= '9'   ? (unsigned)(*at - '0')
                         : *at >= 'a' && *at <= 'f' ? (unsigned)(*at - 'a') + 10
                                                    : base;
        if (digit >= base)
        {
            break;
        }
        if (value > (UINT64_MAX - digit) / base)
        {
            return nullptr;
        }
        value = value * base + digit;
    }
    *out = value;
    return at != start ? at : nullptr;
}

bool muiAtspiFind(const muiAtspiApp* app, const char* path, muiAtspiObject* objectOut)
{
    *objectOut = (muiAtspiObject){0};
    if (strcmp(path, ATSPI_ROOT_PATH) == 0)
    {
        return true;
    }
    // w<window>n<id in hexadecimal>
    const char* at = path + strlen(ATSPI_PREFIX);
    uint64_t window = 0;
    uint64_t id = 0;
    at = at[0] == 'w' ? ReadNumber(at + 1, 10, &window) : nullptr;
    at = at != nullptr && at[0] == 'n' ? ReadNumber(at + 1, 16, &id) : nullptr;
    if (at == nullptr || at[0] != '\0')
    {
        return false;
    }
    for (uint32_t i = 0; i < app->windowCount; i++)
    {
        muiAtspiAdapter* adapter = app->windows[i];
        if (adapter->window == window)
        {
            *objectOut = (muiAtspiObject){adapter, muiAccessTree_Find(adapter->tree, id)};
            return objectOut->node != nullptr;
        }
    }
    return false;
}

void muiAtspiSend(muiAtspiApp* app, DBusMessage* call, DBusMessage* reply)
{
    const muiDBusApi* dbus = &app->dbus;
    if (reply == nullptr)
    {
        reply = dbus->newError(call, ERROR_NO_MEMORY, "out of memory");
    }
    if (reply != nullptr)
    {
        (void)dbus->send(app->connection, reply, nullptr);
        dbus->unrefMessage(reply);
    }
}

static void SendError(muiAtspiApp* app, DBusMessage* call, const char* name, const char* text)
{
    muiAtspiSend(app, call, app->dbus.newError(call, name, text));
}

bool muiAtspiAppendString(muiAtspiApp* app, muiDBusIter* iter, const char* text)
{
    return app->dbus.appendBasic(iter, mui_dbusTypeString, (const void*)&text);
}

bool muiAtspiAppendVariant(muiAtspiApp* app, muiDBusIter* iter, int type, const void* value)
{
    const muiDBusApi* dbus = &app->dbus;
    const char signature[2] = {(char)type, '\0'};
    muiDBusIter variant;
    return dbus->openContainer(iter, mui_dbusTypeVariant, signature, &variant) &&
           dbus->appendBasic(&variant, type, value) && dbus->closeContainer(iter, &variant);
}

// A reference by bus name and path.
static bool AppendNamed(muiAtspiApp* app, muiDBusIter* iter, const char* name, const char* path)
{
    const muiDBusApi* dbus = &app->dbus;
    muiDBusIter reference;
    return dbus->openContainer(iter, mui_dbusTypeStruct, nullptr, &reference) &&
           dbus->appendBasic(&reference, mui_dbusTypeString, (const void*)&name) &&
           dbus->appendBasic(&reference, mui_dbusTypeObjectPath, (const void*)&path) &&
           dbus->closeContainer(iter, &reference);
}

bool muiAtspiAppendReference(muiAtspiApp* app, muiDBusIter* iter, const muiAtspiObject* object)
{
    const char* name = app->dbus.uniqueName(app->connection);
    if (object->node == nullptr)
    {
        return AppendNamed(app, iter, name, ATSPI_ROOT_PATH);
    }
    char path[ATSPI_PATH_SIZE];
    (void)snprintf(path, sizeof(path), "%sw%" PRIu32 "n%" PRIx64, ATSPI_PREFIX,
                   object->adapter->window, object->node->id);
    return AppendNamed(app, iter, name, path);
}

// The desktop the root is embedded in, or the null object before the
// registry answers.
static bool AppendDesktop(muiAtspiApp* app, muiDBusIter* iter)
{
    return app->registered
               ? AppendNamed(app, iter, app->desktopName, app->desktopPath)
               : AppendNamed(app, iter, app->dbus.uniqueName(app->connection), ATSPI_NULL_PATH);
}

bool muiAtspiAppendNull(muiAtspiApp* app, muiDBusIter* iter)
{
    return AppendNamed(app, iter, app->dbus.uniqueName(app->connection), ATSPI_NULL_PATH);
}

muiAtspiObject muiAtspiObjectOf(muiAtspiAdapter* adapter, uint64_t id)
{
    return (muiAtspiObject){adapter, muiAccessTree_Find(adapter->tree, id)};
}

muiAtspiObject muiAtspiParentOf(const muiAtspiObject* object)
{
    uint64_t parent = muiAccessTree_GetShownParent(object->adapter->tree, object->node->id);
    return parent != 0 ? muiAtspiObjectOf(object->adapter, parent) : (muiAtspiObject){0};
}

uint32_t muiAtspiChildrenOf(const muiAtspiObject* object, const uint64_t** idsOut)
{
    muiAtspiAdapter* adapter = object->adapter;
    uint32_t count = 0;
    if (muiAccessTree_GetShownChildren(adapter->tree, object->node->id, adapter->scratch,
                                       adapter->nodes, &count) != mui_success)
    {
        count = 0;
    }
    *idsOut = adapter->scratch;
    return count;
}

// The application root's children: its windows with a tree; the window
// at an index, or how many with index UINT32_MAX.
static uint32_t WindowAt(const muiAtspiApp* app, uint32_t index, muiAtspiObject* objectOut)
{
    *objectOut = (muiAtspiObject){0};
    uint32_t count = 0;
    for (uint32_t i = 0; i < app->windowCount; i++)
    {
        muiAtspiAdapter* adapter = app->windows[i];
        uint64_t root = muiAccessTree_GetRoot(adapter->tree);
        if (root != 0 && count++ == index)
        {
            *objectOut = muiAtspiObjectOf(adapter, root);
            return index;
        }
    }
    return count;
}

uint32_t muiAtspiChildCount(muiAtspiApp* app, const muiAtspiObject* object)
{
    muiAtspiObject unused;
    const uint64_t* ids = nullptr;
    return object->node == nullptr ? WindowAt(app, UINT32_MAX, &unused)
                                   : muiAtspiChildrenOf(object, &ids);
}

// The child at an index; false past the last.
static bool ChildAt(muiAtspiApp* app, const muiAtspiObject* object, uint32_t index,
                    muiAtspiObject* childOut)
{
    if (object->node == nullptr)
    {
        return WindowAt(app, index, childOut) == index && childOut->node != nullptr;
    }
    const uint64_t* ids = nullptr;
    uint32_t count = muiAtspiChildrenOf(object, &ids);
    if (index >= count)
    {
        return false;
    }
    *childOut = muiAtspiObjectOf(object->adapter, ids[index]);
    return true;
}

int32_t muiAtspiIndexInParent(muiAtspiApp* app, const muiAtspiObject* object)
{
    if (object->node == nullptr)
    {
        return -1;
    }
    muiAtspiObject parent = muiAtspiParentOf(object);
    uint32_t count = muiAtspiChildCount(app, &parent);
    for (uint32_t i = 0; i < count; i++)
    {
        muiAtspiObject child;
        if (ChildAt(app, &parent, i, &child) && child.node == object->node)
        {
            return (int32_t)i;
        }
    }
    return -1;
}

bool muiAtspiAppendParent(muiAtspiApp* app, muiDBusIter* iter, const muiAtspiObject* object)
{
    if (object->node == nullptr)
    {
        return AppendDesktop(app, iter);
    }
    muiAtspiObject parent = muiAtspiParentOf(object);
    return muiAtspiAppendReference(app, iter, &parent);
}

// The interfaces an object has.
static const char* const s_rootInterfaces[] = {INTERFACE_ACCESSIBLE, INTERFACE_APPLICATION};
static const char* const s_nodeInterfaces[] = {INTERFACE_ACCESSIBLE, INTERFACE_COMPONENT};

static const char* const* InterfacesOf(const muiAtspiObject* object, uint32_t* countOut)
{
    *countOut = 2;
    return object->node == nullptr ? s_rootInterfaces : s_nodeInterfaces;
}

static void Introspect(muiAtspiApp* app, DBusMessage* call, const muiAtspiObject* object)
{
    char xml[1024];
    size_t length = (size_t)snprintf(
        xml, sizeof(xml),
        "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
        "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n<node>\n"
        "  <interface name=\"" INTERFACE_INTROSPECT "\"/>\n"
        "  <interface name=\"" INTERFACE_PROPERTIES "\"/>\n");
    uint32_t count = 0;
    const char* const* interfaces = InterfacesOf(object, &count);
    for (uint32_t i = 0; i < count && length < sizeof(xml); i++)
    {
        length += (size_t)snprintf(xml + length, sizeof(xml) - length,
                                   "  <interface name=\"%s\"/>\n", interfaces[i]);
    }
    if (length < sizeof(xml))
    {
        (void)snprintf(xml + length, sizeof(xml) - length, "</node>\n");
    }
    DBusMessage* reply = app->dbus.newMethodReturn(call);
    muiDBusIter iter;
    if (reply != nullptr)
    {
        app->dbus.iterInitAppend(reply, &iter);
        if (!muiAtspiAppendString(app, &iter, xml))
        {
            app->dbus.unrefMessage(reply);
            reply = nullptr;
        }
    }
    muiAtspiSend(app, call, reply);
}

// Reads a call's first argument of a type; false for another.
static bool ReadFirst(const muiDBusApi* dbus, DBusMessage* call, int type, void* value)
{
    muiDBusIter iter;
    if (!dbus->iterInit(call, &iter) || dbus->argType(&iter) != type)
    {
        return false;
    }
    dbus->getBasic(&iter, value);
    return true;
}

static bool AppendChildren(muiAtspiApp* app, muiDBusIter* iter, const muiAtspiObject* object)
{
    const muiDBusApi* dbus = &app->dbus;
    muiDBusIter array;
    bool ok = dbus->openContainer(iter, mui_dbusTypeArray, "(so)", &array);
    uint32_t count = muiAtspiChildCount(app, object);
    for (uint32_t i = 0; i < count && ok; i++)
    {
        muiAtspiObject child;
        ok = ChildAt(app, object, i, &child) && muiAtspiAppendReference(app, &array, &child);
    }
    return ok && dbus->closeContainer(iter, &array);
}

static bool AppendInterfaces(muiAtspiApp* app, muiDBusIter* iter, const muiAtspiObject* object)
{
    const muiDBusApi* dbus = &app->dbus;
    muiDBusIter array;
    uint32_t count = 0;
    const char* const* interfaces = InterfacesOf(object, &count);
    bool ok = dbus->openContainer(iter, mui_dbusTypeArray, "s", &array);
    for (uint32_t i = 0; i < count && ok; i++)
    {
        ok = muiAtspiAppendString(app, &array, interfaces[i]);
    }
    return ok && dbus->closeContainer(iter, &array);
}

static bool AppendStates(muiAtspiApp* app, muiDBusIter* iter, const muiAtspiObject* object)
{
    const muiDBusApi* dbus = &app->dbus;
    uint32_t states[2] = {0, 0};
    if (object->node != nullptr)
    {
        muiAtspiStatesOf(object->adapter, object->node, states);
    }
    muiDBusIter array;
    return dbus->openContainer(iter, mui_dbusTypeArray, "u", &array) &&
           dbus->appendBasic(&array, mui_dbusTypeUint32, &states[0]) &&
           dbus->appendBasic(&array, mui_dbusTypeUint32, &states[1]) &&
           dbus->closeContainer(iter, &array);
}

// An empty array of a signature: no relations or attributes yet.
static bool AppendEmpty(muiAtspiApp* app, muiDBusIter* iter, const char* signature)
{
    muiDBusIter array;
    return app->dbus.openContainer(iter, mui_dbusTypeArray, signature, &array) &&
           app->dbus.closeContainer(iter, &array);
}

// Writes the answer of an Accessible method; false for an unknown one,
// with *ok false when memory ran out.
static bool AppendAccessible(muiAtspiApp* app, muiDBusIter* iter, DBusMessage* call,
                             const muiAtspiObject* object, const char* member, bool* ok)
{
    uint32_t role = object->node != nullptr ? muiAtspiRoleOf(object->node) : ROLE_APPLICATION;
    int32_t index = 0;
    muiAtspiObject child;
    if (strcmp(member, "GetChildAtIndex") == 0)
    {
        *ok = ReadFirst(&app->dbus, call, mui_dbusTypeInt32, &index) && index >= 0 &&
              ChildAt(app, object, (uint32_t)index, &child) &&
              muiAtspiAppendReference(app, iter, &child);
    }
    else if (strcmp(member, "GetChildren") == 0)
    {
        *ok = AppendChildren(app, iter, object);
    }
    else if (strcmp(member, "GetIndexInParent") == 0)
    {
        index = muiAtspiIndexInParent(app, object);
        *ok = app->dbus.appendBasic(iter, mui_dbusTypeInt32, &index);
    }
    else if (strcmp(member, "GetRole") == 0)
    {
        *ok = app->dbus.appendBasic(iter, mui_dbusTypeUint32, &role);
    }
    else if (strcmp(member, "GetRoleName") == 0 || strcmp(member, "GetLocalizedRoleName") == 0)
    {
        *ok = muiAtspiAppendString(app, iter, muiAtspiRoleName(role));
    }
    else if (strcmp(member, "GetState") == 0)
    {
        *ok = AppendStates(app, iter, object);
    }
    else if (strcmp(member, "GetRelationSet") == 0)
    {
        *ok = AppendEmpty(app, iter, "(ua(so))");
    }
    else if (strcmp(member, "GetAttributes") == 0)
    {
        *ok = AppendEmpty(app, iter, "{ss}");
    }
    else if (strcmp(member, "GetApplication") == 0)
    {
        *ok = muiAtspiAppendReference(app, iter, &(muiAtspiObject){0});
    }
    else if (strcmp(member, "GetInterfaces") == 0)
    {
        *ok = AppendInterfaces(app, iter, object);
    }
    else
    {
        return false;
    }
    return true;
}

static void AnswerAccessible(muiAtspiApp* app, DBusMessage* call, const muiAtspiObject* object,
                             const char* member)
{
    DBusMessage* reply = app->dbus.newMethodReturn(call);
    if (reply == nullptr)
    {
        muiAtspiSend(app, call, nullptr);
        return;
    }
    muiDBusIter iter;
    app->dbus.iterInitAppend(reply, &iter);
    bool ok = true;
    if (!AppendAccessible(app, &iter, call, object, member, &ok) || !ok)
    {
        app->dbus.unrefMessage(reply);
        bool known = !ok;
        SendError(app, call, known ? ERROR_INVALID_ARGS : ERROR_UNKNOWN_METHOD,
                  known ? "invalid arguments" : member);
        return;
    }
    muiAtspiSend(app, call, reply);
}

bool muiAtspiAnswer(muiAtspiApp* app, DBusMessage* call)
{
    const muiDBusApi* dbus = &app->dbus;
    const char* path = dbus->path(call);
    if (dbus->messageType(call) != mui_dbusMethodCall || path == nullptr ||
        strncmp(path, ATSPI_PREFIX, strlen(ATSPI_PREFIX)) != 0)
    {
        return false;
    }
    muiAtspiObject object;
    const char* interface = dbus->interface(call);
    const char* member = dbus->member(call);
    if (!muiAtspiFind(app, path, &object))
    {
        SendError(app, call, ERROR_UNKNOWN_OBJECT, path);
    }
    else if (interface == nullptr || member == nullptr)
    {
        SendError(app, call, ERROR_UNKNOWN_METHOD, "no interface or member");
    }
    else if (strcmp(interface, INTERFACE_INTROSPECT) == 0 && strcmp(member, "Introspect") == 0)
    {
        Introspect(app, call, &object);
    }
    else if (strcmp(interface, INTERFACE_PROPERTIES) == 0)
    {
        muiAtspiAnswerProperties(app, call, &object, member);
    }
    else if (strcmp(interface, INTERFACE_ACCESSIBLE) == 0)
    {
        AnswerAccessible(app, call, &object, member);
    }
    else if (strcmp(interface, INTERFACE_COMPONENT) != 0 || object.node == nullptr ||
             !muiAtspiAnswerComponent(app, call, &object, member))
    {
        SendError(app, call, ERROR_UNKNOWN_METHOD, member);
    }
    return true;
}
