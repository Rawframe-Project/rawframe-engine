// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Accessibility (record mui-0008): what each node is, read from the
// host's data and what the library holds, and updates of the nodes whose
// record changed since it was last sent.

#include "access.h"
#include "access_store.h"
#include "context.h"
#include "focus.h"
#include "layer.h"
#include "range.h"
#include "scroll_store.h"
#include "style_store.h"
#include "tree.h"
#include "virtual.h"
#include "virtual_store.h"

#include "maul-ui/access.h"

// Roles that take a click without saying so.
static bool IsClicked(muiRole role)
{
    switch (role)
    {
    case mui_roleLink:
    case mui_roleButton:
    case mui_roleDefaultButton:
    case mui_roleCheckBox:
    case mui_roleRadioButton:
    case mui_roleSwitch:
    case mui_roleListBoxOption:
    case mui_roleTreeItem:
    case mui_roleMenuItem:
    case mui_roleMenuItemCheckBox:
    case mui_roleMenuItemRadio:
    case mui_roleTab:
    case mui_roleDisclosureTriangle:
        return true;
    default:
        return false;
    }
}

static uint32_t Bit(muiAccessAction action)
{
    return 1u << action;
}

// The flags of a node: the host's, and what its states and the library
// add.
static muiAccessFlags FlagsOf(const muiContext* context, uint32_t slot, muiAccessFlags host)
{
    const muiTree* tree = &context->tree;
    muiState states = muiStatesOf(&context->style.nodes[slot - 1]);
    muiAccessFlags flags = host;
    flags |= (host & mui_accessCheckable) != 0 && (states & mui_stateChecked) != 0
                 ? mui_accessChecked
                 : 0;
    flags |= (host & mui_accessSelectable) != 0 && (states & mui_stateSelected) != 0
                 ? mui_accessSelected
                 : 0;
    flags |= (states & mui_stateDisabled) != 0 ? mui_accessDisabled : 0;
    // An exiting node is on its way out: its subtree is hidden.
    flags |= (muiTreeAt(tree, slot)->flags & MUI_TREE_EXITING) != 0 ? mui_accessHidden : 0;
    flags |= context->interaction[slot - 1].layer == mui_layerModal && muiIsLayerRoot(tree, slot)
                 ? mui_accessModal
                 : 0;
    flags |= context->layout[slot - 1].style.scrollAxes != mui_scrollNone
                 ? mui_accessClipsChildren | mui_accessScrolls
                 : 0;
    flags |= muiRangeOf(context, slot) != nullptr ? mui_accessNumeric : 0;
    flags |= muiFocusTakes(context, slot, mui_focusPointer) && !muiFocusIsCovered(context, slot)
                 ? mui_accessFocusable
                 : 0;
    return flags;
}

// The actions a node takes, from its role and flags.
static uint32_t ActionsOf(const muiContext* context, uint32_t slot, muiRole role,
                          muiAccessFlags flags)
{
    uint32_t actions = 0;
    bool focused = muiTreeResolve(&context->tree, context->focus.nodes[0]) == slot;
    actions |= (flags & mui_accessFocusable) != 0 && !focused ? Bit(mui_actionFocus) : 0;
    actions |= focused ? Bit(mui_actionBlur) : 0;
    if ((flags & mui_accessDisabled) == 0)
    {
        actions |= (flags & mui_accessClickable) != 0 || IsClicked(role) ? Bit(mui_actionClick) : 0;
        if ((flags & mui_accessExpandable) != 0)
        {
            actions |=
                (flags & mui_accessExpanded) != 0 ? Bit(mui_actionCollapse) : Bit(mui_actionExpand);
        }
        if ((flags & mui_accessNumeric) != 0 && (flags & mui_accessReadOnly) == 0)
        {
            actions |=
                Bit(mui_actionIncrement) | Bit(mui_actionDecrement) | Bit(mui_actionSetValue);
        }
    }
    actions |=
        muiTreeAt(&context->tree, slot)->links.parent != 0 ? Bit(mui_actionScrollIntoView) : 0;
    muiScrollAxes axes = context->layout[slot - 1].style.scrollAxes;
    actions |=
        (axes & mui_scrollVertical) != 0 ? Bit(mui_actionScrollUp) | Bit(mui_actionScrollDown) : 0;
    actions |= (axes & mui_scrollHorizontal) != 0
                   ? Bit(mui_actionScrollLeft) | Bit(mui_actionScrollRight)
                   : 0;
    actions |= axes != mui_scrollNone ? Bit(mui_actionSetScrollOffset) : 0;
    return actions;
}

