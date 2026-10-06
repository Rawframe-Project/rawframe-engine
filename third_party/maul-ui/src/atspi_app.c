// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The AT-SPI adapter's application and windows (record mui-0008):
// joining the accessibility bus, registering the root with the
// registry, pumping the connection without waiting, and the windows'
// adapters.

#include "allocator.h"
#include "atspi.h"

#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

#define APP_DEF_COOKIE     0x6D756170u // "muap"
#define ADAPTER_DEF_COOKIE 0x6D756177u // "muaw"

// How long making the application waits for the session bus to give the
// accessibility bus's address.
#define ADDRESS_TIMEOUT_MS 2000

// How long the registry has to answer Embed.
#define EMBED_TIMEOUT_MS 10000

// Room for the accessibility bus's address.
#define ADDRESS_SIZE 1024

muiAtspiAppDef muiDefaultAtspiAppDef(void)
{
    return (muiAtspiAppDef){.cookie = APP_DEF_COOKIE, .windows = 16};
}

muiAtspiAdapterDef muiDefaultAtspiAdapterDef(void)
{
    return (muiAtspiAdapterDef){.cookie = ADAPTER_DEF_COOKIE, .nodes = 4096, .scale = 1.0f};
}

// Copies a string a message holds; false for none, or one too long.
static bool ReadString(const muiDBusApi* dbus, muiDBusIter* iter, char* out, size_t size)
{
    if (dbus->argType(iter) != mui_dbusTypeString && dbus->argType(iter) != mui_dbusTypeObjectPath)
    {
        return false;
    }
    const char* text = nullptr;
    dbus->getBasic(iter, (void*)&text);
    size_t length = text != nullptr ? strlen(text) : size;
    if (length >= size)
    {
        return false;
    }
    memcpy(out, text, length + 1);
    return true;
}

// The accessibility bus's address: AT_SPI_BUS_ADDRESS, else the answer
// of the session bus's org.a11y.Bus.
static bool AddressOf(const muiDBusApi* dbus, char* out, size_t size)
{
    const char* named = getenv("AT_SPI_BUS_ADDRESS");
    if (named != nullptr && named[0] != '\0' && strlen(named) < size)
    {
        memcpy(out, named, strlen(named) + 1);
        return true;
    }
    DBusConnection* session = dbus->busGetPrivate(mui_dbusSession, nullptr);
    if (session == nullptr)
    {
        return false;
    }
    dbus->setExitOnDisconnect(session, 0);
    DBusMessage* call =
        dbus->newMethodCall("org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress");
    DBusMessage* reply =
        call != nullptr ? dbus->sendWithReplyAndBlock(session, call, ADDRESS_TIMEOUT_MS, nullptr)
                        : nullptr;
    muiDBusIter iter;
    bool found =
        reply != nullptr && dbus->iterInit(reply, &iter) && ReadString(dbus, &iter, out, size);
    if (reply != nullptr)
    {
        dbus->unrefMessage(reply);
    }
    if (call != nullptr)
    {
        dbus->unrefMessage(call);
    }
    dbus->close(session);
    dbus->unrefConnection(session);
    return found;
}

static muiDBusHandled Filter(DBusConnection* connection, DBusMessage* message, void* data)
{
    (void)connection;
    return muiAtspiAnswer(data, message) ? mui_dbusHandled : mui_dbusNotHandled;
}

// Asks the registry to embed the root; it answers at a later pump.
static void Embed(muiAtspiApp* app)
{
    const muiDBusApi* dbus = &app->dbus;
    DBusMessage* call = dbus->newMethodCall("org.a11y.atspi.Registry", ATSPI_ROOT_PATH,
                                            "org.a11y.atspi.Socket", "Embed");
    muiDBusIter iter;
    muiDBusIter plug;
    const char* name = dbus->uniqueName(app->connection);
    const char* path = ATSPI_ROOT_PATH;
    if (call != nullptr)
    {
        dbus->iterInitAppend(call, &iter);
        if (dbus->openContainer(&iter, mui_dbusTypeStruct, nullptr, &plug) &&
            dbus->appendBasic(&plug, mui_dbusTypeString, (const void*)&name) &&
            dbus->appendBasic(&plug, mui_dbusTypeObjectPath, (const void*)&path) &&
            dbus->closeContainer(&iter, &plug))
        {
            (void)dbus->sendWithReply(app->connection, call, &app->embedding, EMBED_TIMEOUT_MS);
        }
        dbus->unrefMessage(call);
    }
}

