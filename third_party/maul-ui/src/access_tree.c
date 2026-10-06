// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The accessibility tree's consumer (record mui-0008): making a tree,
// finding its nodes by id, reading them, and writing the tree as text.

#include "maul-ui/access_tree.h"

#include "access_tree.h"
#include "access_tree_store.h"
#include "allocator.h"

#include <inttypes.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

#define TREE_DEF_COOKIE 0x6D756174u // "muat"

// Slots fit 32 bits with room for the index's doubling.
#define MAX_NODES ((uint32_t)1 << 24)

muiAccessTreeDef muiDefaultAccessTreeDef(void)
{
    return (muiAccessTreeDef){.cookie = TREE_DEF_COOKIE, .nodes = 4096};
}

// Where each part of a tree's block starts.
typedef struct Parts
{
    size_t held;
    size_t free;
    size_t map;
    size_t updates;
    size_t staged;
    size_t retired;
    size_t added;
    size_t stack;
    size_t walk;
    size_t size;
} Parts;

static size_t Add(size_t* at, size_t count, size_t size, size_t alignment)
{
    size_t start = (*at + alignment - 1) / alignment * alignment;
    *at = start + count * size;
    return start;
}

static Parts LayOut(uint32_t nodes, uint32_t indexSize)
{
    size_t at = sizeof(muiAccessTree);
    Parts parts = {
        .held = Add(&at, nodes, sizeof(muiHeldNode), alignof(muiHeldNode)),
        .free = Add(&at, nodes, sizeof(uint32_t), alignof(uint32_t)),
        .map = Add(&at, indexSize, sizeof(uint32_t), alignof(uint32_t)),
        .updates = Add(&at, indexSize, sizeof(muiUpdateSlot), alignof(muiUpdateSlot)),
        .staged = Add(&at, nodes, sizeof(muiStagedNode), alignof(muiStagedNode)),
        // An apply retires at most every node sent and every node held.
        .retired = Add(&at, (size_t)nodes * 2, sizeof(muiRetiredNode), alignof(muiRetiredNode)),
        .added = Add(&at, nodes, sizeof(uint32_t), alignof(uint32_t)),
        .stack = Add(&at, nodes, sizeof(uint32_t), alignof(uint32_t)),
        .walk = Add(&at, nodes, sizeof(uint32_t), alignof(uint32_t)),
    };
    parts.size = at;
    return parts;
}

muiResult muiCreateAccessTree(const muiAccessTreeDef* def, muiAccessTree** treeOut)
{
    if (treeOut != nullptr)
    {
        *treeOut = nullptr;
    }
    if (def == nullptr || treeOut == nullptr || def->cookie != TREE_DEF_COOKIE ||
        !muiIsAllocatorValid(&def->allocator) || def->nodes == 0 || def->nodes > MAX_NODES)
    {
        return mui_errorInvalid;
    }
    // The index stays at most half full.
    uint32_t indexSize = 2;
    while (indexSize < def->nodes * 2)
    {
        indexSize *= 2;
    }
    Parts parts = LayOut(def->nodes, indexSize);
    unsigned char* block = muiAllocate(&def->allocator, parts.size, alignof(max_align_t));
    if (block == nullptr)
    {
        return mui_errorCapacity;
    }
    memset(block, 0, parts.size);
    muiAccessTree* tree = (muiAccessTree*)block;
    *tree = (muiAccessTree){
        .allocator = def->allocator,
        .blockSize = parts.size,
        .capacity = def->nodes,
        .held = (muiHeldNode*)(block + parts.held),
        .free = (uint32_t*)(block + parts.free),
        .map = (uint32_t*)(block + parts.map),
        .updates = (muiUpdateSlot*)(block + parts.updates),
        .mask = indexSize - 1,
        .staged = (muiStagedNode*)(block + parts.staged),
        .retired = (muiRetiredNode*)(block + parts.retired),
        .added = (uint32_t*)(block + parts.added),
        .stack = (uint32_t*)(block + parts.stack),
        .walk = (uint32_t*)(block + parts.walk),
    };
    for (uint32_t i = 0; i < def->nodes; i++)
    {
        tree->free[i] = i + 1;
    }
    tree->freeCount = def->nodes;
    *treeOut = tree;
    return mui_success;
}

void muiFreeHeld(const muiAccessTree* tree, const muiHeldNode* held)
{
    const muiAccessNode* node = &held->node;
    for (uint32_t kind = 0; kind < MUI_ACCESS_TEXTS; kind++)
    {
        if (node->text[kind] != nullptr)
        {
            muiRelease(&tree->allocator, (void*)node->text[kind],
                       (size_t)node->textLength[kind] + 1, 1);
        }
    }
    if (node->links != nullptr)
    {
        muiRelease(&tree->allocator, (void*)node->links, node->linkCount * sizeof(muiAccessLink),
                   alignof(muiAccessLink));
    }
    if (held->children != nullptr)
    {
        muiRelease(&tree->allocator, held->children, node->childCount * sizeof(uint64_t),
                   alignof(uint64_t));
    }
}

