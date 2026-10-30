// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The UI Automation adapter: making it, applying updates, handing out the
// root, and letting go of provider objects whose nodes leave.

#include "allocator.h"
#include "fpenv.h"
#include "host_call.h"
#include "uia.h"

#include <stdalign.h>
#include <string.h>

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
    // Optional: without it, a text attribute not given reads as empty.
    FARPROC none = GetProcAddress(uia->module, "UiaGetReservedNotSupportedValue");
    memcpy((void*)&uia->reservedNotSupported, (const void*)&none, sizeof(none));
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
    return muiIsAllocatorValid(&def->allocator) && def->nodes != 0 && def->window != nullptr &&
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

static MUI_NOINLINE muiResult CreateUiaAdapterWorker(const muiUiaAdapterDef* def,
                                                     muiUiaAdapter** adapterOut)
{
    if (adapterOut != nullptr)
    {
        *adapterOut = nullptr;
    }
    if (def == nullptr || adapterOut == nullptr || def->cookie != MUI_UIA_ADAPTER_DEF_COOKIE)
    {
        return mui_errorInvalid;
    }
    // Another version's def may be of another layout: nothing past the
    // version is read.
    if (def->version != MUI_ABI_VERSION)
    {
        return mui_errorVersion;
    }
    if (!IsValid(def))
    {
        return mui_errorInvalid;
    }
    if (def->nodes > MUI_MAX_ACCESS_NODES)
    {
        return mui_errorLimit;
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

muiResult muiCreateUiaAdapter(const muiUiaAdapterDef* def, muiUiaAdapter** adapterOut)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = CreateUiaAdapterWorker(def, adapterOut);
    muiFloatEnvLeave(floatEnv);
    return result;
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

static void DestroyUiaAdapter(muiUiaAdapter* adapter)
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

void muiDestroyUiaAdapter(muiUiaAdapter* adapter)
{
    if (adapter == nullptr)
    {
        DestroyUiaAdapter(adapter);
        return;
    }
    if (!muiEnterMarked(&adapter->busy, &adapter->misuse))
    {
        return;
    }
    DestroyUiaAdapter(adapter);
}

uint64_t muiUiaAdapter_GetMisuse(const muiUiaAdapter* adapter)
{
    return adapter != nullptr ? adapter->misuse : 0;
}

static MUI_NOINLINE void RemovedWorker(void* user, const muiAccessTree* tree,
                                       const muiAccessNode* old)
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

static void Removed(void* user, const muiAccessTree* tree, const muiAccessNode* old)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    RemovedWorker(user, tree, old);
    muiFloatEnvLeave(floatEnv);
}

static muiResult UiaAdapterApply(muiUiaAdapter* adapter, const muiAccessUpdate* update)
{
    if (adapter == nullptr)
    {
        return mui_errorInvalid;
    }
    adapter->listening = adapter->uia.clientsAreListening() != FALSE;
    const muiAccessChanges changes = {adapter, muiUiaAdded, muiUiaUpdated,
                                      Removed, nullptr,     nullptr};
    muiResult status = muiAccessTree_Apply(adapter->tree, update, &changes);
    if (status == mui_success)
    {
        muiUiaTellFocus(adapter);
    }
    return status;
}

static MUI_NOINLINE muiResult UiaAdapterApplyWorker(muiUiaAdapter* adapter,
                                                    const muiAccessUpdate* update)
{
    if (adapter == nullptr)
    {
        return UiaAdapterApply(adapter, update);
    }
    if (!muiEnterMarked(&adapter->busy, &adapter->misuse))
    {
        return mui_errorBusy;
    }
    muiResult result = UiaAdapterApply(adapter, update);
    adapter->misuse += result == mui_errorInvalid;
    adapter->busy = false;
    return result;
}

muiResult muiUiaAdapter_Apply(muiUiaAdapter* adapter, const muiAccessUpdate* update)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = UiaAdapterApplyWorker(adapter, update);
    muiFloatEnvLeave(floatEnv);
    return result;
}

