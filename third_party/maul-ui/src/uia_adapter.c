// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The UI Automation adapter (record mui-0008): making it, applying
// updates, handing out the root, and letting go of provider objects
// whose nodes leave.

#include "allocator.h"
#include "uia.h"

#include <stdalign.h>
#include <string.h>

#define UIA_DEF_COOKIE 0x6D756175u // "muau"

muiUiaAdapterDef muiDefaultUiaAdapterDef(void)
{
    return (muiUiaAdapterDef){.cookie = UIA_DEF_COOKIE, .nodes = 4096, .scale = 1.0f};
}

// Loads what the adapter calls of uiautomationcore.dll.
static bool Load(muiUiaFunctions* uia)
{
    uia->module = LoadLibraryW(L"uiautomationcore.dll");
    if (uia->module == nullptr)
    {
        return false;
    }
    FARPROC host = GetProcAddress(uia->module, "UiaHostProviderFromHwnd");
    FARPROC give = GetProcAddress(uia->module, "UiaReturnRawElementProvider");
    FARPROC disconnect = GetProcAddress(uia->module, "UiaDisconnectProvider");
    FARPROC listening = GetProcAddress(uia->module, "UiaClientsAreListening");
    FARPROC raise = GetProcAddress(uia->module, "UiaRaiseAutomationEvent");
    FARPROC changed = GetProcAddress(uia->module, "UiaRaiseAutomationPropertyChangedEvent");
    memcpy((void*)&uia->hostProviderFromHwnd, (const void*)&host, sizeof(host));
    memcpy((void*)&uia->returnRawElementProvider, (const void*)&give, sizeof(give));
    memcpy((void*)&uia->disconnectProvider, (const void*)&disconnect, sizeof(disconnect));
    memcpy((void*)&uia->clientsAreListening, (const void*)&listening, sizeof(listening));
    memcpy((void*)&uia->raiseEvent, (const void*)&raise, sizeof(raise));
    memcpy((void*)&uia->raisePropertyChanged, (const void*)&changed, sizeof(changed));
    if (host == nullptr || give == nullptr || disconnect == nullptr || listening == nullptr ||
        raise == nullptr || changed == nullptr)
    {
        FreeLibrary(uia->module);
        return false;
    }
    return true;
}

// Whether the thread is in a single-threaded apartment.
static bool IsApartmentThreaded(void)
{
    APTTYPE type = APTTYPE_CURRENT;
    APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
    return SUCCEEDED(CoGetApartmentType(&type, &qualifier)) &&
           (type == APTTYPE_STA || type == APTTYPE_MAINSTA);
}

static bool IsValid(const muiUiaAdapterDef* def)
{
    return def->cookie == UIA_DEF_COOKIE && muiIsAllocatorValid(&def->allocator) &&
           def->nodes != 0 && def->nodes <= ((uint32_t)1 << 24) && def->window != nullptr &&
           def->action != nullptr && def->scale > 0.0f;
}

// The adapter's block: the adapter, the map's keys and values, and the
// scratch list of ids.
static size_t BlockSize(uint32_t nodes, uint32_t mapSize)
{
    return sizeof(muiUiaAdapter) + (size_t)mapSize * (sizeof(uint64_t) + sizeof(void*)) +
           (size_t)nodes * sizeof(uint64_t);
}

static muiResult MakeTree(muiUiaAdapter* adapter, const muiUiaAdapterDef* def)
{
    muiAccessTreeDef treeDef = muiDefaultAccessTreeDef();
    treeDef.allocator = def->allocator;
    treeDef.nodes = def->nodes;
    return muiCreateAccessTree(&treeDef, &adapter->tree);
}

muiResult muiCreateUiaAdapter(const muiUiaAdapterDef* def, muiUiaAdapter** adapterOut)
{
    if (adapterOut != nullptr)
    {
        *adapterOut = nullptr;
    }
    if (def == nullptr || adapterOut == nullptr || !IsValid(def))
    {
        return mui_errorInvalid;
    }
    muiUiaFunctions uia = {0};
    if (!IsApartmentThreaded() || !Load(&uia))
    {
        return mui_errorPlatform;
    }
    // One object per node at most, the map at most half full.
    uint32_t mapSize = 2;
    while (mapSize < def->nodes * 2)
    {
        mapSize *= 2;
    }
    size_t size = BlockSize(def->nodes, mapSize);
    unsigned char* block = muiAllocate(&def->allocator, size, alignof(max_align_t));
    if (block == nullptr)
    {
        FreeLibrary(uia.module);
        return mui_errorCapacity;
    }
    muiUiaAdapter* adapter = (muiUiaAdapter*)block;
    unsigned char* keys = block + sizeof(muiUiaAdapter);
    unsigned char* values = keys + (size_t)mapSize * sizeof(uint64_t);
    *adapter = (muiUiaAdapter){
        .allocator = def->allocator,
        .blockSize = size,
        .window = (HWND)def->window,
        .scale = def->scale,
        .action = def->action,
        .user = def->user,
        .uia = uia,
        .scratch = (uint64_t*)(values + (size_t)mapSize * sizeof(void*)),
        .nodes = def->nodes,
    };
    muiIdMapInit(&adapter->objects, (uint64_t*)keys, (void**)values, mapSize);
    muiResult status = MakeTree(adapter, def);
    adapter->root = status == mui_success ? muiUiaMakeNode(adapter, 0) : nullptr;
    if (adapter->root == nullptr)
    {
        muiDestroyAccessTree(adapter->tree);
        muiRelease(&def->allocator, block, size, alignof(max_align_t));
        FreeLibrary(uia.module);
        return status != mui_success ? status : mui_errorCapacity;
    }
    *adapterOut = adapter;
    return mui_success;
}