void muiDestroyAccessTree(muiAccessTree* tree)
{
    if (tree == nullptr)
    {
        return;
    }
    for (uint32_t slot = 1; slot <= tree->capacity; slot++)
    {
        if (tree->held[slot - 1].node.id != 0)
        {
            muiFreeHeld(tree, &tree->held[slot - 1]);
        }
    }
    const muiAllocator allocator = tree->allocator;
    muiRelease(&allocator, tree, tree->blockSize, alignof(max_align_t));
}

uint32_t muiAccessHome(const muiAccessTree* tree, uint64_t id)
{
    return (uint32_t)((id * 0x9E3779B97F4A7C15ULL) >> 32) & tree->mask;
}

// Where a node's id sits in the index, or where it would go.
static uint32_t PlaceOf(const muiAccessTree* tree, uint64_t id)
{
    uint32_t at = muiAccessHome(tree, id);
    while (tree->map[at] != 0 && tree->held[tree->map[at] - 1].node.id != id)
    {
        at = (at + 1) & tree->mask;
    }
    return at;
}

uint32_t muiHeldSlotOf(const muiAccessTree* tree, uint64_t id)
{
    return id != 0 ? tree->map[PlaceOf(tree, id)] : 0;
}

void muiAccessIndex(muiAccessTree* tree, uint32_t slot)
{
    tree->map[PlaceOf(tree, tree->held[slot - 1].node.id)] = slot;
}

void muiAccessUnindex(muiAccessTree* tree, uint32_t slot)
{
    // Linear probing's deletion: later entries whose home lies outside
    // the gap move back into it.
    uint32_t gap = PlaceOf(tree, tree->held[slot - 1].node.id);
    for (uint32_t at = (gap + 1) & tree->mask; tree->map[at] != 0; at = (at + 1) & tree->mask)
    {
        uint32_t home = muiAccessHome(tree, tree->held[tree->map[at] - 1].node.id);
        bool inside = gap <= at ? home > gap && home <= at : home > gap || home <= at;
        if (!inside)
        {
            tree->map[gap] = tree->map[at];
            gap = at;
        }
    }
    tree->map[gap] = 0;
}

uint64_t muiAccessTree_GetRoot(const muiAccessTree* tree)
{
    return tree != nullptr ? tree->root : 0;
}

uint64_t muiAccessTree_GetFocus(const muiAccessTree* tree)
{
    return tree != nullptr ? tree->focus : 0;
}

uint32_t muiAccessTree_Count(const muiAccessTree* tree)
{
    return tree != nullptr ? tree->count : 0;
}

const muiAccessNode* muiAccessTree_Find(const muiAccessTree* tree, uint64_t id)
{
    uint32_t slot = tree != nullptr ? muiHeldSlotOf(tree, id) : 0;
    return slot != 0 ? &tree->held[slot - 1].node : nullptr;
}

uint64_t muiAccessTree_GetParent(const muiAccessTree* tree, uint64_t id)
{
    uint32_t slot = tree != nullptr ? muiHeldSlotOf(tree, id) : 0;
    uint32_t parent = slot != 0 ? tree->held[slot - 1].parent : 0;
    return parent != 0 ? tree->held[parent - 1].node.id : 0;
}

const uint64_t* muiAccessTree_GetChildren(const muiAccessTree* tree, uint64_t id,
                                          uint32_t* countOut)
{
    uint32_t slot = tree != nullptr ? muiHeldSlotOf(tree, id) : 0;
    if (countOut != nullptr)
    {
        *countOut = slot != 0 ? tree->held[slot - 1].node.childCount : 0;
    }
    return slot != 0 ? tree->held[slot - 1].children : nullptr;
}

static const char* const s_roles[] = {
    "generic",
    "label",
    "image",
    "link",
    "button",
    "defaultButton",
    "checkBox",
    "radioButton",
    "radioGroup",
    "switch",
    "textInput",
    "multilineTextInput",
    "searchInput",
    "passwordInput",
    "numberInput",
    "emailInput",
    "phoneNumberInput",
    "urlInput",
    "dateInput",
    "timeInput",
    "dateTimeInput",
    "comboBox",
    "editableComboBox",
    "listBox",
    "listBoxOption",
    "list",
    "listItem",
    "tree",
    "treeItem",
    "treeGrid",
    "table",
    "row",
    "cell",
    "rowHeader",
    "columnHeader",
    "rowGroup",
    "grid",
    "gridCell",
    "menu",
    "menuBar",
    "menuItem",
    "menuItemCheckBox",
    "menuItemRadio",
    "tab",
    "tabList",
    "tabPanel",
    "toolbar",
    "tooltip",
    "dialog",
    "alertDialog",
    "alert",
    "status",
    "log",
    "timer",
    "progressIndicator",
    "meter",
    "slider",
    "spinButton",
    "scrollBar",
    "scrollView",
    "splitter",
    "group",
    "pane",
    "window",
    "titleBar",
    "heading",
    "paragraph",
    "region",
    "navigation",
    "main",
    "banner",
    "complementary",
    "contentInfo",
    "search",
    "form",
    "article",
    "document",
    "application",
    "figure",
    "caption",
    "note",
    "details",
    "disclosureTriangle",
    "canvas",
    "video",
    "audio",
    "colorWell",
    "terminal",
    "feed",
    "marquee",
};

