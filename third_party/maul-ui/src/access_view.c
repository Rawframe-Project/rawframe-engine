// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The accessibility tree's consumer (record mui-0008): the tree as
// platforms see it. Filtering leaves out hidden subtrees and children
// clipped wholly out of view, and flattens generic nodes into their
// children; names come from labels, the nodes that label, or contents;
// bounds are carried into the root's placement through every transform.

#include "access_tree_store.h"

#include "maul-ui/access_tree.h"

#include <string.h>

// How filtering treats a node: shown; left out with its subtree; or left
// out alone, its children shown in its place.
typedef enum Status
{
    Status_shown,
    Status_hidden,
    Status_flattened,
} Status;

// The box around a rectangle carried through a transform.
static muiRect BoxOf(const muiDrawTransform* t, muiRect rect)
{
    const float xs[4] = {rect.x, rect.x + rect.width, rect.x, rect.x + rect.width};
    const float ys[4] = {rect.y, rect.y, rect.y + rect.height, rect.y + rect.height};
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    for (int i = 0; i < 4; i++)
    {
        float x = t->a * xs[i] + t->c * ys[i] + t->e;
        float y = t->b * xs[i] + t->d * ys[i] + t->f;
        left = i == 0 || x < left ? x : left;
        right = i == 0 || x > right ? x : right;
        top = i == 0 || y < top ? y : top;
        bottom = i == 0 || y > bottom ? y : bottom;
    }
    return (muiRect){left, top, right - left, bottom - top};
}

// Whether a parent's child, by place, lies wholly outside the parent.
static bool IsOutside(const muiAccessTree* tree, const muiHeldNode* parent, uint32_t index)
{
    uint32_t slot = muiHeldSlotOf(tree, parent->children[index]);
    const muiAccessNode* child = &tree->held[slot - 1].node;
    const muiRect box = BoxOf(&child->transform, child->bounds);
    const muiRect clip = parent->node.bounds;
    return box.x + box.width <= clip.x || box.x >= clip.x + clip.width ||
           box.y + box.height <= clip.y || box.y >= clip.y + clip.height;
}

// Whether a child is clipped out of view: outside a parent that clips,
// and so are its neighbours, so the first child past each edge stays to
// be scrolled to.
static bool IsClippedOut(const muiAccessTree* tree, uint32_t parentSlot, uint32_t index)
{
    const muiHeldNode* parent = &tree->held[parentSlot - 1];
    if ((parent->node.flags & mui_accessClipsChildren) == 0 || !IsOutside(tree, parent, index))
    {
        return false;
    }
    bool before = index > 0 && !IsOutside(tree, parent, index - 1);
    bool after = index + 1 < parent->node.childCount && !IsOutside(tree, parent, index + 1);
    return !before && !after;
}

// Whether a node is the focus or above it.
static bool HoldsFocus(const muiAccessTree* tree, uint32_t slot)
{
    uint32_t steps = 0;
    for (uint32_t at = muiHeldSlotOf(tree, tree->focus); at != 0 && steps <= tree->count;
         at = tree->held[at - 1].parent, steps++)
    {
        if (at == slot)
        {
            return true;
        }
    }
    return false;
}

// How filtering treats the child at index of the node at parentSlot.
static Status StatusOf(const muiAccessTree* tree, uint32_t parentSlot, uint32_t index)
{
    uint32_t slot = muiHeldSlotOf(tree, tree->held[parentSlot - 1].children[index]);
    const muiAccessNode* node = &tree->held[slot - 1].node;
    bool focus = node->id == tree->focus;
    if ((!focus && (node->flags & mui_accessHidden) != 0) ||
        (IsClippedOut(tree, parentSlot, index) && !HoldsFocus(tree, slot)))
    {
        return Status_hidden;
    }
    // The core makes every node generic until the host gives a role, so
    // a generic node with a label stays to be heard.
    bool flattened =
        !focus && node->role == mui_roleGeneric && node->text[mui_accessLabel] == nullptr;
    return flattened ? Status_flattened : Status_shown;
}

static bool BoxDiffers(const muiAccessNode* old, const muiAccessNode* now)
{
    const muiRect* a = &old->bounds;
    const muiRect* b = &now->bounds;
    const muiDrawTransform* s = &old->transform;
    const muiDrawTransform* t = &now->transform;
    return a->x != b->x || a->y != b->y || a->width != b->width || a->height != b->height ||
           s->a != t->a || s->b != t->b || s->c != t->c || s->d != t->d || s->e != t->e ||
           s->f != t->f;
}

