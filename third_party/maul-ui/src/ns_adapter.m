// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The NSAccessibility adapter: making it, applying updates, the node
// objects it holds by id, and where nodes lie on the screen. Objects are
// made when first asked for; one whose node goes is let go, its adapter
// cleared, so that a client still holding it gets nothing.

#include "access_record.h"
#include "allocator.h"
#include "fpenv.h"
#include "host_call.h"
#include "ns.h"

#include <stdalign.h>

static void Post(id element, NSAccessibilityNotificationName name, NSDictionary* info)
{
    if (info != nil)
    {
        NSAccessibilityPostNotificationWithUserInfo(element, name, info);
    }
    else
    {
        NSAccessibilityPostNotification(element, name);
    }
}

static bool IsValid(const muiNsAdapterDef* def)
{
    return muiIsAllocatorValid(&def->allocator) && def->nodes != 0 && def->view != nullptr &&
           def->action != nullptr && def->scale > 0.0f;
}

// The objects' map is at most half full.
static uint32_t MapSizeOf(uint32_t nodes)
{
    uint32_t size = 2;
    while (size < 2 * nodes)
    {
        size *= 2;
    }
    return size;
}

static size_t SizeOf(uint32_t nodes)
{
    size_t map = MapSizeOf(nodes);
    return sizeof(muiNsAdapter) + (size_t)nodes * sizeof(uint64_t) +
           map * (sizeof(uint64_t) + sizeof(void*));
}

static MUI_NOINLINE muiResult CreateNsAdapterWorker(const muiNsAdapterDef* def,
                                                    muiNsAdapter** adapterOut)
{
    if (adapterOut != nullptr)
    {
        *adapterOut = nullptr;
    }
    if (def == nullptr || adapterOut == nullptr || def->cookie != MUI_NS_ADAPTER_DEF_COOKIE)
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
    size_t size = SizeOf(def->nodes);
    unsigned char* block = muiAllocate(&def->allocator, size, alignof(max_align_t));
    if (block == nullptr)
    {
        return mui_errorCapacity;
    }
    muiNsAdapter* adapter = (muiNsAdapter*)block;
    *adapter = (muiNsAdapter){
        .allocator = def->allocator,
        .blockSize = size,
        .scale = def->scale,
        .action = def->action,
        .user = def->user,
        .nodes = def->nodes,
        .scratch = (uint64_t*)(block + sizeof(muiNsAdapter)),
        .post = Post,
    };
    uint32_t map = MapSizeOf(def->nodes);
    uint64_t* keys = adapter->scratch + def->nodes;
    muiIdMapInit(&adapter->objectById, keys, (void**)(keys + map), map);
    muiAccessTreeDef treeDef = muiDefaultAccessTreeDef();
    treeDef.allocator = def->allocator;
    treeDef.nodes = def->nodes;
    muiResult status = muiCreateAccessTree(&treeDef, &adapter->tree);
    if (status != mui_success)
    {
        muiRelease(&def->allocator, block, size, alignof(max_align_t));
        return status;
    }
    adapter->view = [(NSView*)def->view retain];
    *adapterOut = adapter;
    return mui_success;
}

muiResult muiCreateNsAdapter(const muiNsAdapterDef* def, muiNsAdapter** adapterOut)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = CreateNsAdapterWorker(def, adapterOut);
    muiFloatEnvLeave(floatEnv);
    return result;
}

// Lets an object go: it answers nothing from then on.
static void Forget(MUIAccessibilityNode* object)
{
    object->adapter = nullptr;
    [object release];
}

static void DestroyNsAdapter(muiNsAdapter* adapter)
{
    if (adapter == nullptr)
    {
        return;
    }
    const muiIdMap* map = &adapter->objectById;
    for (uint32_t i = 0; i <= map->mask; i++)
    {
        if (map->keys[i] != 0)
        {
            Forget((MUIAccessibilityNode*)map->values[i]);
        }
    }
    [adapter->view release];
    muiDestroyAccessTree(adapter->tree);
    const muiAllocator allocator = adapter->allocator;
    muiRelease(&allocator, adapter, adapter->blockSize, alignof(max_align_t));
}

void muiDestroyNsAdapter(muiNsAdapter* adapter)
{
    if (adapter == nullptr)
    {
        DestroyNsAdapter(adapter);
        return;
    }
    if (!muiEnterMarked(&adapter->busy, &adapter->misuse))
    {
        return;
    }
    DestroyNsAdapter(adapter);
}

uint64_t muiNsAdapter_GetMisuse(const muiNsAdapter* adapter)
{
    return adapter != nullptr ? adapter->misuse : 0;
}

