// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The ARIA adapter (record mui-0008): making it, applying updates, and
// keeping the page's elements to the shown tree. After an update that
// may reshape the shown tree, one walk of it removes the elements of
// nodes no longer shown, then makes and places the others in the walk's
// order, so each parent's in rising index; nodes whose box changed are
// placed again with what they hold, as each element's place is relative
// to its parent's.

#include "allocator.h"
#include "aria.h"

#include <stdalign.h>
#include <string.h>

#define DEF_COOKIE 0x6D756172u // "muar"

muiAriaAdapterDef muiDefaultAriaAdapterDef(void)
{
    return (muiAriaAdapterDef){.cookie = DEF_COOKIE,
                               .nodes = 4096,
                               .scale = 1.0f,
                               .deferred = true,
                               .enableLabel = "Enable accessibility"};
}

static bool IsValid(const muiAriaAdapterDef* def)
{
    return def->cookie == DEF_COOKIE && muiIsAllocatorValid(&def->allocator) && def->nodes != 0 &&
           def->nodes <= ((uint32_t)1 << 24) && def->host != nullptr &&
           def->enableLabel != nullptr && def->action != nullptr && def->scale > 0.0f;
}

// The elements' map is at most half full.
static uint32_t MapSizeOf(uint32_t nodes)
{
    uint32_t size = 2;
    while (size < 2 * nodes)
    {
        size *= 2;
    }
    return size;
}

// The adapter, then its arrays, those of 8 bytes first.
static size_t SizeOf(uint32_t nodes)
{
    size_t map = MapSizeOf(nodes);
    return sizeof(muiAriaAdapter) + (size_t)nodes * (3 * sizeof(uint64_t)) +
           (size_t)nodes * (sizeof(muiAriaPlace) + sizeof(muiAriaElement)) +
           map * (sizeof(uint64_t) + sizeof(void*)) + (size_t)nodes * sizeof(uint32_t);
}

static void Lay(muiAriaAdapter* adapter, unsigned char* block, uint32_t nodes)
{
    uint32_t map = MapSizeOf(nodes);
    adapter->scratch = (uint64_t*)(block + sizeof(muiAriaAdapter));
    adapter->walk = adapter->scratch + nodes;
    adapter->moved = adapter->walk + nodes;
    adapter->places = (muiAriaPlace*)(adapter->moved + nodes);
    adapter->elements = (muiAriaElement*)(adapter->places + nodes);
    uint64_t* keys = (uint64_t*)(adapter->elements + nodes);
    void** values = (void**)(keys + map);
    adapter->freeElements = (uint32_t*)(values + map);
    // The block's memory may be another's: no element is made yet.
    memset(adapter->elements, 0, (size_t)nodes * sizeof(muiAriaElement));
    muiIdMapInit(&adapter->elementById, keys, values, map);
    for (uint32_t i = 0; i < nodes; i++)
    {
        adapter->freeElements[i] = nodes - 1 - i;
    }
    adapter->freeCount = nodes;
}

muiResult muiCreateAriaAdapter(const muiAriaAdapterDef* def, muiAriaAdapter** adapterOut)
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
    muiAriaAdapter* adapter = (muiAriaAdapter*)block;
    *adapter = (muiAriaAdapter){
        .allocator = def->allocator,
        .blockSize = size,
        .page = -1,
        .scale = def->scale,
        .enabled = !def->deferred,
        .action = def->action,
        .user = def->user,
        .nodes = def->nodes,
    };
    Lay(adapter, block, def->nodes);
    muiAccessTreeDef treeDef = muiDefaultAccessTreeDef();
    treeDef.allocator = def->allocator;
    treeDef.nodes = def->nodes;
    muiResult status = muiCreateAccessTree(&treeDef, &adapter->tree);
    if (status == mui_success)
    {
        adapter->page = muiAriaPageOpen(def->host, def->deferred, def->enableLabel, adapter);
        status = adapter->page >= 0 ? mui_success : mui_errorPlatform;
    }
    if (status != mui_success)
    {
        muiDestroyAriaAdapter(adapter);
        return status;
    }
    *adapterOut = adapter;
    return mui_success;
}

