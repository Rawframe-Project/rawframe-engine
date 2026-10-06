// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The AT-SPI adapter (record mui-0008): the application, its windows,
// and what answering clients shares: finding the object a path names,
// writing references, texts and variants, and AT-SPI's roles and states.

#ifndef MAUL_UI_SRC_ATSPI_H
#define MAUL_UI_SRC_ATSPI_H

#include "dbus_api.h"

#include "maul-ui/access_atspi.h"

#include <stddef.h>
#include <stdint.h>

#define ATSPI_PREFIX    "/org/a11y/atspi/accessible/"
#define ATSPI_ROOT_PATH "/org/a11y/atspi/accessible/root"
#define ATSPI_NULL_PATH "/org/a11y/atspi/null"

// Room for any path the adapter writes: the prefix, "w", a window number,
// "n" and a 64-bit id in hexadecimal.
#define ATSPI_PATH_SIZE 72

// Room for the desktop's bus name and path, which the registry gives.
#define ATSPI_NAME_SIZE 256

struct muiAtspiApp
{
    muiAllocator allocator;
    muiDBusApi dbus;
    DBusConnection* connection;
    char* name;
    size_t nameLength;
    // Its windows, in the order they came; a window's number in paths is
    // given once and never again.
    muiAtspiAdapter** windows;
    uint32_t windowCapacity;
    uint32_t windowCount;
    uint32_t nextWindow;
    // The registry's answer to Embed while it is awaited, and the
    // desktop it gave.
    DBusPendingCall* embedding;
    char desktopName[ATSPI_NAME_SIZE];
    char desktopPath[ATSPI_NAME_SIZE];
    bool registered;
    // The id the registry gives through the Application interface.
    int32_t id;
};

struct muiAtspiAdapter
{
    muiAtspiApp* app;
    size_t blockSize;
    muiAccessTree* tree;
    uint32_t window;
    float scale;
    int32_t x;
    int32_t y;
    muiAtspiActionFunction action;
    void* user;
    // Room for any node's shown children.
    uint64_t* scratch;
    uint32_t nodes;
};

// What a path names: the application's root, or a node of a window.
typedef struct muiAtspiObject
{
    muiAtspiAdapter* adapter;
    const muiAccessNode* node;
} muiAtspiObject;

// The object a path names; false for one that does not exist (any more).
bool muiAtspiFind(const muiAtspiApp* app, const char* path, muiAtspiObject* objectOut);

// Answers a method call to one of the adapter's objects; false for a
// message that is not one.
bool muiAtspiAnswer(muiAtspiApp* app, DBusMessage* call);

// Answers the Component interface's methods; false for another member.
bool muiAtspiAnswerComponent(muiAtspiApp* app, DBusMessage* call, const muiAtspiObject* object,
                             const char* member);

// Sends a reply and lets it go; an error reply when it is NULL.
void muiAtspiSend(muiAtspiApp* app, DBusMessage* call, DBusMessage* reply);

// Writing: an object reference (so), a string, a variant holding a
// basic value, and the start and end of a container. Each is false
// when memory runs out.
bool muiAtspiAppendReference(muiAtspiApp* app, muiDBusIter* iter, const muiAtspiObject* object);
bool muiAtspiAppendString(muiAtspiApp* app, muiDBusIter* iter, const char* text);
bool muiAtspiAppendVariant(muiAtspiApp* app, muiDBusIter* iter, int type, const void* value);

// Answers the Properties interface's methods.
void muiAtspiAnswerProperties(muiAtspiApp* app, DBusMessage* call, const muiAtspiObject* object,
                              const char* member);

// An object's children and its index among its parent's; writing its
// parent, and the null object.
uint32_t muiAtspiChildCount(muiAtspiApp* app, const muiAtspiObject* object);
int32_t muiAtspiIndexInParent(muiAtspiApp* app, const muiAtspiObject* object);
bool muiAtspiAppendParent(muiAtspiApp* app, muiDBusIter* iter, const muiAtspiObject* object);
bool muiAtspiAppendNull(muiAtspiApp* app, muiDBusIter* iter);

// The object a node is, and its shown parent's: the application root
// for a window's root. A window's root may be NULL for an empty tree.
muiAtspiObject muiAtspiObjectOf(muiAtspiAdapter* adapter, uint64_t id);
muiAtspiObject muiAtspiParentOf(const muiAtspiObject* object);

// An object's shown children, written into the adapter's scratch (the
// windows' roots for the application root); how many.
uint32_t muiAtspiChildrenOf(const muiAtspiObject* object, const uint64_t** idsOut);

// AT-SPI's role of a node, and its name.
uint32_t muiAtspiRoleOf(const muiAccessNode* node);
const char* muiAtspiRoleName(uint32_t role);

// AT-SPI's state set of a node, as two words of bits.
void muiAtspiStatesOf(const muiAtspiAdapter* adapter, const muiAccessNode* node,
                      uint32_t statesOut[2]);

// A node's extents in pixels: in the window, with the window's place
// on the screen added for screen coordinates.
void muiAtspiExtentsOf(const muiAtspiAdapter* adapter, uint64_t id, bool screen,
                       int32_t extentsOut[4]);

#endif // MAUL_UI_SRC_ATSPI_H