// Fills the values the library holds: bounds and transform, scrolling,
// the range, the place in a virtual list.
static void PlaceOf(const muiContext* context, uint32_t slot, muiAccessNode* node)
{
    const muiLayoutNode* layout = &context->layout[slot - 1];
    const muiScrollState* scroll = &context->scrolls[slot - 1];
    uint32_t parent = muiTreeAt(&context->tree, slot)->links.parent;
    node->bounds = (muiRect){0.0f, 0.0f, layout->rect.width, layout->rect.height};
    // Its place in its parent, which moves by the parent's scroll as
    // painting moves it.
    float x = layout->rect.x;
    float y = layout->rect.y;
    if (parent != 0)
    {
        x += muiScrollShiftX(&context->layout[parent - 1], &context->scrolls[parent - 1]);
        y += muiScrollShiftY(&context->layout[parent - 1], &context->scrolls[parent - 1]);
    }
    node->transform = (muiDrawTransform){1.0f, 0.0f, 0.0f, 1.0f, x, y};
    if ((node->flags & mui_accessScrolls) != 0)
    {
        const muiSize size = {layout->rect.width, layout->rect.height};
        node->scrollX = scroll->x;
        node->scrollY = scroll->y;
        node->scrollXMax = muiScrollLimit(&layout->style, size, scroll, true);
        node->scrollYMax = muiScrollLimit(&layout->style, size, scroll, false);
    }
    const muiValueRange* range = muiRangeOf(context, slot);
    if (range != nullptr)
    {
        node->value = range->value;
        node->minimum = range->minimum;
        node->maximum = range->maximum;
        node->step = range->step;
    }
    // The host's position wins; a range's axis is its orientation unless
    // the host gave one.
    const muiVirtualEntry* list = parent != 0 ? muiVirtualEntryOf(context, parent) : nullptr;
    uint32_t item = context->lists.items[slot - 1];
    muiAccessValues* values = &node->values;
    if (list != nullptr && item != 0 && item <= list->list.count && values->setPosition == 0 &&
        values->setSize == 0)
    {
        values->setPosition = item;
        values->setSize = list->list.count;
    }
    if (range != nullptr && values->orientation == mui_orientationNone)
    {
        values->orientation = range->axis == mui_rangeHorizontal ? mui_orientationHorizontal
                                                                 : mui_orientationVertical;
    }
}

// The order a virtual list's children are read in: by their items, then
// those bound to none in tree order before them.
static uint32_t KeyOf(const muiContext* context, uint32_t slot, uint32_t count)
{
    uint32_t item = context->lists.items[slot - 1];
    return item != 0 && item <= count ? item : 0;
}

// Lists a node's children, a virtual list's by item; their count.
static uint32_t ChildrenOf(const muiContext* context, uint32_t slot, uint32_t* childrenOut)
{
    const muiTree* tree = &context->tree;
    uint32_t count = 0;
    for (uint32_t c = muiTreeAt(tree, slot)->links.firstChild; c != 0;
         c = muiTreeAt(tree, c)->links.next)
    {
        childrenOut[count++] = c;
    }
    const muiVirtualEntry* list = muiVirtualEntryOf(context, slot);
    if (list == nullptr)
    {
        return count;
    }
    // An insertion sort, stable: a window holds tens of items.
    for (uint32_t i = 1; i < count; i++)
    {
        uint32_t child = childrenOut[i];
        uint32_t key = KeyOf(context, child, list->list.count);
        uint32_t at = i;
        while (at > 0 && KeyOf(context, childrenOut[at - 1], list->list.count) > key)
        {
            childrenOut[at] = childrenOut[at - 1];
            at--;
        }
        childrenOut[at] = child;
    }
    return count;
}