void muiDestroyAriaAdapter(muiAriaAdapter* adapter)
{
    if (adapter == nullptr)
    {
        return;
    }
    if (adapter->page >= 0)
    {
        muiAriaPageClose(adapter->page);
    }
    muiDestroyAccessTree(adapter->tree);
    const muiAllocator allocator = adapter->allocator;
    muiRelease(&allocator, adapter, adapter->blockSize, alignof(max_align_t));
}

uint32_t muiAriaSlotOf(const muiAriaAdapter* adapter, uint64_t id)
{
    const muiAriaElement* element = muiIdMapFind(&adapter->elementById, id);
    return element != nullptr ? (uint32_t)(element - adapter->elements) : ARIA_NO_SLOT;
}

// Places an element at its node's box, relative to its parent's, where
// it moved.
static void PlaceBox(muiAriaAdapter* adapter, uint32_t slot)
{
    muiAriaElement* element = &adapter->elements[slot];
    muiRect box = {0};
    muiRect origin = {0};
    (void)muiAccessTree_GetBounds(adapter->tree, element->id, &box);
    if (element->parent != ARIA_NO_SLOT)
    {
        (void)muiAccessTree_GetBounds(adapter->tree, adapter->elements[element->parent].id,
                                      &origin);
    }
    float scale = adapter->scale;
    const float placed[4] = {(box.x - origin.x) * scale, (box.y - origin.y) * scale,
                             box.width * scale, box.height * scale};
    if (placed[0] != element->box[0] || placed[1] != element->box[1] ||
        placed[2] != element->box[2] || placed[3] != element->box[3])
    {
        muiAriaPageBox(adapter->page, slot, placed[0], placed[1], placed[2], placed[3]);
        for (int i = 0; i < 4; i++)
        {
            element->box[i] = placed[i];
        }
    }
}

// Walks the shown tree in breadth, from the root: every shown node in
// walk, with where it was found. How many.
static uint32_t Walk(muiAriaAdapter* adapter)
{
    uint64_t root = muiAccessTree_GetRoot(adapter->tree);
    if (root == 0)
    {
        return 0;
    }
    uint32_t count = 0;
    adapter->walk[count] = root;
    adapter->places[count++] = (muiAriaPlace){ARIA_NO_SLOT, 0};
    for (uint32_t at = 0; at < count; at++)
    {
        uint32_t children = 0;
        if (muiAccessTree_GetShownChildren(adapter->tree, adapter->walk[at], adapter->scratch,
                                           adapter->nodes, &children) != mui_success)
        {
            children = 0;
        }
        for (uint32_t i = 0; i < children && count < adapter->nodes; i++)
        {
            adapter->walk[count] = adapter->scratch[i];
            adapter->places[count++] = (muiAriaPlace){at, i};
        }
    }
    return count;
}

// Takes an element out of the page and forgets it.
static void Forget(muiAriaAdapter* adapter, uint32_t slot)
{
    muiAriaElement* element = &adapter->elements[slot];
    muiAriaPageRemove(adapter->page, slot);
    (void)muiIdMapRemove(&adapter->elementById, element->id);
    *element = (muiAriaElement){0};
    adapter->freeElements[adapter->freeCount++] = slot;
}

// Removes the elements of nodes the walk did not see.
static void RemoveUnseen(muiAriaAdapter* adapter)
{
    for (uint32_t slot = 0; slot < adapter->nodes; slot++)
    {
        const muiAriaElement* element = &adapter->elements[slot];
        if (element->id != 0 && element->seen != adapter->pass)
        {
            Forget(adapter, slot);
        }
    }
}

// Makes a node's element with all its attributes.
static uint32_t Make(muiAriaAdapter* adapter, const muiAccessNode* node)
{
    uint32_t slot = adapter->freeElements[--adapter->freeCount];
    muiAriaElement* element = &adapter->elements[slot];
    *element = (muiAriaElement){.id = node->id, .parent = ARIA_NO_SLOT, .seen = adapter->pass};
    // An impossible box, so the first placing writes it.
    element->box[2] = -1.0f;
    (void)muiIdMapInsert(&adapter->elementById, node->id, element);
    char id[ARIA_ID_SIZE];
    muiAriaIdOf(adapter->page, node->id, id);
    muiAriaPageMake(adapter->page, slot, muiAriaIsRange(node), id);
    muiAriaWriteAttributes(adapter, slot, nullptr, node);
    return slot;
}

