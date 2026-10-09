// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The NSAccessibility adapter (record mui-0008): making it, applying
// updates, the node objects it holds by id, and where nodes lie on the
// screen. Objects are made when first asked for; one whose node goes is
// let go, its adapter cleared, so that a client still holding it gets
// nothing.

#include "allocator.h"
#include "ns.h"

#include <stdalign.h>

#define DEF_COOKIE 0x6D756E73u // "muns"

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

muiNsAdapterDef muiDefaultNsAdapterDef(void)
{
    return (muiNsAdapterDef){.cookie = DEF_COOKIE, .nodes = 4096, .scale = 1.0f};
}

static bool IsValid(const muiNsAdapterDef* def)
{
    return def->cookie == DEF_COOKIE && muiIsAllocatorValid(&def->allocator) && def->nodes != 0 &&
           def->nodes <= ((uint32_t)1 << 24) && def->view != nullptr && def->action != nullptr &&
           def->scale > 0.0f;
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

muiResult muiCreateNsAdapter(const muiNsAdapterDef* def, muiNsAdapter** adapterOut)
{
    if (adapterOut != nullptr)
    {
        *adapterOut = nullptr;
    }
    if (def == nullptr || adapterOut == nullptr || !IsValid(def))
    {
        return mui_errorInvalid;
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

// Lets an object go: it answers nothing from then on.
static void Forget(MUIAccessibilityNode* object)
{
    object->adapter = nullptr;
    [object release];
}

void muiDestroyNsAdapter(muiNsAdapter* adapter)
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

static void Updated(void* user, const muiAccessTree* tree, const muiAccessNode* old)
{
    muiNsTellUpdated(user, old, muiAccessTree_Find(tree, old->id));
}

static void Removed(void* user, const muiAccessTree* tree, const muiAccessNode* old)
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

static void ShownChanged(void* user, const muiAccessTree* tree)
{
    (void)tree;
    ((muiNsAdapter*)user)->reshaped = true;
}

static void FocusMoved(void* user, const muiAccessTree* tree, uint64_t old, uint64_t focus)
{
    (void)tree;
    (void)old;
    (void)focus;
    ((muiNsAdapter*)user)->focusMoved = true;
}

muiResult muiNsAdapter_Apply(muiNsAdapter* adapter, const muiAccessUpdate* update)
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

const muiAccessTree* muiNsAdapter_GetTree(const muiNsAdapter* adapter)
{
    return adapter != nullptr ? adapter->tree : nullptr;
}

muiResult muiNsAdapter_SetScale(muiNsAdapter* adapter, float scale)
{
    if (adapter == nullptr || !(scale > 0.0f))
    {
        return mui_errorInvalid;
    }
    adapter->scale = scale;
    return mui_success;
}

void* muiNsAdapter_GetRoot(muiNsAdapter* adapter)
{
    return adapter != nullptr ? muiNsObjectOf(adapter, muiAccessTree_GetRoot(adapter->tree))
                              : nullptr;
}

bool muiNsAct(const muiNsAdapter* adapter, muiAccessAction action, uint64_t id, float value)
{
    const muiAccessRequest request = {.action = action, .target = id, .value = value};
    return adapter->action(adapter->user, &request);
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

NSRect muiNsScreenRectOf(const muiNsAdapter* adapter, uint64_t id)
{
    muiRect box = {0};
    (void)muiAccessTree_GetBounds(adapter->tree, id, &box);
    NSView* view = adapter->view;
    NSRect inWindow = [view convertRect:InView(adapter, box) toView:nil];
    NSWindow* window = [view window];
    return window != nil ? [window convertRectToScreen:inWindow] : inWindow;
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