uint32_t muiAccessDerive(const muiContext* context, uint32_t slot, muiAccessNode* nodeOut,
                         uint32_t* childrenOut)
{
    const muiAccessEntry* entry = muiAccessEntryOf(context, slot);
    *nodeOut = (muiAccessNode){
        .id = muiAccessIdOf(muiTreeIdOf(&context->tree, slot)),
        .role = entry != nullptr ? entry->role : mui_roleGeneric,
        .values = entry != nullptr ? entry->values : muiDefaultAccessValues(),
        .links = entry != nullptr ? entry->links : nullptr,
        .linkCount = entry != nullptr ? entry->linkCount : 0,
    };
    nodeOut->flags = FlagsOf(context, slot, entry != nullptr ? entry->flags : 0);
    nodeOut->actions = ActionsOf(context, slot, nodeOut->role, nodeOut->flags);
    PlaceOf(context, slot, nodeOut);
    for (uint32_t kind = 0; entry != nullptr && kind < MUI_ACCESS_TEXTS; kind++)
    {
        nodeOut->text[kind] = entry->text[kind];
        nodeOut->textLength[kind] = entry->length[kind];
    }
    return childrenOut != nullptr ? ChildrenOf(context, slot, childrenOut) : 0;
}

// Whether two records of a node agree in all but their texts and links,
// which the host data's version stands for, and their place in an
// update.
static bool IsSame(const muiAccessNode* a, const muiAccessNode* b)
{
    return a->id == b->id && a->role == b->role && a->flags == b->flags &&
           a->actions == b->actions && a->bounds.x == b->bounds.x && a->bounds.y == b->bounds.y &&
           a->bounds.width == b->bounds.width && a->bounds.height == b->bounds.height &&
           a->transform.a == b->transform.a && a->transform.b == b->transform.b &&
           a->transform.c == b->transform.c && a->transform.d == b->transform.d &&
           a->transform.e == b->transform.e && a->transform.f == b->transform.f &&
           a->value == b->value && a->minimum == b->minimum && a->maximum == b->maximum &&
           a->step == b->step && a->scrollX == b->scrollX && a->scrollY == b->scrollY &&
           a->scrollXMax == b->scrollXMax && a->scrollYMax == b->scrollYMax &&
           muiAccessSameValues(&a->values, &b->values);
}

// FNV-1a over bytes, on from print.
static uint64_t Fingerprint(uint64_t print, const unsigned char* bytes, size_t length)
{
    for (size_t i = 0; i < length; i++)
    {
        print = (print ^ bytes[i]) * 1099511628211ULL;
    }
    return print;
}

// Reads host content's text from the host's text function into a node
// whose value the host did not set, labelling a node the host gave no
// role; a fingerprint of the text, 0 for none.
static uint64_t ReadContent(muiContext* context, uint32_t slot, muiAccessNode* node)
{
    const muiAccessStore* store = &context->access;
    if (store->textFunction == nullptr ||
        context->layout[slot - 1].style.content != mui_contentHost ||
        node->text[mui_accessValue] != nullptr)
    {
        return 0;
    }
    const char* text = nullptr;
    size_t length = 0;
    // As the measure function, it may not edit the context.
    context->inHostCall = true;
    bool read = store->textFunction(store->textUser, muiTreeIdOf(&context->tree, slot),
                                    muiTreeAt(&context->tree, slot)->hostKey, &text, &length);
    context->inHostCall = false;
    if (!read || text == nullptr || length == 0 || length > INT32_MAX ||
        !muiAccessIsUtf8((const unsigned char*)text, length))
    {
        return 0;
    }
    node->text[mui_accessValue] = text;
    node->textLength[mui_accessValue] = (uint32_t)length;
    node->role = node->role == mui_roleGeneric ? mui_roleLabel : node->role;
    // A changed text whose fingerprint matches the last, one in 2^64, is
    // not sent again.
    return Fingerprint(14695981039346656037ULL, (const unsigned char*)text, length);
}