static void Restructure(muiAriaAdapter* adapter)
{
    adapter->pass++;
    uint32_t count = Walk(adapter);
    for (uint32_t at = 0; at < count; at++)
    {
        muiAriaElement* element = muiIdMapFind(&adapter->elementById, adapter->walk[at]);
        if (element != nullptr)
        {
            element->seen = adapter->pass;
        }
    }
    RemoveUnseen(adapter);
    for (uint32_t at = 0; at < count; at++)
    {
        const muiAriaPlace* place = &adapter->places[at];
        uint32_t parent = place->parent != ARIA_NO_SLOT
                              ? muiAriaSlotOf(adapter, adapter->walk[place->parent])
                              : ARIA_NO_SLOT;
        uint32_t slot = muiAriaSlotOf(adapter, adapter->walk[at]);
        if (slot == ARIA_NO_SLOT)
        {
            slot = Make(adapter, muiAccessTree_Find(adapter->tree, adapter->walk[at]));
        }
        muiAriaPagePlace(adapter->page, slot, parent, place->index);
        muiAriaElement* element = &adapter->elements[slot];
        if (element->parent != parent || element->box[2] < 0.0f)
        {
            element->parent = parent;
            PlaceBox(adapter, slot);
        }
    }
}

// Places again the elements at and under a node, shown or not.
static void PlaceUnder(muiAriaAdapter* adapter, uint64_t id)
{
    uint32_t count = 0;
    adapter->walk[count++] = id;
    for (uint32_t at = 0; at < count; at++)
    {
        uint32_t slot = muiAriaSlotOf(adapter, adapter->walk[at]);
        if (slot != ARIA_NO_SLOT)
        {
            PlaceBox(adapter, slot);
        }
        uint32_t children = 0;
        if (muiAccessTree_GetShownChildren(adapter->tree, adapter->walk[at], adapter->scratch,
                                           adapter->nodes, &children) != mui_success)
        {
            children = 0;
        }
        for (uint32_t i = 0; i < children && count < adapter->nodes; i++)
        {
            adapter->walk[count++] = adapter->scratch[i];
        }
    }
}

static bool BoxMoved(const muiAccessNode* old, const muiAccessNode* now)
{
    const muiRect* a = &old->bounds;
    const muiRect* b = &now->bounds;
    const muiDrawTransform* s = &old->transform;
    const muiDrawTransform* t = &now->transform;
    return a->x != b->x || a->y != b->y || a->width != b->width || a->height != b->height ||
           s->a != t->a || s->b != t->b || s->c != t->c || s->d != t->d || s->e != t->e ||
           s->f != t->f;
}

// Says a live node's whole name, as clients read it.
static void Announce(muiAriaAdapter* adapter, const muiAccessNode* node)
{
    size_t length = 0;
    char small[256];
    char* name = small;
    muiResult status =
        muiAccessTree_GetName(adapter->tree, node->id, small, sizeof(small), &length);
    if (status == mui_errorCapacity)
    {
        name = muiAllocate(&adapter->allocator, length + 1, 1);
        status = name != nullptr
                     ? muiAccessTree_GetName(adapter->tree, node->id, name, length + 1, &length)
                     : mui_errorCapacity;
    }
    if (status == mui_success)
    {
        muiAriaPageAnnounce(adapter->page, name, node->values.live == mui_liveAssertive);
    }
    if (name != small && name != nullptr)
    {
        muiRelease(&adapter->allocator, name, length + 1, 1);
    }
}

static void Updated(void* user, const muiAccessTree* tree, const muiAccessNode* old)
{
    muiAriaAdapter* adapter = user;
    const muiAccessNode* node = muiAccessTree_Find(tree, old->id);
    uint32_t slot = muiAriaSlotOf(adapter, old->id);
    if (!adapter->enabled)
    {
        return;
    }
    if (BoxMoved(old, node) && adapter->movedCount < adapter->nodes)
    {
        adapter->moved[adapter->movedCount++] = old->id;
    }
    if (slot == ARIA_NO_SLOT)
    {
        return;
    }
    if (muiAriaIsRange(old) != muiAriaIsRange(node))
    {
        // Another kind of element: the walk makes it anew.
        Forget(adapter, slot);
        adapter->reshaped = true;
        return;
    }
    muiAriaWriteAttributes(adapter, slot, old, node);
    if (node->values.live != mui_liveOff && muiAriaNameChanged(old, node))
    {
        Announce(adapter, node);
    }
}

