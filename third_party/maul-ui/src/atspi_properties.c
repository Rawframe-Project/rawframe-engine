// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The AT-SPI adapter's properties (record mui-0008): the Accessible
// interface's and, on the root, the Application interface's, read one
// at a time or all at once, and the application id the registry sets.

#include "allocator.h"
#include "atspi.h"

#include <stdio.h>
#include <string.h>

#define INTERFACE_ACCESSIBLE   "org.a11y.atspi.Accessible"
#define INTERFACE_APPLICATION  "org.a11y.atspi.Application"
#define ERROR_INVALID_ARGS     "org.freedesktop.DBus.Error.InvalidArgs"
#define ERROR_UNKNOWN_PROPERTY "org.freedesktop.DBus.Error.UnknownProperty"

static const char* const s_accessible[] = {"Name",   "Description",  "Parent",  "ChildCount",
                                           "Locale", "AccessibleId", "HelpText"};
static const char* const s_application[] = {"ToolkitName",  "Version",          "ToolkitVersion",
                                            "AtspiVersion", "InterfaceVersion", "Id"};

// A node's name, or the application's, as a variant.
static bool AppendName(muiAtspiApp* app, muiDBusIter* iter, const muiAtspiObject* object)
{
    if (object->node == nullptr)
    {
        return muiAtspiAppendVariant(app, iter, mui_dbusTypeString, (const void*)&app->name);
    }
    const muiAccessTree* tree = object->adapter->tree;
    size_t length = 0;
    if (muiAccessTree_GetName(tree, object->node->id, nullptr, 0, &length) != mui_errorCapacity)
    {
        const char* none = "";
        return muiAtspiAppendVariant(app, iter, mui_dbusTypeString, (const void*)&none);
    }
    char* name = muiAllocate(&app->allocator, length + 1, 1);
    bool ok =
        name != nullptr &&
        muiAccessTree_GetName(tree, object->node->id, name, length + 1, &length) == mui_success &&
        muiAtspiAppendVariant(app, iter, mui_dbusTypeString, (const void*)&name);
    if (name != nullptr)
    {
        muiRelease(&app->allocator, name, length + 1, 1);
    }
    return ok;
}

// A text of a node's as a variant, empty for none. The record's texts
// end in a NUL.
static bool AppendText(muiAtspiApp* app, muiDBusIter* iter, const muiAtspiObject* object,
                       muiAccessTextKind kind)
{
    const char* text = object->node != nullptr && object->node->text[kind] != nullptr
                           ? object->node->text[kind]
                           : "";
    return muiAtspiAppendVariant(app, iter, mui_dbusTypeString, (const void*)&text);
}

static bool AppendParent(muiAtspiApp* app, muiDBusIter* iter, const muiAtspiObject* object)
{
    muiDBusIter variant;
    return app->dbus.openContainer(iter, mui_dbusTypeVariant, "(so)", &variant) &&
           muiAtspiAppendParent(app, &variant, object) && app->dbus.closeContainer(iter, &variant);
}

// Writes an Accessible property as a variant; false for an unknown one,
// with *ok false when memory ran out.
static bool AppendAccessible(muiAtspiApp* app, muiDBusIter* iter, const muiAtspiObject* object,
                             const char* name, bool* ok)
{
    const char* none = "";
    if (strcmp(name, "Name") == 0)
    {
        *ok = AppendName(app, iter, object);
    }
    else if (strcmp(name, "Description") == 0)
    {
        *ok = AppendText(app, iter, object, mui_accessDescription);
    }
    else if (strcmp(name, "Parent") == 0)
    {
        *ok = AppendParent(app, iter, object);
    }
    else if (strcmp(name, "ChildCount") == 0)
    {
        int32_t count = (int32_t)muiAtspiChildCount(app, object);
        *ok = muiAtspiAppendVariant(app, iter, mui_dbusTypeInt32, &count);
    }
    else if (strcmp(name, "Locale") == 0 || strcmp(name, "AccessibleId") == 0 ||
             strcmp(name, "HelpText") == 0)
    {
        *ok = muiAtspiAppendVariant(app, iter, mui_dbusTypeString, (const void*)&none);
    }
    else
    {
        return false;
    }
    return true;
}

// Writes an Application property as a variant; false for an unknown one.
static bool AppendApplication(muiAtspiApp* app, muiDBusIter* iter, const char* name, bool* ok)
{
    char version[32];
    (void)snprintf(version, sizeof(version), "%d.%d.%d", MUI_VERSION_MAJOR, MUI_VERSION_MINOR,
                   MUI_VERSION_PATCH);
    const char* toolkit = "Maul UI";
    const char* atspi = "2.1";
    const char* text = strcmp(name, "ToolkitName") == 0    ? toolkit
                       : strcmp(name, "AtspiVersion") == 0 ? atspi
                       : strcmp(name, "Version") == 0 || strcmp(name, "ToolkitVersion") == 0
                           ? version
                           : nullptr;
    uint32_t interfaceVersion = 1;
    if (text != nullptr)
    {
        *ok = muiAtspiAppendVariant(app, iter, mui_dbusTypeString, (const void*)&text);
    }
    else if (strcmp(name, "InterfaceVersion") == 0)
    {
        *ok = muiAtspiAppendVariant(app, iter, mui_dbusTypeUint32, &interfaceVersion);
    }
    else if (strcmp(name, "Id") == 0)
    {
        *ok = muiAtspiAppendVariant(app, iter, mui_dbusTypeInt32, &app->id);
    }
    else
    {
        return false;
    }
    return true;
}