// Takes the registry's answer: the desktop the root is embedded in.
static void TakeEmbedding(muiAtspiApp* app)
{
    const muiDBusApi* dbus = &app->dbus;
    if (app->embedding == nullptr || !dbus->completed(app->embedding))
    {
        return;
    }
    DBusMessage* reply = dbus->stealReply(app->embedding);
    dbus->unrefPending(app->embedding);
    app->embedding = nullptr;
    muiDBusIter iter;
    muiDBusIter desktop;
    if (reply != nullptr && dbus->messageType(reply) == mui_dbusMethodReturn &&
        dbus->iterInit(reply, &iter) && dbus->argType(&iter) == mui_dbusTypeStruct)
    {
        dbus->recurse(&iter, &desktop);
        app->registered = ReadString(dbus, &desktop, app->desktopName, ATSPI_NAME_SIZE) &&
                          dbus->next(&desktop) &&
                          ReadString(dbus, &desktop, app->desktopPath, ATSPI_NAME_SIZE);
    }
    if (reply != nullptr)
    {
        dbus->unrefMessage(reply);
    }
}

static bool IsValidApp(const muiAtspiAppDef* def)
{
    return def->cookie == APP_DEF_COOKIE && muiIsAllocatorValid(&def->allocator) &&
           def->name != nullptr && def->windows != 0 && def->windows <= 4096;
}

// Joins the bus and serves the root; false when it cannot.
static bool Connect(muiAtspiApp* app)
{
    char address[ADDRESS_SIZE];
    if (!muiLoadDBus(&app->dbus) || !AddressOf(&app->dbus, address, sizeof(address)))
    {
        return false;
    }
    const muiDBusApi* dbus = &app->dbus;
    app->connection = dbus->openPrivate(address, nullptr);
    if (app->connection == nullptr)
    {
        return false;
    }
    dbus->setExitOnDisconnect(app->connection, 0);
    return dbus->busRegister(app->connection, nullptr) &&
           dbus->addFilter(app->connection, Filter, app, nullptr);
}

static size_t AppSize(uint32_t windows, size_t nameLength)
{
    return sizeof(muiAtspiApp) + windows * sizeof(muiAtspiAdapter*) + nameLength + 1;
}

muiResult muiCreateAtspiApp(const muiAtspiAppDef* def, muiAtspiApp** appOut)
{
    if (appOut != nullptr)
    {
        *appOut = nullptr;
    }
    if (def == nullptr || appOut == nullptr || !IsValidApp(def))
    {
        return mui_errorInvalid;
    }
    size_t nameLength = strlen(def->name);
    size_t size = AppSize(def->windows, nameLength);
    unsigned char* block = muiAllocate(&def->allocator, size, alignof(max_align_t));
    if (block == nullptr)
    {
        return mui_errorCapacity;
    }
    muiAtspiApp* app = (muiAtspiApp*)block;
    *app = (muiAtspiApp){
        .allocator = def->allocator,
        .windows = (muiAtspiAdapter**)(block + sizeof(muiAtspiApp)),
        .windowCapacity = def->windows,
        .nameLength = nameLength,
    };
    app->name = (char*)(app->windows + def->windows);
    memcpy(app->name, def->name, nameLength + 1);
    if (!Connect(app))
    {
        muiDestroyAtspiApp(app);
        return mui_errorPlatform;
    }
    Embed(app);
    *appOut = app;
    return mui_success;
}

void muiDestroyAtspiApp(muiAtspiApp* app)
{
    if (app == nullptr)
    {
        return;
    }
    const muiDBusApi* dbus = &app->dbus;
    if (app->embedding != nullptr)
    {
        dbus->cancel(app->embedding);
        dbus->unrefPending(app->embedding);
    }
    if (app->connection != nullptr)
    {
        dbus->removeFilter(app->connection, Filter, app);
        dbus->close(app->connection);
        dbus->unrefConnection(app->connection);
    }
    const muiAllocator allocator = app->allocator;
    muiRelease(&allocator, app, AppSize(app->windowCapacity, app->nameLength),
               alignof(max_align_t));
}