static void ShownChanged(void* user, const muiAccessTree* tree)
{
    (void)tree;
    ((muiAriaAdapter*)user)->reshaped = true;
}

static void FocusMoved(void* user, const muiAccessTree* tree, uint64_t old, uint64_t focus)
{
    (void)tree;
    (void)old;
    (void)focus;
    ((muiAriaAdapter*)user)->focusMoved = true;
}

// Gives the focused node's element the DOM focus, as the page allows.
static void TellFocus(muiAriaAdapter* adapter, bool always)
{
    uint32_t slot = muiAriaSlotOf(adapter, muiAccessTree_GetFocus(adapter->tree));
    if (slot != ARIA_NO_SLOT)
    {
        muiAriaPageFocus(adapter->page, slot, always);
    }
}

void muiAriaPerform(muiAriaAdapter* adapter, muiAriaEvent event, uint32_t slot, double value)
{
    uint64_t id = slot < adapter->nodes ? adapter->elements[slot].id : 0;
    const muiAccessNode* node = muiAccessTree_Find(adapter->tree, id);
    if (node == nullptr)
    {
        return;
    }
    muiAccessRequest request = {.target = id};
    uint32_t actions = node->actions;
    if (event == mui_ariaFocused)
    {
        request.action = mui_actionFocus;
    }
    else if (event == mui_ariaRangeSet)
    {
        request.action = mui_actionSetValue;
        request.value = (float)value;
    }
    else if ((actions & (1u << mui_actionClick)) != 0)
    {
        request.action = mui_actionClick;
    }
    else if ((actions & (1u << mui_actionExpand | 1u << mui_actionCollapse)) != 0)
    {
        // A click on what expands without a click of its own toggles it.
        request.action =
            (node->flags & mui_accessExpanded) != 0 ? mui_actionCollapse : mui_actionExpand;
    }
    else
    {
        return;
    }
    (void)adapter->action(adapter->user, &request);
}

muiResult muiAriaAdapter_Apply(muiAriaAdapter* adapter, const muiAccessUpdate* update)
{
    if (adapter == nullptr)
    {
        return mui_errorInvalid;
    }
    const muiAccessChanges changes = {.user = adapter,
                                      .updated = Updated,
                                      .shownChanged = ShownChanged,
                                      .focusMoved = FocusMoved};
    adapter->reshaped = false;
    adapter->focusMoved = false;
    adapter->movedCount = 0;
    muiResult status = muiAccessTree_Apply(adapter->tree, update, &changes);
    if (status != mui_success || !adapter->enabled)
    {
        return status;
    }
    if (adapter->reshaped)
    {
        Restructure(adapter);
    }
    if (adapter->movedCount == adapter->nodes)
    {
        PlaceUnder(adapter, muiAccessTree_GetRoot(adapter->tree));
    }
    for (uint32_t i = 0; i < adapter->movedCount && adapter->movedCount < adapter->nodes; i++)
    {
        PlaceUnder(adapter, adapter->moved[i]);
    }
    if (adapter->focusMoved)
    {
        TellFocus(adapter, false);
    }
    return status;
}

const muiAccessTree* muiAriaAdapter_GetTree(const muiAriaAdapter* adapter)
{
    return adapter != nullptr ? adapter->tree : nullptr;
}

muiResult muiAriaAdapter_SetScale(muiAriaAdapter* adapter, float scale)
{
    if (adapter == nullptr || !(scale > 0.0f))
    {
        return mui_errorInvalid;
    }
    adapter->scale = scale;
    if (adapter->enabled)
    {
        PlaceUnder(adapter, muiAccessTree_GetRoot(adapter->tree));
    }
    return mui_success;
}

void muiAriaAdapter_Enable(muiAriaAdapter* adapter)
{
    if (adapter == nullptr || adapter->enabled)
    {
        return;
    }
    adapter->enabled = true;
    // A screen reader user pressed the button: the focus goes on to the
    // program's.
    bool focused = muiAriaPageDropButton(adapter->page);
    Restructure(adapter);
    TellFocus(adapter, focused);
}

bool muiAriaAdapter_IsEnabled(const muiAriaAdapter* adapter)
{
    return adapter != nullptr && adapter->enabled;
}