static bool AppendProperty(muiAtspiApp* app, muiDBusIter* iter, const muiAtspiObject* object,
                           const char* interface, const char* name, bool* ok)
{
    if (strcmp(interface, INTERFACE_ACCESSIBLE) == 0)
    {
        return AppendAccessible(app, iter, object, name, ok);
    }
    return object->node == nullptr && strcmp(interface, INTERFACE_APPLICATION) == 0 &&
           AppendApplication(app, iter, name, ok);
}

// Reads a call's strings, as many as asked; false for fewer.
static bool ReadStrings(const muiDBusApi* dbus, DBusMessage* call, const char** first,
                        const char** second, muiDBusIter* restOut)
{
    muiDBusIter iter;
    if (!dbus->iterInit(call, &iter) || dbus->argType(&iter) != mui_dbusTypeString)
    {
        return false;
    }
    dbus->getBasic(&iter, (void*)first);
    if (second == nullptr)
    {
        return true;
    }
    if (!dbus->next(&iter) || dbus->argType(&iter) != mui_dbusTypeString)
    {
        return false;
    }
    dbus->getBasic(&iter, (void*)second);
    if (restOut != nullptr)
    {
        *restOut = iter;
        return dbus->next(restOut) != 0;
    }
    return true;
}

static void Get(muiAtspiApp* app, DBusMessage* call, const muiAtspiObject* object)
{
    const muiDBusApi* dbus = &app->dbus;
    const char* interface = nullptr;
    const char* name = nullptr;
    if (!ReadStrings(dbus, call, &interface, &name, nullptr))
    {
        muiAtspiSend(app, call, dbus->newError(call, ERROR_INVALID_ARGS, "Get(ss)"));
        return;
    }
    DBusMessage* reply = dbus->newMethodReturn(call);
    muiDBusIter iter;
    bool ok = true;
    bool known = false;
    if (reply != nullptr)
    {
        dbus->iterInitAppend(reply, &iter);
        known = AppendProperty(app, &iter, object, interface, name, &ok);
    }
    if (reply != nullptr && (!known || !ok))
    {
        dbus->unrefMessage(reply);
        reply = known ? nullptr : dbus->newError(call, ERROR_UNKNOWN_PROPERTY, name);
    }
    muiAtspiSend(app, call, reply);
}

static bool AppendAll(muiAtspiApp* app, muiDBusIter* iter, const muiAtspiObject* object,
                      const char* interface)
{
    const muiDBusApi* dbus = &app->dbus;
    bool accessible = strcmp(interface, INTERFACE_ACCESSIBLE) == 0;
    bool application = object->node == nullptr && strcmp(interface, INTERFACE_APPLICATION) == 0;
    const char* const* names = accessible ? s_accessible : s_application;
    size_t count = accessible    ? sizeof(s_accessible) / sizeof(s_accessible[0])
                   : application ? sizeof(s_application) / sizeof(s_application[0])
                                 : 0;
    muiDBusIter array;
    bool ok = dbus->openContainer(iter, mui_dbusTypeArray, "{sv}", &array);
    for (size_t i = 0; i < count && ok; i++)
    {
        muiDBusIter entry;
        ok = dbus->openContainer(&array, mui_dbusTypeDictEntry, nullptr, &entry) &&
             muiAtspiAppendString(app, &entry, names[i]) &&
             AppendProperty(app, &entry, object, interface, names[i], &ok) && ok &&
             dbus->closeContainer(&array, &entry);
    }
    return ok && dbus->closeContainer(iter, &array);
}

static void GetAll(muiAtspiApp* app, DBusMessage* call, const muiAtspiObject* object)
{
    const muiDBusApi* dbus = &app->dbus;
    const char* interface = nullptr;
    if (!ReadStrings(dbus, call, &interface, nullptr, nullptr))
    {
        muiAtspiSend(app, call, dbus->newError(call, ERROR_INVALID_ARGS, "GetAll(s)"));
        return;
    }
    DBusMessage* reply = dbus->newMethodReturn(call);
    muiDBusIter iter;
    if (reply != nullptr)
    {
        dbus->iterInitAppend(reply, &iter);
        if (!AppendAll(app, &iter, object, interface))
        {
            dbus->unrefMessage(reply);
            reply = nullptr;
        }
    }
    muiAtspiSend(app, call, reply);
}

// The registry sets the application's id; every other property is read
// only.
static void Set(muiAtspiApp* app, DBusMessage* call, const muiAtspiObject* object)
{
    const muiDBusApi* dbus = &app->dbus;
    const char* interface = nullptr;
    const char* name = nullptr;
    muiDBusIter value;
    muiDBusIter inside;
    bool id = ReadStrings(dbus, call, &interface, &name, &value) && object->node == nullptr &&
              strcmp(interface, INTERFACE_APPLICATION) == 0 && strcmp(name, "Id") == 0 &&
              dbus->argType(&value) == mui_dbusTypeVariant;
    if (id)
    {
        dbus->recurse(&value, &inside);
        id = dbus->argType(&inside) == mui_dbusTypeInt32;
    }
    if (id)
    {
        dbus->getBasic(&inside, &app->id);
    }
    muiAtspiSend(app, call,
                 id ? dbus->newMethodReturn(call)
                    : dbus->newError(call, ERROR_INVALID_ARGS, "only Application.Id is set"));
}

void muiAtspiAnswerProperties(muiAtspiApp* app, DBusMessage* call, const muiAtspiObject* object,
                              const char* member)
{
    if (strcmp(member, "Get") == 0)
    {
        Get(app, call, object);
    }
    else if (strcmp(member, "GetAll") == 0)
    {
        GetAll(app, call, object);
    }
    else if (strcmp(member, "Set") == 0)
    {
        Set(app, call, object);
    }
    else
    {
        muiAtspiSend(app, call,
                     app->dbus.newError(call, "org.freedesktop.DBus.Error.UnknownMethod", member));
    }
}