MUIAccessibilityNode* muiNsObjectOf(muiNsAdapter* adapter, uint64_t id)
{
    if (adapter == nullptr || muiAccessTree_Find(adapter->tree, id) == nullptr)
    {
        return nil;
    }
    MUIAccessibilityNode* object = muiIdMapFind(&adapter->objectById, id);
    if (object == nil)
    {
        object = [[MUIAccessibilityNode alloc] init];
        object->adapter = adapter;
        object->nodeId = id;
        if (!muiIdMapInsert(&adapter->objectById, id, object))
        {
            Forget(object);
            return nil;
        }
    }
    return object;
}

static MUI_NOINLINE void UpdatedWorker(void* user, const muiAccessTree* tree,
                                       const muiAccessNode* old)
{
    muiNsTellUpdated(user, old, muiAccessTree_Find(tree, old->id));
}

static void Updated(void* user, const muiAccessTree* tree, const muiAccessNode* old)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    UpdatedWorker(user, tree, old);
    muiFloatEnvLeave(floatEnv);
}

static MUI_NOINLINE void RemovedWorker(void* user, const muiAccessTree* tree,
                                       const muiAccessNode* old)
{
    (void)tree;
    muiNsAdapter* adapter = user;
    MUIAccessibilityNode* object = muiIdMapRemove(&adapter->objectById, old->id);
    if (object != nil)
    {
        muiNsTellDestroyed(adapter, object);
        Forget(object);
    }
}

static void Removed(void* user, const muiAccessTree* tree, const muiAccessNode* old)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    RemovedWorker(user, tree, old);
    muiFloatEnvLeave(floatEnv);
}

static MUI_NOINLINE void ShownChangedWorker(void* user, const muiAccessTree* tree)
{
    (void)tree;
    ((muiNsAdapter*)user)->reshaped = true;
}

static void ShownChanged(void* user, const muiAccessTree* tree)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    ShownChangedWorker(user, tree);
    muiFloatEnvLeave(floatEnv);
}

static MUI_NOINLINE void FocusMovedWorker(void* user, const muiAccessTree* tree, uint64_t old,
                                          uint64_t focus)
{
    (void)tree;
    (void)old;
    (void)focus;
    ((muiNsAdapter*)user)->focusMoved = true;
}

static void FocusMoved(void* user, const muiAccessTree* tree, uint64_t old, uint64_t focus)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    FocusMovedWorker(user, tree, old, focus);
    muiFloatEnvLeave(floatEnv);
}

static muiResult NsAdapterApply(muiNsAdapter* adapter, const muiAccessUpdate* update)
{
    if (adapter == nullptr)
    {
        return mui_errorInvalid;
    }
    const muiAccessChanges changes = {.user = adapter,
                                      .updated = Updated,
                                      .removed = Removed,
                                      .shownChanged = ShownChanged,
                                      .focusMoved = FocusMoved};
    adapter->reshaped = false;
    adapter->focusMoved = false;
    muiResult status = muiAccessTree_Apply(adapter->tree, update, &changes);
    if (status == mui_success)
    {
        // The focus shown moves with the active descendant too.
        uint64_t shown = muiRecordActiveFocus(adapter->tree);
        adapter->focusMoved = adapter->focusMoved || shown != adapter->toldFocus;
        adapter->toldFocus = shown;
    }
    if (status == mui_success && adapter->reshaped)
    {
        muiNsTellLayout(adapter);
    }
    if (status == mui_success && adapter->focusMoved)
    {
        muiNsTellFocus(adapter);
    }
    return status;
}

static MUI_NOINLINE muiResult NsAdapterApplyWorker(muiNsAdapter* adapter,
                                                   const muiAccessUpdate* update)
{
    if (adapter == nullptr)
    {
        return NsAdapterApply(adapter, update);
    }
    if (!muiEnterMarked(&adapter->busy, &adapter->misuse))
    {
        return mui_errorBusy;
    }
    muiResult result = NsAdapterApply(adapter, update);
    adapter->misuse += result == mui_errorInvalid;
    adapter->busy = false;
    return result;
}

muiResult muiNsAdapter_Apply(muiNsAdapter* adapter, const muiAccessUpdate* update)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = NsAdapterApplyWorker(adapter, update);
    muiFloatEnvLeave(floatEnv);
    return result;
}

const muiAccessTree* muiNsAdapter_GetTree(const muiNsAdapter* adapter)
{
    return adapter != nullptr ? adapter->tree : nullptr;
}

static muiResult NsAdapterSetScale(muiNsAdapter* adapter, float scale)
{
    if (adapter == nullptr || !(scale > 0.0f))
    {
        return mui_errorInvalid;
    }
    adapter->scale = scale;
    return mui_success;
}