const muiAccessTree* muiUiaAdapter_GetTree(const muiUiaAdapter* adapter)
{
    return adapter != nullptr ? adapter->tree : nullptr;
}

static void* UiaAdapterGetRoot(muiUiaAdapter* adapter)
{
    return adapter != nullptr ? &adapter->root->simple : nullptr;
}

void* muiUiaAdapter_GetRoot(muiUiaAdapter* adapter)
{
    if (adapter == nullptr)
    {
        return UiaAdapterGetRoot(adapter);
    }
    if (!muiEnterMarked(&adapter->busy, &adapter->misuse))
    {
        return nullptr;
    }
    void* result = UiaAdapterGetRoot(adapter);
    adapter->busy = false;
    return result;
}

static bool UiaAdapterHandleGetObject(muiUiaAdapter* adapter, uintptr_t wParam, intptr_t lParam,
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

bool muiUiaAdapter_HandleGetObject(muiUiaAdapter* adapter, uintptr_t wParam, intptr_t lParam,
                                   intptr_t* resultOut)
{
    if (resultOut != nullptr)
    {
        *resultOut = 0;
    }
    if (adapter == nullptr)
    {
        return UiaAdapterHandleGetObject(adapter, wParam, lParam, resultOut);
    }
    if (!muiEnterMarked(&adapter->busy, &adapter->misuse))
    {
        return false;
    }
    bool result = UiaAdapterHandleGetObject(adapter, wParam, lParam, resultOut);
    adapter->busy = false;
    return result;
}

static muiResult UiaAdapterSetScale(muiUiaAdapter* adapter, float scale)
{
    if (adapter == nullptr || !(scale > 0.0f))
    {
        return mui_errorInvalid;
    }
    adapter->scale = scale;
    return mui_success;
}

static MUI_NOINLINE muiResult UiaAdapterSetScaleWorker(muiUiaAdapter* adapter, float scale)
{
    if (adapter == nullptr)
    {
        return UiaAdapterSetScale(adapter, scale);
    }
    if (!muiEnterMarked(&adapter->busy, &adapter->misuse))
    {
        return mui_errorBusy;
    }
    muiResult result = UiaAdapterSetScale(adapter, scale);
    adapter->misuse += result == mui_errorInvalid;
    adapter->busy = false;
    return result;
}

muiResult muiUiaAdapter_SetScale(muiUiaAdapter* adapter, float scale)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = UiaAdapterSetScaleWorker(adapter, scale);
    muiFloatEnvLeave(floatEnv);
    return result;
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

muiUiaRect muiUiaScreenRectOf(const muiUiaAdapter* adapter, muiRect bounds)
{
    POINT origin = muiUiaClientOrigin(adapter);
    double scale = (double)adapter->scale;
    return (muiUiaRect){(double)origin.x + (double)bounds.x * scale,
                        (double)origin.y + (double)bounds.y * scale, (double)bounds.width * scale,
                        (double)bounds.height * scale};
}

muiUiaRect muiUiaScreenRect(const muiUiaAdapter* adapter, uint64_t id)
{
    muiRect bounds = {0};
    if (muiAccessTree_GetBounds(adapter->tree, id, &bounds) != mui_success)
    {
        return (muiUiaRect){0};
    }
    return muiUiaScreenRectOf(adapter, bounds);
}

void muiUiaRootPointOf(const muiUiaAdapter* adapter, double x, double y, float* xOut, float* yOut)
{
    POINT origin = muiUiaClientOrigin(adapter);
    double scale = (double)adapter->scale;
    *xOut = (float)((x - (double)origin.x) / scale);
    *yOut = (float)((y - (double)origin.y) / scale);
}

HRESULT muiUiaPerformRequest(muiUiaAdapter* adapter, const muiAccessRequest* request)
{
    return muiActMarked(&adapter->busy, adapter->action, adapter->user, request) ? S_OK
                                                                                 : NOT_SUPPORTED;
}

HRESULT muiUiaPerform(muiUiaAdapter* adapter, muiAccessAction action, uint64_t target)
{
    const muiAccessRequest request = {.action = action, .target = target};
    return muiUiaPerformRequest(adapter, &request);
}