static_assert(sizeof(s_roles) / sizeof(s_roles[0]) == MUI_ROLE_LAST + 1, "a name for every role");

const char* muiAccessRoleName(muiRole role)
{
    return role <= MUI_ROLE_LAST ? s_roles[role] : "unknown";
}

// Text written so far, past the end counted but not written.
typedef struct Writer
{
    char* buffer;
    size_t capacity;
    size_t length;
} Writer;

static void Put(Writer* writer, const char* bytes, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        if (writer->length + 1 < writer->capacity)
        {
            writer->buffer[writer->length] = bytes[i];
        }
        writer->length++;
    }
}

static void PutText(Writer* writer, const char* text)
{
    Put(writer, text, strlen(text));
}

static void PutCount(Writer* writer, const char* prefix, uint32_t value)
{
    char number[32];
    int count = snprintf(number, sizeof(number), "%s%" PRIu32, prefix, value);
    Put(writer, number, count > 0 ? (size_t)count : 0);
}

static void PutFormat(Writer* writer, const char* format, double a, double b)
{
    char number[64];
    int count = snprintf(number, sizeof(number), format, a, b);
    Put(writer, number, count > 0 ? (size_t)count : 0);
}

// A text in quotes, a quote, a backslash or a control byte escaped.
static void PutQuoted(Writer* writer, const char* text, uint32_t length)
{
    Put(writer, "\"", 1);
    for (uint32_t i = 0; i < length; i++)
    {
        unsigned char byte = (unsigned char)text[i];
        if (byte == '"' || byte == '\\')
        {
            Put(writer, "\\", 1);
            Put(writer, &text[i], 1);
        }
        else if (byte < 0x20 || byte == 0x7F)
        {
            char escaped[8];
            int count = snprintf(escaped, sizeof(escaped), "\\x%02X", byte);
            Put(writer, escaped, (size_t)count);
        }
        else
        {
            Put(writer, &text[i], 1);
        }
    }
    Put(writer, "\"", 1);
}

static const char* const s_textNames[MUI_ACCESS_TEXTS] = {
    "label",    "description",     "value",           "placeholder",
    "shortcut", "roleDescription", "stateDescription"};

static void PutNode(Writer* writer, const muiAccessNode* node, uint32_t depth)
{
    for (uint32_t i = 0; i < depth; i++)
    {
        Put(writer, "  ", 2);
    }
    PutText(writer, muiAccessRoleName(node->role));
    PutCount(writer, " #", (uint32_t)node->id);
    if (node->flags != 0)
    {
        PutCount(writer, " flags=", node->flags);
    }
    if (node->actions != 0)
    {
        PutCount(writer, " actions=", node->actions);
    }
    PutFormat(writer, " %gx%g", (double)node->bounds.width, (double)node->bounds.height);
    PutFormat(writer, " @%g,%g", (double)node->transform.e, (double)node->transform.f);
    for (uint32_t kind = 0; kind < MUI_ACCESS_TEXTS; kind++)
    {
        if (node->text[kind] != nullptr)
        {
            Put(writer, " ", 1);
            PutText(writer, s_textNames[kind]);
            Put(writer, "=", 1);
            PutQuoted(writer, node->text[kind], node->textLength[kind]);
        }
    }
    Put(writer, "\n", 1);
}

muiResult muiAccessTree_Write(const muiAccessTree* tree, char* buffer, size_t capacity,
                              size_t* lengthOut)
{
    if (tree == nullptr || lengthOut == nullptr || (buffer == nullptr && capacity != 0))
    {
        return mui_errorInvalid;
    }
    Writer writer = {buffer, capacity, 0};
    // Tree order through the stack, children pushed last first; the
    // depth of each node is its parent's plus 1, found by walking up.
    uint32_t* stack = tree->stack;
    uint32_t count = 0;
    uint32_t root = muiHeldSlotOf(tree, tree->root);
    if (root != 0)
    {
        stack[count++] = root;
    }
    while (count != 0)
    {
        uint32_t slot = stack[--count];
        const muiHeldNode* held = &tree->held[slot - 1];
        uint32_t depth = 0;
        for (uint32_t at = held->parent; at != 0; at = tree->held[at - 1].parent)
        {
            depth++;
        }
        PutNode(&writer, &held->node, depth);
        for (uint32_t i = held->node.childCount; i > 0; i--)
        {
            uint32_t child = muiHeldSlotOf(tree, held->children[i - 1]);
            if (child != 0 && count < tree->capacity)
            {
                stack[count++] = child;
            }
        }
    }
    if (capacity != 0)
    {
        buffer[writer.length < capacity ? writer.length : capacity - 1] = '\0';
    }
    *lengthOut = writer.length;
    return writer.length < capacity ? mui_success : mui_errorCapacity;
}