int muiAtspiApp_GetDescriptor(const muiAtspiApp* app)
{
    int descriptor = -1;
    if (app != nullptr && !app->dbus.getUnixFd(app->connection, &descriptor))
    {
        descriptor = -1;
    }
    return descriptor;
}

void muiAtspiApp_Pump(muiAtspiApp* app)
{
    if (app == nullptr)
    {
        return;
    }
    const muiDBusApi* dbus = &app->dbus;
    (void)dbus->readWrite(app->connection, 0);
    while (dbus->dispatch(app->connection) == mui_dbusDataRemains)
    {
    }
    TakeEmbedding(app);
    (void)dbus->readWrite(app->connection, 0);
}

bool muiAtspiApp_IsRegistered(const muiAtspiApp* app)
{
    return app != nullptr && app->registered;
}

static bool IsValidAdapter(const muiAtspiAdapterDef* def)
{
    return def->cookie == ADAPTER_DEF_COOKIE && def->nodes != 0 &&
           def->nodes <= ((uint32_t)1 << 24) && def->action != nullptr && def->scale > 0.0f;
}

static size_t AdapterSize(uint32_t nodes)
{
    return sizeof(muiAtspiAdapter) + (size_t)nodes * sizeof(uint64_t);
}

muiResult muiCreateAtspiAdapter(muiAtspiApp* app, const muiAtspiAdapterDef* def,
                                muiAtspiAdapter** adapterOut)
{
    if (adapterOut != nullptr)
    {
        *adapterOut = nullptr;
    }
    if (app == nullptr || def == nullptr || adapterOut == nullptr || !IsValidAdapter(def))
    {
        return mui_errorInvalid;
    }
    if (app->windowCount == app->windowCapacity)
    {
        return mui_errorCapacity;
    }
    size_t size = AdapterSize(def->nodes);
    unsigned char* block = muiAllocate(&app->allocator, size, alignof(max_align_t));
    if (block == nullptr)
    {
        return mui_errorCapacity;
    }
    muiAtspiAdapter* adapter = (muiAtspiAdapter*)block;
    *adapter = (muiAtspiAdapter){
        .app = app,
        .blockSize = size,
        .window = app->nextWindow + 1,
        .scale = def->scale,
        .action = def->action,
        .user = def->user,
        .scratch = (uint64_t*)(block + sizeof(muiAtspiAdapter)),
        .nodes = def->nodes,
    };
    muiAccessTreeDef treeDef = muiDefaultAccessTreeDef();
    treeDef.allocator = app->allocator;
    treeDef.nodes = def->nodes;
    muiResult status = muiCreateAccessTree(&treeDef, &adapter->tree);
    if (status != mui_success)
    {
        muiRelease(&app->allocator, block, size, alignof(max_align_t));
        return status;
    }
    app->nextWindow++;
    app->windows[app->windowCount++] = adapter;
    *adapterOut = adapter;
    return mui_success;
}

void muiDestroyAtspiAdapter(muiAtspiAdapter* adapter)
{
    if (adapter == nullptr)
    {
        return;
    }
    muiAtspiApp* app = adapter->app;
    uint32_t at = 0;
    while (at < app->windowCount && app->windows[at] != adapter)
    {
        at++;
    }
    for (; at + 1 < app->windowCount; at++)
    {
        app->windows[at] = app->windows[at + 1];
    }
    app->windowCount--;
    muiDestroyAccessTree(adapter->tree);
    muiRelease(&app->allocator, adapter, adapter->blockSize, alignof(max_align_t));
}

muiResult muiAtspiAdapter_Apply(muiAtspiAdapter* adapter, const muiAccessUpdate* update)
{
    return adapter != nullptr ? muiAccessTree_Apply(adapter->tree, update, nullptr)
                              : mui_errorInvalid;
}

const muiAccessTree* muiAtspiAdapter_GetTree(const muiAtspiAdapter* adapter)
{
    return adapter != nullptr ? adapter->tree : nullptr;
}

muiResult muiAtspiAdapter_SetScale(muiAtspiAdapter* adapter, float scale)
{
    if (adapter == nullptr || !(scale > 0.0f))
    {
        return mui_errorInvalid;
    }
    adapter->scale = scale;
    return mui_success;
}

void muiAtspiAdapter_SetPlace(muiAtspiAdapter* adapter, int32_t x, int32_t y)
{
    if (adapter != nullptr)
    {
        adapter->x = x;
        adapter->y = y;
    }
}