// A build under way.
typedef struct Build
{
    muiContext* context;
    bool whole;
    uint32_t nodeCount;
    uint32_t childCount;
} Build;

// Marks a subtree to be sent whole: its nodes left the tree adapters
// keep with it, when its root left its parent's list.
static void Resend(muiContext* context, uint32_t node)
{
    muiTree* tree = &context->tree;
    for (uint32_t at = node; at != 0; at = muiTreeNextIn(tree, node, at))
    {
        context->access.copies[at - 1].id = 0;
        muiTreeMark(tree, at, mui_stageAccess);
    }
}

// Whether a node's children are those last sent, in order; children it
// lists anew, which left the adapters' tree, are marked to be resent.
static bool HasSameChildren(muiContext* context, uint32_t slot, const uint32_t* children,
                            uint32_t count, bool sentBefore)
{
    const muiAccessSent* sent = context->access.sent;
    const muiTree* tree = &context->tree;
    bool same = sentBefore && sent[slot - 1].childCount == count;
    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t child = children[i];
        const muiAccessSent* place = &sent[child - 1];
        bool listed = sentBefore && place->parent == slot &&
                      place->generation == muiTreeAt(tree, child)->generation &&
                      place->list == sent[slot - 1].serial;
        same = same && listed && place->place == i;
        // A child that was out of this list was out of the tree, unless
        // it is new, never sent, and sent whole anyway.
        if (!listed && context->access.copies[child - 1].id != 0)
        {
            Resend(context, child);
        }
    }
    return same;
}

// Derives the node at slot and, when it differs from what was last sent
// or the build is whole, adds it to the update.
static void Visit(Build* build, uint32_t slot)
{
    muiContext* context = build->context;
    muiAccessStore* store = &context->access;
    uint32_t* children = store->order;
    muiAccessNode node;
    uint32_t count = muiAccessDerive(context, slot, &node, children);
    uint64_t content = ReadContent(context, slot, &node);
    const muiAccessEntry* entry = muiAccessEntryOf(context, slot);
    uint32_t version = entry != nullptr ? entry->version : 0;
    muiAccessNode* copy = &store->copies[slot - 1];
    bool sentBefore = copy->id == node.id;
    bool sameChildren = HasSameChildren(context, slot, children, count, sentBefore);
    if (!build->whole && sentBefore && sameChildren && store->sent[slot - 1].version == version &&
        store->sent[slot - 1].content == content && IsSame(copy, &node))
    {
        return;
    }
    node.firstChild = build->childCount;
    node.childCount = count;
    for (uint32_t i = 0; i < count; i++)
    {
        store->children[build->childCount++] =
            muiAccessIdOf(muiTreeIdOf(&context->tree, children[i]));
    }
    *copy = node;
    store->nodes[build->nodeCount++] = copy;
    muiAccessSent* sent = &store->sent[slot - 1];
    sent->version = version;
    sent->content = content;
    sent->childCount = count;
    // A new list: children it no longer holds hold an older one.
    sent->serial++;
}