static MUI_NOINLINE muiResult NsAdapterSetScaleWorker(muiNsAdapter* adapter, float scale)
{
    if (adapter == nullptr)
    {
        return NsAdapterSetScale(adapter, scale);
    }
    if (!muiEnterMarked(&adapter->busy, &adapter->misuse))
    {
        return mui_errorBusy;
    }
    muiResult result = NsAdapterSetScale(adapter, scale);
    adapter->misuse += result == mui_errorInvalid;
    adapter->busy = false;
    return result;
}

muiResult muiNsAdapter_SetScale(muiNsAdapter* adapter, float scale)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = NsAdapterSetScaleWorker(adapter, scale);
    muiFloatEnvLeave(floatEnv);
    return result;
}

static void* NsAdapterGetRoot(muiNsAdapter* adapter)
{
    return adapter != nullptr ? muiNsObjectOf(adapter, muiAccessTree_GetRoot(adapter->tree))
                              : nullptr;
}

void* muiNsAdapter_GetRoot(muiNsAdapter* adapter)
{
    if (adapter == nullptr)
    {
        return NsAdapterGetRoot(adapter);
    }
    if (!muiEnterMarked(&adapter->busy, &adapter->misuse))
    {
        return nullptr;
    }
    void* result = NsAdapterGetRoot(adapter);
    adapter->busy = false;
    return result;
}

bool muiNsAct(const muiNsAdapter* adapter, muiAccessAction action, uint64_t id, float value)
{
    const muiAccessRequest request = {.action = action, .target = id, .value = value};
    return muiActMarked(&adapter->busy, adapter->action, adapter->user, &request);
}

// A box in units, in the view's points, flipped or not.
static NSRect InView(const muiNsAdapter* adapter, muiRect box)
{
    CGFloat scale = (CGFloat)adapter->scale;
    NSView* view = adapter->view;
    CGFloat height = (CGFloat)box.height * scale;
    CGFloat top = (CGFloat)box.y * scale;
    CGFloat y = [view isFlipped] ? top : NSHeight([view bounds]) - top - height;
    return NSMakeRect((CGFloat)box.x * scale, y, (CGFloat)box.width * scale, height);
}

NSRect muiNsScreenRectOfBox(const muiNsAdapter* adapter, muiRect box)
{
    NSView* view = adapter->view;
    NSRect inWindow = [view convertRect:InView(adapter, box) toView:nil];
    NSWindow* window = [view window];
    return window != nil ? [window convertRectToScreen:inWindow] : inWindow;
}

NSRect muiNsScreenRectOf(const muiNsAdapter* adapter, uint64_t id)
{
    muiRect box = {0};
    (void)muiAccessTree_GetBounds(adapter->tree, id, &box);
    return muiNsScreenRectOfBox(adapter, box);
}

void muiNsRootPointOf(const muiNsAdapter* adapter, NSPoint screen, float* xOut, float* yOut)
{
    NSView* view = adapter->view;
    NSWindow* window = [view window];
    NSPoint inWindow = window != nil ? [window convertPointFromScreen:screen] : screen;
    NSPoint inView = [view convertPoint:inWindow fromView:nil];
    CGFloat scale = (CGFloat)adapter->scale;
    CGFloat top = [view isFlipped] ? inView.y : NSHeight([view bounds]) - inView.y;
    *xOut = (float)(inView.x / scale);
    *yOut = (float)(top / scale);
}

static bool Holds(const muiNsAdapter* adapter, uint64_t id, NSPoint point)
{
    return NSPointInRect(point, muiNsScreenRectOf(adapter, id));
}

uint64_t muiNsNodeAt(const muiNsAdapter* adapter, NSPoint screen)
{
    uint64_t at = muiAccessTree_GetRoot(adapter->tree);
    if (at == 0 || !Holds(adapter, at, screen))
    {
        return 0;
    }
    // Down through the last drawn shown child holding the point.
    for (uint32_t depth = 0; depth < adapter->nodes; depth++)
    {
        uint32_t count = 0;
        if (muiAccessTree_GetShownChildren(adapter->tree, at, adapter->scratch, adapter->nodes,
                                           &count) != mui_success)
        {
            count = 0;
        }
        uint64_t next = 0;
        for (uint32_t i = count; i > 0 && next == 0; i--)
        {
            next = Holds(adapter, adapter->scratch[i - 1], screen) ? adapter->scratch[i - 1] : 0;
        }
        if (next == 0)
        {
            break;
        }
        at = next;
    }
    return at;
}
