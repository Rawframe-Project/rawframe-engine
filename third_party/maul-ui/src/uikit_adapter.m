// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The UIAccessibility adapter (record mui-0008): making it, applying
// updates, the element and container objects it holds by id, and where
// nodes lie on the screen. Objects are made when first asked for; those
// whose node goes are let go, their adapter cleared, so that a client
// still holding one gets nothing.

#include "allocator.h"
#include "uikit.h"

#include <stdalign.h>

#define DEF_COOKIE 0x6D75756Bu // "muuk"

static void Post(UIAccessibilityNotifications notification, id argument)
{
    UIAccessibilityPostNotification(notification, argument);
}

muiUikitAdapterDef muiDefaultUikitAdapterDef(void)
{
    return (muiUikitAdapterDef){.cookie = DEF_COOKIE, .nodes = 4096, .scale = 1.0f};
}

static bool IsValid(const muiUikitAdapterDef* def)
{
    return def->cookie == DEF_COOKIE && muiIsAllocatorValid(&def->allocator) && def->nodes != 0 &&
           def->nodes <= ((uint32_t)1 << 24) && def->view != nullptr && def->action != nullptr &&
           def->scale > 0.0f;
}

// Each map is at most half full.
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
    return sizeof(muiUikitAdapter) + (size_t)nodes * sizeof(uint64_t) +
           2 * map * (sizeof(uint64_t) + sizeof(void*));
}

static void LayMaps(muiUikitAdapter* adapter)
{
    uint32_t map = MapSizeOf(adapter->nodes);
    uint64_t* keys = adapter->scratch + adapter->nodes;
    void** values = (void**)(keys + map);
    muiIdMapInit(&adapter->elementById, keys, values, map);
    uint64_t* more = (uint64_t*)(values + map);
    muiIdMapInit(&adapter->containerById, more, (void**)(more + map), map);
}

muiResult muiCreateUikitAdapter(const muiUikitAdapterDef* def, muiUikitAdapter** adapterOut)
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
    muiUikitAdapter* adapter = (muiUikitAdapter*)block;
    *adapter = (muiUikitAdapter){
        .allocator = def->allocator,
        .blockSize = size,
        .scale = def->scale,
        .action = def->action,
        .user = def->user,
        .nodes = def->nodes,
        .scratch = (uint64_t*)(block + sizeof(muiUikitAdapter)),
        .post = Post,
    };
    LayMaps(adapter);
    muiAccessTreeDef treeDef = muiDefaultAccessTreeDef();
    treeDef.allocator = def->allocator;
    treeDef.nodes = def->nodes;
    muiResult status = muiCreateAccessTree(&treeDef, &adapter->tree);
    if (status != mui_success)
    {
        muiRelease(&def->allocator, block, size, alignof(max_align_t));
        return status;
    }
    adapter->view = [(UIView*)def->view retain];
    *adapterOut = adapter;
    return mui_success;
}

// Lets an object go: it answers nothing from then on.
static void ForgetElement(MUIAccessibilityElement* element)
{
    element->adapter = nullptr;
    [element release];
}

static void ForgetContainer(MUIAccessibilityContainer* container)
{
    container->adapter = nullptr;
    [container release];
}

void muiDestroyUikitAdapter(muiUikitAdapter* adapter)
{
    if (adapter == nullptr)
    {
        return;
    }
    const muiIdMap* elements = &adapter->elementById;
    const muiIdMap* containers = &adapter->containerById;
    for (uint32_t i = 0; i <= elements->mask; i++)
    {
        if (elements->keys[i] != 0)
        {
            ForgetElement((MUIAccessibilityElement*)elements->values[i]);
        }
        if (containers->keys[i] != 0)
        {
            ForgetContainer((MUIAccessibilityContainer*)containers->values[i]);
        }
    }
    [adapter->view release];
    muiDestroyAccessTree(adapter->tree);
    const muiAllocator allocator = adapter->allocator;
    muiRelease(&allocator, adapter, adapter->blockSize, alignof(max_align_t));
}

MUIAccessibilityElement* muiUikitElementOf(muiUikitAdapter* adapter, uint64_t id)
{
    if (adapter == nullptr || muiAccessTree_Find(adapter->tree, id) == nullptr)
    {
        return nil;
    }
    MUIAccessibilityElement* element = muiIdMapFind(&adapter->elementById, id);
    if (element == nil)
    {
        element = [[MUIAccessibilityElement alloc] initWithAccessibilityContainer:adapter->view];
        element->adapter = adapter;
        element->nodeId = id;
        if (!muiIdMapInsert(&adapter->elementById, id, element))
        {
            ForgetElement(element);
            return nil;
        }
    }
    return element;
}

MUIAccessibilityContainer* muiUikitContainerOf(muiUikitAdapter* adapter, uint64_t id)
{
    if (adapter == nullptr || muiAccessTree_Find(adapter->tree, id) == nullptr)
    {
        return nil;
    }
    MUIAccessibilityContainer* container = muiIdMapFind(&adapter->containerById, id);
    if (container == nil)
    {
        container =
            [[MUIAccessibilityContainer alloc] initWithAccessibilityContainer:adapter->view];
        container->adapter = adapter;
        container->nodeId = id;
        if (!muiIdMapInsert(&adapter->containerById, id, container))
        {
            ForgetContainer(container);
            return nil;
        }
    }
    return container;
}