bool muiViewDiffers(const muiAccessTree* tree, const muiHeldNode* old, const muiHeldNode* now)
{
    const muiAccessNode* a = &old->node;
    const muiAccessNode* b = &now->node;
    const uint32_t shaping = mui_accessHidden | mui_accessClipsChildren;
    uint32_t count = a->childCount;
    if (count != b->childCount ||
        (count != 0 && memcmp(old->children, now->children, count * sizeof(uint64_t)) != 0) ||
        ((a->flags ^ b->flags) & shaping) != 0 ||
        (a->role == mui_roleGeneric) != (b->role == mui_roleGeneric) ||
        (a->text[mui_accessLabel] == nullptr) != (b->text[mui_accessLabel] == nullptr))
    {
        return true;
    }
    // A box counts where it or its parent clips (IsClippedOut).
    if (!BoxDiffers(a, b))
    {
        return false;
    }
    const muiHeldNode* parent = now->parent != 0 ? &tree->held[now->parent - 1] : nullptr;
    return (b->flags & mui_accessClipsChildren) != 0 ||
           (parent != nullptr && (parent->node.flags & mui_accessClipsChildren) != 0);
}

static uint32_t IndexIn(const muiAccessTree* tree, uint32_t parentSlot, uint64_t id)
{
    const muiHeldNode* parent = &tree->held[parentSlot - 1];
    uint32_t index = 0;
    while (index < parent->node.childCount && parent->children[index] != id)
    {
        index++;
    }
    return index;
}

// Walks from the root down to a node: whether it is shown, and its
// nearest ancestor shown.
static bool Trace(const muiAccessTree* tree, uint32_t slot, uint32_t* shownParentOut)
{
    uint32_t depth = 0;
    for (uint32_t at = slot; at != 0 && depth < tree->capacity; at = tree->held[at - 1].parent)
    {
        tree->stack[depth++] = at;
    }
    // Every node held is under the root, so the walk ends there.
    *shownParentOut = 0;
    uint32_t shown = tree->stack[depth - 1];
    for (uint32_t k = depth - 1; k > 0; k--)
    {
        uint32_t parent = tree->stack[k];
        uint32_t child = tree->stack[k - 1];
        Status status =
            StatusOf(tree, parent, IndexIn(tree, parent, tree->held[child - 1].node.id));
        if (status == Status_hidden || k == 1)
        {
            *shownParentOut = shown;
            return status == Status_shown;
        }
        shown = status == Status_shown ? child : shown;
    }
    return true;
}

bool muiAccessTree_IsShown(const muiAccessTree* tree, uint64_t id)
{
    uint32_t slot = tree != nullptr ? muiHeldSlotOf(tree, id) : 0;
    uint32_t parent = 0;
    return slot != 0 && Trace(tree, slot, &parent);
}

uint64_t muiAccessTree_GetShownParent(const muiAccessTree* tree, uint64_t id)
{
    uint32_t slot = tree != nullptr ? muiHeldSlotOf(tree, id) : 0;
    uint32_t parent = 0;
    if (slot != 0)
    {
        (void)Trace(tree, slot, &parent);
    }
    return parent != 0 ? tree->held[parent - 1].node.id : 0;
}

muiResult muiAccessTree_GetShownChildren(const muiAccessTree* tree, uint64_t id,
                                         uint64_t* childrenOut, uint32_t capacity,
                                         uint32_t* countOut)
{
    if (tree == nullptr || countOut == nullptr || (childrenOut == nullptr && capacity != 0))
    {
        return mui_errorInvalid;
    }
    *countOut = 0;
    uint32_t slot = muiHeldSlotOf(tree, id);
    if (slot == 0)
    {
        return mui_empty;
    }
    // Depth first through flattened nodes: each holds its place in its
    // children, so the order is the children's.
    uint32_t count = 0;
    uint32_t top = 1;
    tree->stack[0] = slot;
    tree->walk[0] = 0;
    while (top != 0)
    {
        uint32_t at = tree->stack[top - 1];
        uint32_t index = tree->walk[top - 1];
        const muiHeldNode* held = &tree->held[at - 1];
        if (index == held->node.childCount)
        {
            top--;
            continue;
        }
        tree->walk[top - 1] = index + 1;
        Status status = StatusOf(tree, at, index);
        if (status == Status_shown)
        {
            if (count < capacity)
            {
                childrenOut[count] = held->children[index];
            }
            count++;
        }
        else if (status == Status_flattened && top < tree->capacity)
        {
            tree->stack[top] = muiHeldSlotOf(tree, held->children[index]);
            tree->walk[top] = 0;
            top++;
        }
    }
    *countOut = count;
    return count <= capacity ? mui_success : mui_errorCapacity;
}

muiResult muiAccessTree_GetBounds(const muiAccessTree* tree, uint64_t id, muiRect* boundsOut)
{
    if (tree == nullptr || boundsOut == nullptr)
    {
        return mui_errorInvalid;
    }
    uint32_t slot = muiHeldSlotOf(tree, id);
    if (slot == 0)
    {
        return mui_empty;
    }
    // The transforms composed, the node's innermost.
    muiDrawTransform m = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    uint32_t steps = 0;
    for (uint32_t at = slot; at != 0 && steps <= tree->count;
         at = tree->held[at - 1].parent, steps++)
    {
        const muiDrawTransform* t = &tree->held[at - 1].node.transform;
        m = (muiDrawTransform){
            t->a * m.a + t->c * m.b, t->b * m.a + t->d * m.b,        t->a * m.c + t->c * m.d,
            t->b * m.c + t->d * m.d, t->a * m.e + t->c * m.f + t->e, t->b * m.e + t->d * m.f + t->f,
        };
    }
    *boundsOut = BoxOf(&m, tree->held[slot - 1].node.bounds);
    return mui_success;
}

