// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The AT-SPI adapter's Component interface (record mui-0008): a node's
// extents in pixels on the screen, in the window or in its parent, the
// node under a point, and focusing and scrolling asked of the host.

#include "atspi.h"

#include <math.h>
#include <string.h>

// AT-SPI's coordinate types and the widget layer.
#define COORD_SCREEN 0
#define COORD_WINDOW 1
#define COORD_PARENT 2
#define LAYER_WIDGET 3

// A sum kept within int32's range: a client's point past it is held by
// no node either way.
static int32_t Saturated(int64_t value)
{
    return value < INT32_MIN ? INT32_MIN : (value > INT32_MAX ? INT32_MAX : (int32_t)value);
}

// A whole number of pixels within int32's range; 0 for NaN.
static int32_t Whole(float value)
{
    if (isnan(value))
    {
        return 0;
    }
    return value >= 2147483648.0f ? INT32_MAX
                                  : (value <= -2147483648.0f ? INT32_MIN : (int32_t)value);
}

void muiAtspiExtentsOf(const muiAtspiAdapter* adapter, uint64_t id, bool screen,
                       int32_t extentsOut[4])
{
    muiRect bounds = {0};
    (void)muiAccessTree_GetBounds(adapter->tree, id, &bounds);
    float scale = adapter->scale;
    // Edges rounded outward, so the extents hold the whole box.
    float left = floorf(bounds.x * scale);
    float top = floorf(bounds.y * scale);
    float right = ceilf((bounds.x + bounds.width) * scale);
    float bottom = ceilf((bounds.y + bounds.height) * scale);
    extentsOut[0] = Saturated((int64_t)Whole(left) + (screen ? adapter->x : 0));
    extentsOut[1] = Saturated((int64_t)Whole(top) + (screen ? adapter->y : 0));
    extentsOut[2] = Whole(right - left);
    extentsOut[3] = Whole(bottom - top);
}

// An object's extents in a coordinate type; the parent's are those of
// the shown parent, the application's root having none.
static void Extents(const muiAtspiObject* object, uint32_t coordinates, int32_t extentsOut[4])
{
    muiAtspiExtentsOf(object->adapter, object->node->id, coordinates == COORD_SCREEN, extentsOut);
    muiAtspiObject parent = muiAtspiParentOf(object);
    if (coordinates == COORD_PARENT && parent.node != nullptr)
    {
        int32_t origin[4];
        muiAtspiExtentsOf(object->adapter, parent.node->id, false, origin);
        extentsOut[0] = Saturated((int64_t)extentsOut[0] - origin[0]);
        extentsOut[1] = Saturated((int64_t)extentsOut[1] - origin[1]);
    }
}

// A point in a coordinate type, in the window's pixels.
static void ToWindow(const muiAtspiObject* object, uint32_t coordinates, int32_t* x, int32_t* y)
{
    if (coordinates == COORD_SCREEN)
    {
        *x = Saturated((int64_t)*x - object->adapter->x);
        *y = Saturated((int64_t)*y - object->adapter->y);
    }
    else if (coordinates == COORD_PARENT)
    {
        int32_t origin[4] = {0, 0, 0, 0};
        muiAtspiObject parent = muiAtspiParentOf(object);
        if (parent.node != nullptr)
        {
            muiAtspiExtentsOf(object->adapter, parent.node->id, false, origin);
        }
        *x = Saturated((int64_t)*x + origin[0]);
        *y = Saturated((int64_t)*y + origin[1]);
    }
}

static bool Holds(const int32_t extents[4], int32_t x, int32_t y)
{
    return x >= extents[0] && (int64_t)x < (int64_t)extents[0] + extents[2] && y >= extents[1] &&
           (int64_t)y < (int64_t)extents[1] + extents[3];
}

// The deepest shown node under a point in the window, from an object
// down: among each node's shown children, the last drawn that holds it.
// NULL when the object itself does not.
static muiAtspiObject NodeAt(const muiAtspiObject* object, int32_t x, int32_t y)
{
    int32_t extents[4];
    muiAtspiExtentsOf(object->adapter, object->node->id, false, extents);
    if (!Holds(extents, x, y))
    {
        return (muiAtspiObject){0};
    }
    muiAtspiObject at = *object;
    for (uint32_t depth = 0; depth < object->adapter->nodes; depth++)
    {
        const uint64_t* ids = nullptr;
        uint32_t count = muiAtspiChildrenOf(&at, &ids);
        uint64_t next = 0;
        for (uint32_t i = count; i > 0 && next == 0; i--)
        {
            muiAtspiExtentsOf(at.adapter, ids[i - 1], false, extents);
            next = Holds(extents, x, y) ? ids[i - 1] : 0;
        }
        if (next == 0)
        {
            break;
        }
        at = muiAtspiObjectOf(at.adapter, next);
    }
    return at;
}