uint32_t muiUikitChildrenOf(const muiUikitAdapter* adapter, uint64_t id)
{
    uint32_t count = 0;
    if (muiAccessTree_GetShownChildren(adapter->tree, id, adapter->scratch, adapter->nodes,
                                       &count) != mui_success)
    {
        count = 0;
    }
    return count;
}

static bool HasChildren(const muiUikitAdapter* adapter, uint64_t id)
{
    uint32_t count = 0;
    muiResult status = muiAccessTree_GetShownChildren(adapter->tree, id, nullptr, 0, &count);
    return (status == mui_success || status == mui_errorCapacity) && count != 0;
}

id muiUikitObjectOf(muiUikitAdapter* adapter, uint64_t node)
{
    return HasChildren(adapter, node) ? (id)muiUikitContainerOf(adapter, node)
                                      : (id)muiUikitElementOf(adapter, node);
}

id muiUikitParentOf(muiUikitAdapter* adapter, uint64_t node)
{
    uint64_t parent = muiAccessTree_GetShownParent(adapter->tree, node);
    return parent != 0 ? (id)muiUikitContainerOf(adapter, parent) : (id)adapter->view;
}

static void Added(void* user, const muiAccessTree* tree, uint64_t id)
{
    (void)tree;
    muiUikitTellAdded(user, id);
}

static void Updated(void* user, const muiAccessTree* tree, const muiAccessNode* old)
{
    muiUikitTellUpdated(user, old, muiAccessTree_Find(tree, old->id));
}

static void Removed(void* user, const muiAccessTree* tree, const muiAccessNode* old)
{
    (void)tree;
    muiUikitAdapter* adapter = user;
    MUIAccessibilityElement* element = muiIdMapRemove(&adapter->elementById, old->id);
    if (element != nil)
    {
        ForgetElement(element);
    }
    MUIAccessibilityContainer* container = muiIdMapRemove(&adapter->containerById, old->id);
    if (container != nil)
    {
        ForgetContainer(container);
    }
}

static void ShownChanged(void* user, const muiAccessTree* tree)
{
    (void)tree;
    ((muiUikitAdapter*)user)->reshaped = true;
}

static void FocusMoved(void* user, const muiAccessTree* tree, uint64_t old, uint64_t focus)
{
    (void)tree;
    (void)old;
    (void)focus;
    ((muiUikitAdapter*)user)->focusMoved = true;
}

muiResult muiUikitAdapter_Apply(muiUikitAdapter* adapter, const muiAccessUpdate* update)
{
    if (adapter == nullptr)
    {
        return mui_errorInvalid;
    }
    const muiAccessChanges changes = {.user = adapter,
                                      .added = Added,
                                      .updated = Updated,
                                      .removed = Removed,
                                      .focusMoved = FocusMoved,
                                      .shownChanged = ShownChanged};
    uint64_t oldRoot = muiAccessTree_GetRoot(adapter->tree);
    adapter->reshaped = false;
    adapter->focusMoved = false;
    adapter->screen = 0;
    muiResult status = muiAccessTree_Apply(adapter->tree, update, &changes);
    if (status == mui_success)
    {
        muiUikitTellChanges(adapter, oldRoot);
    }
    return status;
}

const muiAccessTree* muiUikitAdapter_GetTree(const muiUikitAdapter* adapter)
{
    return adapter != nullptr ? adapter->tree : nullptr;
}

muiResult muiUikitAdapter_SetScale(muiUikitAdapter* adapter, float scale)
{
    if (adapter == nullptr || !(scale > 0.0f))
    {
        return mui_errorInvalid;
    }
    adapter->scale = scale;
    return mui_success;
}

void* muiUikitAdapter_GetRoot(muiUikitAdapter* adapter)
{
    uint64_t root = adapter != nullptr ? muiAccessTree_GetRoot(adapter->tree) : 0;
    return root != 0 ? muiUikitObjectOf(adapter, root) : nullptr;
}

bool muiUikitAct(const muiUikitAdapter* adapter, muiAccessAction action, uint64_t id, float value)
{
    const muiAccessRequest request = {.action = action, .target = id, .value = value};
    return adapter->action(adapter->user, &request);
}

CGRect muiUikitScreenRectOf(const muiUikitAdapter* adapter, uint64_t id)
{
    muiRect box = {0};
    (void)muiAccessTree_GetBounds(adapter->tree, id, &box);
    CGFloat scale = (CGFloat)adapter->scale;
    CGRect inView = CGRectMake((CGFloat)box.x * scale, (CGFloat)box.y * scale,
                               (CGFloat)box.width * scale, (CGFloat)box.height * scale);
    return UIAccessibilityConvertFrameToScreenCoordinates(inView, adapter->view);
}

NSString* muiUikitNameOf(const muiUikitAdapter* adapter, uint64_t id)
{
    size_t length = 0;
    char small[256];
    muiResult status = muiAccessTree_GetName(adapter->tree, id, small, sizeof(small), &length);
    if (status == mui_success)
    {
        return [NSString stringWithUTF8String:small];
    }
    if (status != mui_errorCapacity)
    {
        return nil;
    }
    char* name = muiAllocate(&adapter->allocator, length + 1, 1);
    NSString* string = nil;
    if (name != nullptr &&
        muiAccessTree_GetName(adapter->tree, id, name, length + 1, &length) == mui_success)
    {
        string = [NSString stringWithUTF8String:name];
    }
    if (name != nullptr)
    {
        muiRelease(&adapter->allocator, name, length + 1, 1);
    }
    return string;
}