// Writes down where the nodes sent list their children, once every node
// was compared with what was sent before.
static void Settle(muiContext* context, uint32_t nodeCount)
{
    muiAccessStore* store = &context->access;
    const muiTree* tree = &context->tree;
    for (uint32_t i = 0; i < nodeCount; i++)
    {
        const muiAccessNode* node = store->nodes[i];
        uint32_t parent = (uint32_t)node->id;
        for (uint32_t k = 0; k < node->childCount; k++)
        {
            uint32_t child = (uint32_t)store->children[node->firstChild + k];
            muiAccessSent* sent = &store->sent[child - 1];
            sent->parent = parent;
            sent->place = k;
            sent->generation = muiTreeAt(tree, child)->generation;
            sent->list = store->sent[parent - 1].serial;
        }
    }
}

// A fingerprint of the layers under root, in order: each one's id, kind
// and whether it exits, FNV-1a over their bytes. Opening, closing,
// raising or exiting a modal layer changes which nodes it covers.
static uint64_t LayersOf(const muiContext* context, uint32_t root)
{
    const muiTree* tree = &context->tree;
    uint64_t print = 14695981039346656037ULL;
    for (uint32_t i = 0; i < context->layers.count; i++)
    {
        uint32_t layer = muiLayerAt(context, i);
        if (layer == 0 || !muiTreeIsAncestor(tree, root, layer))
        {
            continue;
        }
        uint64_t word = muiAccessIdOf(muiTreeIdOf(tree, layer)) ^
                        (uint64_t)context->interaction[layer - 1].layer << 56 ^
                        (uint64_t)muiTreeIsExiting(tree, layer) << 60;
        for (uint32_t b = 0; b < 8; b++)
        {
            print = (print ^ ((word >> (8 * b)) & 0xFF)) * 1099511628211ULL;
        }
    }
    return print;
}

muiResult muiBuildAccessUpdate(muiContext* context, muiNodeId rootId, muiAccessUpdate* updateOut)
{
    if (context == nullptr || updateOut == nullptr)
    {
        return mui_errorInvalid;
    }
    *updateOut = (muiAccessUpdate){0};
    if (rootId.index1 == 0 || muiIsInHostCall(context))
    {
        return muiRefuse(context);
    }
    muiTree* tree = &context->tree;
    uint32_t root = muiTreeResolve(tree, rootId);
    if (root == 0)
    {
        return mui_errorStale;
    }
    muiAccessRoot* entry = muiAccessRootOf(context, root);
    if (entry == nullptr)
    {
        return mui_empty;
    }
    Build build = {.context = context, .whole = entry->whole};
    // Whole, or with the layers changed, every node; else the marked
    // ones, found through the marks. Nodes marked while visiting (a
    // subtree to resend) lie after the node that marked them, so the walk
    // still reaches them.
    uint64_t layers = LayersOf(context, root);
    bool every = build.whole || layers != entry->layers;
    for (uint32_t at = every ? root : muiTreeNextOwing(tree, root, 0, mui_stageAccess); at != 0;
         at = every ? muiTreeNextIn(tree, root, at)
                    : muiTreeNextOwing(tree, root, at, mui_stageAccess))
    {
        muiTreeNode* node = muiTreeAt(tree, at);
        bool marked = (node->dirty.request & mui_stageAccess) != 0;
        node->dirty.request &= (muiStages)~mui_stageAccess;
        if (every || marked)
        {
            Visit(&build, at);
        }
        node->dirty.subtree &= (muiStages)~mui_stageAccess;
    }
    entry->layers = layers;
    Settle(context, build.nodeCount);
    uint32_t focus = muiTreeResolve(tree, context->focus.nodes[0]);
    focus = focus != 0 && muiTreeIsAncestor(tree, root, focus) ? focus : root;
    *updateOut = (muiAccessUpdate){
        .nodes = context->access.nodes,
        .nodeCount = build.nodeCount,
        .children = context->access.children,
        .root = build.whole ? muiAccessIdOf(rootId) : 0,
        .focus = muiAccessIdOf(muiTreeIdOf(tree, focus)),
    };
    entry->whole = false;
    return mui_success;
}