// Detaches an object, tells UI Automation to let go of it unless in a
// sent message (where it may not call out), and drops the adapter's
// reference.
static void LetGo(muiUiaAdapter* adapter, muiUiaNode* node, bool disconnect)
{
    node->adapter = nullptr;
    if (disconnect)
    {
        (void)adapter->uia.disconnectProvider(&node->simple);
    }
    muiUiaRelease(node);
}

void muiDestroyUiaAdapter(muiUiaAdapter* adapter)
{
    if (adapter == nullptr)
    {
        return;
    }
    bool disconnect = (InSendMessageEx(nullptr) & ISMEX_SEND) == 0;
    const muiIdMap* map = &adapter->objects;
    for (uint32_t at = 0; at <= map->mask; at++)
    {
        if (map->keys[at] != 0)
        {
            LetGo(adapter, map->values[at], disconnect);
        }
    }
    LetGo(adapter, adapter->root, disconnect);
    muiDestroyAccessTree(adapter->tree);
    HMODULE module = adapter->uia.module;
    const muiAllocator allocator = adapter->allocator;
    muiRelease(&allocator, adapter, adapter->blockSize, alignof(max_align_t));
    FreeLibrary(module);
}

static void Removed(void* user, const muiAccessTree* tree, const muiAccessNode* old)
{
    (void)tree;
    muiUiaAdapter* adapter = user;
    muiUiaNode* node = muiIdMapRemove(&adapter->objects, old->id);
    if (node != nullptr)
    {
        node->adapter = nullptr;
        muiUiaRelease(node);
    }
}

muiResult muiUiaAdapter_Apply(muiUiaAdapter* adapter, const muiAccessUpdate* update)
{
    if (adapter == nullptr)
    {
        return mui_errorInvalid;
    }
    adapter->listening = adapter->uia.clientsAreListening() != FALSE;
    const muiAccessChanges changes = {adapter, muiUiaAdded, muiUiaUpdated, Removed,
                                      muiUiaFocusMoved};
    return muiAccessTree_Apply(adapter->tree, update, &changes);
}

const muiAccessTree* muiUiaAdapter_GetTree(const muiUiaAdapter* adapter)
{
    return adapter != nullptr ? adapter->tree : nullptr;
}

void* muiUiaAdapter_GetRoot(muiUiaAdapter* adapter)
{
    return adapter != nullptr ? &adapter->root->simple : nullptr;
}

bool muiUiaAdapter_HandleGetObject(muiUiaAdapter* adapter, uintptr_t wParam, intptr_t lParam,
                                   intptr_t* resultOut)
{
    if (adapter == nullptr || resultOut == nullptr || (LONG)lParam != ROOT_OBJECT_ID)
    {
        return false;
    }
    *resultOut = adapter->uia.returnRawElementProvider(adapter->window, (WPARAM)wParam,
                                                       (LPARAM)lParam, &adapter->root->simple);
    return true;
}

muiResult muiUiaAdapter_SetScale(muiUiaAdapter* adapter, float scale)
{
    if (adapter == nullptr || !(scale > 0.0f))
    {
        return mui_errorInvalid;
    }
    adapter->scale = scale;
    return mui_success;
}

muiUiaNode* muiUiaNodeOf(muiUiaAdapter* adapter, uint64_t id)
{
    const muiAccessTree* tree = adapter->tree;
    if (muiAccessTree_Find(tree, id) == nullptr)
    {
        return nullptr;
    }
    if (id == muiAccessTree_GetRoot(tree))
    {
        return adapter->root;
    }
    muiUiaNode* node = muiIdMapFind(&adapter->objects, id);
    if (node == nullptr)
    {
        node = muiUiaMakeNode(adapter, id);
        if (node != nullptr && !muiIdMapInsert(&adapter->objects, id, node))
        {
            node->adapter = nullptr;
            muiUiaRelease(node);
            node = nullptr;
        }
    }
    return node;
}

POINT muiUiaClientOrigin(const muiUiaAdapter* adapter)
{
    POINT origin = {0, 0};
    (void)ClientToScreen(adapter->window, &origin);
    return origin;
}

muiUiaRect muiUiaScreenRect(const muiUiaAdapter* adapter, uint64_t id)
{
    muiRect bounds = {0};
    if (muiAccessTree_GetBounds(adapter->tree, id, &bounds) != mui_success)
    {
        return (muiUiaRect){0};
    }
    POINT origin = muiUiaClientOrigin(adapter);
    double scale = (double)adapter->scale;
    return (muiUiaRect){(double)origin.x + (double)bounds.x * scale,
                        (double)origin.y + (double)bounds.y * scale, (double)bounds.width * scale,
                        (double)bounds.height * scale};
}

HRESULT muiUiaPerformRequest(muiUiaAdapter* adapter, const muiAccessRequest* request)
{
    return adapter->action(adapter->user, request) ? S_OK : NOT_SUPPORTED;
}

HRESULT muiUiaPerform(muiUiaAdapter* adapter, muiAccessAction action, uint64_t target)
{
    const muiAccessRequest request = {.action = action, .target = target};
    return muiUiaPerformRequest(adapter, &request);
}