// Reads the arguments (i x, i y, u coordinates).
static bool ReadPoint(const muiDBusApi* dbus, DBusMessage* call, int32_t* x, int32_t* y,
                      uint32_t* coordinates)
{
    muiDBusIter iter;
    bool ok = dbus->iterInit(call, &iter) && dbus->argType(&iter) == mui_dbusTypeInt32;
    if (ok)
    {
        dbus->getBasic(&iter, x);
        ok = dbus->next(&iter) && dbus->argType(&iter) == mui_dbusTypeInt32;
    }
    if (ok)
    {
        dbus->getBasic(&iter, y);
        ok = dbus->next(&iter) && dbus->argType(&iter) == mui_dbusTypeUint32;
    }
    if (ok)
    {
        dbus->getBasic(&iter, coordinates);
    }
    return ok && *coordinates <= COORD_PARENT;
}

static bool ReadCoordinates(const muiDBusApi* dbus, DBusMessage* call, uint32_t* coordinates)
{
    muiDBusIter iter;
    if (!dbus->iterInit(call, &iter) || dbus->argType(&iter) != mui_dbusTypeUint32)
    {
        return false;
    }
    dbus->getBasic(&iter, coordinates);
    return *coordinates <= COORD_PARENT;
}

// Writes (ii...) of some of the extents.
static bool AppendInts(muiAtspiApp* app, muiDBusIter* iter, const int32_t* values, int count)
{
    muiDBusIter tuple;
    bool ok = app->dbus.openContainer(iter, mui_dbusTypeStruct, nullptr, &tuple);
    for (int i = 0; i < count && ok; i++)
    {
        ok = app->dbus.appendBasic(&tuple, mui_dbusTypeInt32, &values[i]);
    }
    return ok && app->dbus.closeContainer(iter, &tuple);
}

static bool Act(const muiAtspiObject* object, muiAccessAction action)
{
    const muiAccessRequest request = {.action = action, .target = object->node->id};
    return object->adapter->action(object->adapter->user, &request);
}

// Writes the answer of a method that reads where the node is.
static bool AppendPlace(muiAtspiApp* app, muiDBusIter* iter, DBusMessage* call,
                        const muiAtspiObject* object, const char* member, bool* ok)
{
    uint32_t coordinates = COORD_WINDOW;
    int32_t extents[4];
    int32_t x = 0;
    int32_t y = 0;
    if (strcmp(member, "GetExtents") == 0 || strcmp(member, "GetPosition") == 0)
    {
        *ok = ReadCoordinates(&app->dbus, call, &coordinates);
        Extents(object, coordinates, extents);
        *ok = *ok && AppendInts(app, iter, extents, member[3] == 'E' ? 4 : 2);
    }
    else if (strcmp(member, "GetSize") == 0)
    {
        Extents(object, COORD_WINDOW, extents);
        *ok = AppendInts(app, iter, extents + 2, 2);
    }
    else if (strcmp(member, "Contains") == 0 || strcmp(member, "GetAccessibleAtPoint") == 0)
    {
        *ok = ReadPoint(&app->dbus, call, &x, &y, &coordinates);
        ToWindow(object, coordinates, &x, &y);
        muiAtspiObject at = NodeAt(object, x, y);
        muiDBusBool holds = at.node != nullptr;
        *ok = *ok && (member[0] == 'C' ? app->dbus.appendBasic(iter, mui_dbusTypeBoolean, &holds)
                      : holds          ? muiAtspiAppendReference(app, iter, &at)
                                       : muiAtspiAppendNull(app, iter));
    }
    else
    {
        return false;
    }
    return true;
}

// Writes the answer of a method that reads how the node is drawn, or asks
// the host to act.
static bool AppendOther(muiAtspiApp* app, muiDBusIter* iter, const muiAtspiObject* object,
                        const char* member, bool* ok)
{
    uint32_t layer = LAYER_WIDGET;
    int16_t order = -1;
    double alpha = 1.0;
    muiDBusBool done = 0;
    if (strcmp(member, "GetLayer") == 0)
    {
        *ok = app->dbus.appendBasic(iter, mui_dbusTypeUint32, &layer);
    }
    else if (strcmp(member, "GetMDIZOrder") == 0)
    {
        *ok = app->dbus.appendBasic(iter, mui_dbusTypeInt16, &order);
    }
    else if (strcmp(member, "GetAlpha") == 0)
    {
        *ok = app->dbus.appendBasic(iter, mui_dbusTypeDouble, &alpha);
    }
    else if (strcmp(member, "GrabFocus") == 0 || strcmp(member, "ScrollTo") == 0)
    {
        done = Act(object, member[0] == 'G' ? mui_actionFocus : mui_actionScrollIntoView);
        *ok = app->dbus.appendBasic(iter, mui_dbusTypeBoolean, &done);
    }
    else
    {
        return false;
    }
    return true;
}

bool muiAtspiAnswerComponent(muiAtspiApp* app, DBusMessage* call, const muiAtspiObject* object,
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
    if (!AppendPlace(app, &iter, call, object, member, &ok) &&
        !AppendOther(app, &iter, object, member, &ok))
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
