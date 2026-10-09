// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The AT-SPI adapter's Action and Value interfaces (record mui-0008): a
// node's actions listed by name and done by index, each asked of the
// host, and a range's numbers, its value set through the host.

#include "atspi.h"

#include <string.h>

// The actions AT-SPI lists, in this order, with their names; focusing
// and scrolling come through the Component interface.
typedef struct Named
{
    muiAccessAction action;
    const char* name;
} Named;

static const Named s_actions[] = {
    {mui_actionClick, "click"},         {mui_actionExpand, "expand"},
    {mui_actionCollapse, "collapse"},   {mui_actionIncrement, "increment"},
    {mui_actionDecrement, "decrement"}, {mui_actionScrollIntoView, "scroll-into-view"},
};

// The index-th action a node has; NULL past the last.
static const Named* ActionAt(const muiAccessNode* node, uint32_t index)
{
    uint32_t count = 0;
    for (size_t i = 0; i < sizeof(s_actions) / sizeof(s_actions[0]); i++)
    {
        if ((node->actions & (1u << s_actions[i].action)) != 0 && count++ == index)
        {
            return &s_actions[i];
        }
    }
    return nullptr;
}

uint32_t muiAtspiActionCount(const muiAccessNode* node)
{
    uint32_t count = 0;
    while (ActionAt(node, count) != nullptr)
    {
        count++;
    }
    return count;
}

static bool Perform(const muiAtspiObject* object, muiAccessAction action, float value)
{
    const muiAccessRequest request = {.action = action, .target = object->node->id, .value = value};
    return object->adapter->action(object->adapter->user, &request);
}

// Reads a call's index argument; NULL for none, or an index past the
// node's actions.
static const Named* IndexedAction(const muiDBusApi* dbus, DBusMessage* call,
                                  const muiAccessNode* node)
{
    muiDBusIter iter;
    int32_t index = -1;
    if (dbus->iterInit(call, &iter) && dbus->argType(&iter) == mui_dbusTypeInt32)
    {
        dbus->getBasic(&iter, &index);
    }
    return index >= 0 ? ActionAt(node, (uint32_t)index) : nullptr;
}

static bool AppendActions(muiAtspiApp* app, muiDBusIter* iter, const muiAccessNode* node)
{
    const muiDBusApi* dbus = &app->dbus;
    const char* none = "";
    const char* keys = node->text[mui_accessKeyboardShortcut] != nullptr
                           ? node->text[mui_accessKeyboardShortcut]
                           : "";
    muiDBusIter array;
    bool ok = dbus->openContainer(iter, mui_dbusTypeArray, "(sss)", &array);
    for (uint32_t i = 0; ok && ActionAt(node, i) != nullptr; i++)
    {
        muiDBusIter entry;
        ok = dbus->openContainer(&array, mui_dbusTypeStruct, nullptr, &entry) &&
             muiAtspiAppendString(app, &entry, ActionAt(node, i)->name) &&
             muiAtspiAppendString(app, &entry, none) &&
             muiAtspiAppendString(app, &entry, i == 0 ? keys : none) &&
             dbus->closeContainer(&array, &entry);
    }
    return ok && dbus->closeContainer(iter, &array);
}

// Writes the answer of an Action method; false for an unknown one, with
// *ok false for a bad index or no memory.
static bool AppendAction(muiAtspiApp* app, muiDBusIter* iter, DBusMessage* call,
                         const muiAtspiObject* object, const char* member, bool* ok)
{
    const muiAccessNode* node = object->node;
    const Named* named = IndexedAction(&app->dbus, call, node);
    const char* none = "";
    if (strcmp(member, "GetActions") == 0)
    {
        *ok = AppendActions(app, iter, node);
    }
    else if (strcmp(member, "GetName") == 0 || strcmp(member, "GetLocalizedName") == 0)
    {
        *ok = named != nullptr && muiAtspiAppendString(app, iter, named->name);
    }
    else if (strcmp(member, "GetDescription") == 0 || strcmp(member, "GetKeyBinding") == 0)
    {
        *ok = named != nullptr && muiAtspiAppendString(app, iter, none);
    }
    else if (strcmp(member, "DoAction") == 0)
    {
        muiDBusBool done = named != nullptr && Perform(object, named->action, 0.0f);
        *ok = named != nullptr && app->dbus.appendBasic(iter, mui_dbusTypeBoolean, &done);
    }
    else
    {
        return false;
    }
    return true;
}

bool muiAtspiAnswerAction(muiAtspiApp* app, DBusMessage* call, const muiAtspiObject* object,
                          const char* member)
{
    DBusMessage* reply = app->dbus.newMethodReturn(call);
    if (reply == nullptr)
    {
        muiAtspiSend(app, call, nullptr);
        return true;
    }
    muiDBusIter iter;
    app->dbus.iterInitAppend(reply, &iter);
    bool ok = true;
    if (!AppendAction(app, &iter, call, object, member, &ok))
    {
        app->dbus.unrefMessage(reply);
        return false;
    }
    if (!ok)
    {
        app->dbus.unrefMessage(reply);
        reply = app->dbus.newError(call, "org.freedesktop.DBus.Error.InvalidArgs", member);
    }
    muiAtspiSend(app, call, reply);
    return true;
}

bool muiAtspiAppendActionProperty(muiAtspiApp* app, muiDBusIter* iter, const muiAtspiObject* object,
                                  const char* name, bool* ok)
{
    if (strcmp(name, "NActions") != 0)
    {
        return false;
    }
    int32_t count = (int32_t)muiAtspiActionCount(object->node);
    *ok = muiAtspiAppendVariant(app, iter, mui_dbusTypeInt32, &count);
    return true;
}

bool muiAtspiAppendValueProperty(muiAtspiApp* app, muiDBusIter* iter, const muiAtspiObject* object,
                                 const char* name, bool* ok)
{
    const muiAccessNode* node = object->node;
    double number = strcmp(name, "MinimumValue") == 0       ? (double)node->minimum
                    : strcmp(name, "MaximumValue") == 0     ? (double)node->maximum
                    : strcmp(name, "MinimumIncrement") == 0 ? (double)node->step
                                                            : (double)node->value;
    const char* text = node->text[mui_accessValue] != nullptr ? node->text[mui_accessValue] : "";
    if (strcmp(name, "Text") == 0)
    {
        *ok = muiAtspiAppendVariant(app, iter, mui_dbusTypeString, (const void*)&text);
    }
    else if (strcmp(name, "CurrentValue") == 0 || strcmp(name, "MinimumValue") == 0 ||
             strcmp(name, "MaximumValue") == 0 || strcmp(name, "MinimumIncrement") == 0)
    {
        *ok = muiAtspiAppendVariant(app, iter, mui_dbusTypeDouble, &number);
    }
    else
    {
        return false;
    }
    return true;
}

bool muiAtspiSetValue(const muiAtspiObject* object, double value)
{
    const muiAccessNode* node = object->node;
    return value >= (double)node->minimum && value <= (double)node->maximum &&
           (node->actions & (1u << mui_actionSetValue)) != 0 &&
           Perform(object, mui_actionSetValue, (float)value);
}