// A name as it is joined: parts with spaces between, past the room
// counted but not written.
typedef struct Name
{
    char* buffer;
    size_t capacity;
    size_t length;
    // The first byte with no room, kept to cut on a whole character.
    unsigned char dropped;
} Name;

static void Join(Name* name, const char* text, uint32_t length)
{
    if (text == nullptr || length == 0)
    {
        return;
    }
    for (uint32_t i = name->length != 0 ? 0 : 1; i <= length; i++)
    {
        char byte = i == 0 ? ' ' : text[i - 1];
        if (name->length + 1 < name->capacity)
        {
            name->buffer[name->length] = byte;
        }
        else if (name->length + 1 == name->capacity)
        {
            name->dropped = (unsigned char)byte;
        }
        name->length++;
    }
}

// The text a node gives a name it is part of: its label, or a label
// node's value.
static void JoinTextOf(Name* name, const muiAccessNode* node)
{
    if (node->text[mui_accessLabel] != nullptr)
    {
        Join(name, node->text[mui_accessLabel], node->textLength[mui_accessLabel]);
    }
    else if (node->role == mui_roleLabel)
    {
        Join(name, node->text[mui_accessValue], node->textLength[mui_accessValue]);
    }
}

static void JoinLabellers(Name* name, const muiAccessTree* tree, const muiAccessNode* node)
{
    for (uint32_t i = 0; i < node->linkCount; i++)
    {
        uint32_t slot = node->links[i].kind == mui_relationLabelledBy
                            ? muiHeldSlotOf(tree, node->links[i].target)
                            : 0;
        if (slot != 0)
        {
            JoinTextOf(name, &tree->held[slot - 1].node);
        }
    }
}

// Whether a role takes its name from what is inside it.
static bool IsNamedByContents(muiRole role)
{
    switch (role)
    {
    case mui_roleButton:
    case mui_roleDefaultButton:
    case mui_roleCheckBox:
    case mui_roleRadioButton:
    case mui_roleSwitch:
    case mui_roleLink:
    case mui_roleMenuItem:
    case mui_roleMenuItemCheckBox:
    case mui_roleMenuItemRadio:
    case mui_roleTab:
        return true;
    default:
        return false;
    }
}

// The texts of the labels and images inside a node, in tree order;
// hidden subtrees give none.
static void JoinContents(Name* name, const muiAccessTree* tree, uint32_t slot)
{
    uint32_t count = 0;
    const muiHeldNode* top = &tree->held[slot - 1];
    for (uint32_t k = top->node.childCount; k > 0; k--)
    {
        tree->stack[count++] = muiHeldSlotOf(tree, top->children[k - 1]);
    }
    while (count != 0)
    {
        const muiHeldNode* held = &tree->held[tree->stack[--count] - 1];
        const muiAccessNode* node = &held->node;
        if ((node->flags & mui_accessHidden) != 0)
        {
            continue;
        }
        if (node->role == mui_roleLabel || node->role == mui_roleImage)
        {
            JoinTextOf(name, node);
            continue;
        }
        for (uint32_t k = node->childCount; k > 0 && count < tree->capacity; k--)
        {
            tree->stack[count++] = muiHeldSlotOf(tree, held->children[k - 1]);
        }
    }
}

muiResult muiAccessTree_GetName(const muiAccessTree* tree, uint64_t id, char* buffer,
                                size_t capacity, size_t* lengthOut)
{
    if (tree == nullptr || lengthOut == nullptr || (buffer == nullptr && capacity != 0))
    {
        return mui_errorInvalid;
    }
    Name name = {buffer, capacity, 0, 0};
    uint32_t slot = muiHeldSlotOf(tree, id);
    if (slot != 0)
    {
        const muiAccessNode* node = &tree->held[slot - 1].node;
        JoinTextOf(&name, node);
        if (name.length == 0)
        {
            JoinLabellers(&name, tree, node);
        }
        if (name.length == 0 && IsNamedByContents(node->role))
        {
            JoinContents(&name, tree, slot);
        }
    }
    size_t end = name.length < capacity ? name.length : capacity - 1;
    if (name.length >= capacity && capacity != 0 && (name.dropped & 0xC0) == 0x80)
    {
        // A character cut short: back to its first byte, and before it.
        while (end > 0 && ((unsigned char)buffer[end - 1] & 0xC0) == 0x80)
        {
            end--;
        }
        end -= end > 0 && ((unsigned char)buffer[end - 1] & 0xC0) == 0xC0 ? 1 : 0;
    }
    if (capacity != 0)
    {
        buffer[end] = '\0';
    }
    *lengthOut = name.length;
    return name.length == 0 ? mui_empty : name.length < capacity ? mui_success : mui_errorCapacity;
}
