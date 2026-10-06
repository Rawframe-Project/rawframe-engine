// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The UI Automation adapter's control patterns (record mui-0008): which
// a node has, and Invoke, Toggle, ExpandCollapse, ScrollItem and
// SelectionItem, each turned into the host's actions.

#include "uia.h"

#include <stddef.h>
#include <string.h>

static const IID s_invoke = {
    0x54fcb24b, 0xe18e, 0x47a2, {0xb4, 0xd3, 0xec, 0xcb, 0xe7, 0x75, 0x99, 0xa2}};
static const IID s_toggle = {
    0x56d00bd0, 0xc4f4, 0x433c, {0xa8, 0x36, 0x1a, 0x52, 0xa5, 0x7e, 0x08, 0x92}};
static const IID s_expandCollapse = {
    0xd847d3a5, 0xcab0, 0x4a98, {0x8c, 0x32, 0xec, 0xb4, 0x5c, 0x59, 0xad, 0x24}};
static const IID s_value = {
    0xc7935180, 0x6fb3, 0x4201, {0xb1, 0x74, 0x7d, 0xf7, 0x3a, 0xdb, 0xf6, 0x4a}};
static const IID s_rangeValue = {
    0x36dc7aef, 0x33e6, 0x4691, {0xaf, 0xe1, 0x2b, 0xe7, 0x27, 0x4b, 0x3d, 0x33}};
static const IID s_scroll = {
    0xb38b8077, 0x1fc3, 0x42a5, {0x8c, 0xae, 0xd4, 0x0c, 0x22, 0x15, 0x05, 0x5a}};
static const IID s_scrollItem = {
    0x2360c714, 0x4bf1, 0x4b26, {0xba, 0x65, 0x9b, 0x21, 0x31, 0x61, 0x27, 0xeb}};
static const IID s_selectionItem = {
    0x2acad808, 0xb2d4, 0x452d, {0xa4, 0x07, 0x91, 0xff, 0x1a, 0xd1, 0x67, 0xb2}};

// A pattern: its id, its interface's id, and where the interface sits.
typedef struct Pattern
{
    int pattern;
    const IID* id;
    size_t offset;
} Pattern;

static const Pattern s_patterns[] = {
    {PATTERN_INVOKE, &s_invoke, offsetof(muiUiaNode, invoke)},
    {PATTERN_TOGGLE, &s_toggle, offsetof(muiUiaNode, toggle)},
    {PATTERN_EXPAND_COLLAPSE, &s_expandCollapse, offsetof(muiUiaNode, expandCollapse)},
    {PATTERN_VALUE, &s_value, offsetof(muiUiaNode, value)},
    {PATTERN_RANGE_VALUE, &s_rangeValue, offsetof(muiUiaNode, rangeValue)},
    {PATTERN_SCROLL, &s_scroll, offsetof(muiUiaNode, scroll)},
    {PATTERN_SCROLL_ITEM, &s_scrollItem, offsetof(muiUiaNode, scrollItem)},
    {PATTERN_SELECTION_ITEM, &s_selectionItem, offsetof(muiUiaNode, selectionItem)},
};

static bool Can(const muiAccessNode* node, muiAccessAction action)
{
    return (node->actions & (1u << action)) != 0;
}

// Whether a node has a pattern: what it needs is in its flags, actions
// or texts. A node that toggles or is selected by a click does not also
// invoke.
static bool Has(const muiAccessNode* node, int pattern)
{
    uint32_t flags = node->flags;
    switch (pattern)
    {
    case PATTERN_INVOKE:
        return Can(node, mui_actionClick) &&
               (flags & (mui_accessCheckable | mui_accessSelectable)) == 0;
    case PATTERN_TOGGLE:
        return (flags & mui_accessCheckable) != 0;
    case PATTERN_EXPAND_COLLAPSE:
        return (flags & mui_accessExpandable) != 0;
    case PATTERN_VALUE:
        return node->text[mui_accessValue] != nullptr && (flags & mui_accessNumeric) == 0;
    case PATTERN_RANGE_VALUE:
        return (flags & mui_accessNumeric) != 0;
    case PATTERN_SCROLL:
        return (flags & mui_accessScrolls) != 0;
    case PATTERN_SCROLL_ITEM:
        return Can(node, mui_actionScrollIntoView);
    case PATTERN_SELECTION_ITEM:
        return (flags & mui_accessSelectable) != 0;
    default:
        return false;
    }
}

IUnknown* muiUiaPatternOf(muiUiaNode* node, const muiAccessNode* held, int pattern)
{
    for (size_t i = 0; i < sizeof(s_patterns) / sizeof(s_patterns[0]); i++)
    {
        if (s_patterns[i].pattern == pattern && Has(held, pattern))
        {
            (void)muiUiaAddReference(node);
            return (IUnknown*)((char*)node + s_patterns[i].offset);
        }
    }
    return nullptr;
}

void* muiUiaPatternInterface(muiUiaNode* node, REFIID id)
{
    const muiAccessNode* held = muiUiaNodeFor(node);
    for (size_t i = 0; i < sizeof(s_patterns) / sizeof(s_patterns[0]) && held != nullptr; i++)
    {
        if (memcmp(id, s_patterns[i].id, sizeof(IID)) == 0 && Has(held, s_patterns[i].pattern))
        {
            return (char*)node + s_patterns[i].offset;
        }
    }
    return nullptr;
}

// Performs an action on the node an object stands for.
static HRESULT Act(muiUiaNode* node, muiAccessAction action)
{
    const muiAccessNode* held = muiUiaNodeFor(node);
    return held != nullptr ? muiUiaPerform(node->adapter, action, held->id) : ELEMENT_GONE;
}

// Invoke.

static muiUiaNode* FromInvoke(muiUiaInvoke* self)
{
    return (muiUiaNode*)((char*)self - offsetof(muiUiaNode, invoke));
}

static HRESULT STDMETHODCALLTYPE InvokeQuery(muiUiaInvoke* self, REFIID id, void** out)
{
    return muiUiaQuery(FromInvoke(self), id, out);
}

static ULONG STDMETHODCALLTYPE InvokeAddRef(muiUiaInvoke* self)
{
    return muiUiaAddReference(FromInvoke(self));
}

static ULONG STDMETHODCALLTYPE InvokeRelease(muiUiaInvoke* self)
{
    muiUiaNode* node = FromInvoke(self);
    ULONG left = (ULONG)(node->references - 1);
    muiUiaRelease(node);
    return left;
}

static HRESULT STDMETHODCALLTYPE Invoke(muiUiaInvoke* self)
{
    return Act(FromInvoke(self), mui_actionClick);
}

static const muiUiaInvokeTable s_invokeTable = {
    .QueryInterface = InvokeQuery,
    .AddRef = InvokeAddRef,
    .Release = InvokeRelease,
    .Invoke = Invoke,
};

// Toggle.

static muiUiaNode* FromToggle(muiUiaToggle* self)
{
    return (muiUiaNode*)((char*)self - offsetof(muiUiaNode, toggle));
}

static HRESULT STDMETHODCALLTYPE ToggleQuery(muiUiaToggle* self, REFIID id, void** out)
{
    return muiUiaQuery(FromToggle(self), id, out);
}

static ULONG STDMETHODCALLTYPE ToggleAddRef(muiUiaToggle* self)
{
    return muiUiaAddReference(FromToggle(self));
}

static ULONG STDMETHODCALLTYPE ToggleRelease(muiUiaToggle* self)
{
    muiUiaNode* node = FromToggle(self);
    ULONG left = (ULONG)(node->references - 1);
    muiUiaRelease(node);
    return left;
}

static HRESULT STDMETHODCALLTYPE Toggle(muiUiaToggle* self)
{
    return Act(FromToggle(self), mui_actionClick);
}

static HRESULT STDMETHODCALLTYPE ToggleState(muiUiaToggle* self, int* out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    const muiAccessNode* held = muiUiaNodeFor(FromToggle(self));
    if (held == nullptr)
    {
        return ELEMENT_GONE;
    }
    *out = (held->flags & mui_accessMixed) != 0     ? TOGGLE_MIXED
           : (held->flags & mui_accessChecked) != 0 ? TOGGLE_ON
                                                    : TOGGLE_OFF;
    return S_OK;
}

static const muiUiaToggleTable s_toggleTable = {
    .QueryInterface = ToggleQuery,
    .AddRef = ToggleAddRef,
    .Release = ToggleRelease,
    .Toggle = Toggle,
    .get_ToggleState = ToggleState,
};

// ExpandCollapse.

static muiUiaNode* FromExpandCollapse(muiUiaExpandCollapse* self)
{
    return (muiUiaNode*)((char*)self - offsetof(muiUiaNode, expandCollapse));
}

static HRESULT STDMETHODCALLTYPE ExpandQuery(muiUiaExpandCollapse* self, REFIID id, void** out)
{
    return muiUiaQuery(FromExpandCollapse(self), id, out);
}

static ULONG STDMETHODCALLTYPE ExpandAddRef(muiUiaExpandCollapse* self)
{
    return muiUiaAddReference(FromExpandCollapse(self));
}

static ULONG STDMETHODCALLTYPE ExpandRelease(muiUiaExpandCollapse* self)
{
    muiUiaNode* node = FromExpandCollapse(self);
    ULONG left = (ULONG)(node->references - 1);
    muiUiaRelease(node);
    return left;
}

static HRESULT STDMETHODCALLTYPE Expand(muiUiaExpandCollapse* self)
{
    return Act(FromExpandCollapse(self), mui_actionExpand);
}

static HRESULT STDMETHODCALLTYPE Collapse(muiUiaExpandCollapse* self)
{
    return Act(FromExpandCollapse(self), mui_actionCollapse);
}

static HRESULT STDMETHODCALLTYPE ExpandState(muiUiaExpandCollapse* self, int* out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    const muiAccessNode* held = muiUiaNodeFor(FromExpandCollapse(self));
    if (held == nullptr)
    {
        return ELEMENT_GONE;
    }
    *out = (held->flags & mui_accessExpanded) != 0 ? EXPAND_EXPANDED : EXPAND_COLLAPSED;
    return S_OK;
}

static const muiUiaExpandCollapseTable s_expandCollapseTable = {
    .QueryInterface = ExpandQuery,
    .AddRef = ExpandAddRef,
    .Release = ExpandRelease,
    .Expand = Expand,
    .Collapse = Collapse,
    .get_ExpandCollapseState = ExpandState,
};

// ScrollItem.

static muiUiaNode* FromScrollItem(muiUiaScrollItem* self)
{
    return (muiUiaNode*)((char*)self - offsetof(muiUiaNode, scrollItem));
}

static HRESULT STDMETHODCALLTYPE ScrollItemQuery(muiUiaScrollItem* self, REFIID id, void** out)
{
    return muiUiaQuery(FromScrollItem(self), id, out);
}

static ULONG STDMETHODCALLTYPE ScrollItemAddRef(muiUiaScrollItem* self)
{
    return muiUiaAddReference(FromScrollItem(self));
}

static ULONG STDMETHODCALLTYPE ScrollItemRelease(muiUiaScrollItem* self)
{
    muiUiaNode* node = FromScrollItem(self);
    ULONG left = (ULONG)(node->references - 1);
    muiUiaRelease(node);
    return left;
}

static HRESULT STDMETHODCALLTYPE ScrollIntoView(muiUiaScrollItem* self)
{
    return Act(FromScrollItem(self), mui_actionScrollIntoView);
}

static const muiUiaScrollItemTable s_scrollItemTable = {
    .QueryInterface = ScrollItemQuery,
    .AddRef = ScrollItemAddRef,
    .Release = ScrollItemRelease,
    .ScrollIntoView = ScrollIntoView,
};

// SelectionItem: selecting is a click, as on the screen.

static muiUiaNode* FromSelectionItem(muiUiaSelectionItem* self)
{
    return (muiUiaNode*)((char*)self - offsetof(muiUiaNode, selectionItem));
}

static HRESULT STDMETHODCALLTYPE SelectionQuery(muiUiaSelectionItem* self, REFIID id, void** out)
{
    return muiUiaQuery(FromSelectionItem(self), id, out);
}

static ULONG STDMETHODCALLTYPE SelectionAddRef(muiUiaSelectionItem* self)
{
    return muiUiaAddReference(FromSelectionItem(self));
}

static ULONG STDMETHODCALLTYPE SelectionRelease(muiUiaSelectionItem* self)
{
    muiUiaNode* node = FromSelectionItem(self);
    ULONG left = (ULONG)(node->references - 1);
    muiUiaRelease(node);
    return left;
}

// Clicks a node unless it already is, or is not, selected as wanted.
static HRESULT SelectAs(muiUiaSelectionItem* self, bool selected)
{
    muiUiaNode* node = FromSelectionItem(self);
    const muiAccessNode* held = muiUiaNodeFor(node);
    if (held == nullptr)
    {
        return ELEMENT_GONE;
    }
    bool is = (held->flags & mui_accessSelected) != 0;
    return is == selected ? S_OK : muiUiaPerform(node->adapter, mui_actionClick, held->id);
}

static HRESULT STDMETHODCALLTYPE Select(muiUiaSelectionItem* self)
{
    return SelectAs(self, true);
}

static HRESULT STDMETHODCALLTYPE RemoveFromSelection(muiUiaSelectionItem* self)
{
    return SelectAs(self, false);
}

static HRESULT STDMETHODCALLTYPE IsSelected(muiUiaSelectionItem* self, BOOL* out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    const muiAccessNode* held = muiUiaNodeFor(FromSelectionItem(self));
    if (held == nullptr)
    {
        return ELEMENT_GONE;
    }
    *out = (held->flags & mui_accessSelected) != 0;
    return S_OK;
}

// Whether a role holds items one selects.
static bool IsSelectionContainer(muiRole role)
{
    switch (role)
    {
    case mui_roleListBox:
    case mui_roleList:
    case mui_roleTree:
    case mui_roleTreeGrid:
    case mui_roleGrid:
    case mui_roleTable:
    case mui_roleTabList:
    case mui_roleRadioGroup:
    case mui_roleMenu:
    case mui_roleMenuBar:
    case mui_roleComboBox:
    case mui_roleEditableComboBox:
        return true;
    default:
        return false;
    }
}

static HRESULT STDMETHODCALLTYPE Container(muiUiaSelectionItem* self, muiUiaSimple** out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    *out = nullptr;
    muiUiaNode* node = FromSelectionItem(self);
    const muiAccessNode* held = muiUiaNodeFor(node);
    if (held == nullptr)
    {
        return ELEMENT_GONE;
    }
    const muiAccessTree* tree = node->adapter->tree;
    uint32_t steps = 0;
    for (uint64_t at = muiAccessTree_GetParent(tree, held->id);
         at != 0 && steps < node->adapter->nodes; at = muiAccessTree_GetParent(tree, at), steps++)
    {
        if (IsSelectionContainer(muiAccessTree_Find(tree, at)->role))
        {
            muiUiaNode* container = muiUiaNodeOf(node->adapter, at);
            if (container != nullptr)
            {
                (void)muiUiaAddReference(container);
                *out = &container->simple;
            }
            return S_OK;
        }
    }
    return S_OK;
}

static const muiUiaSelectionItemTable s_selectionItemTable = {
    .QueryInterface = SelectionQuery,
    .AddRef = SelectionAddRef,
    .Release = SelectionRelease,
    .Select = Select,
    .AddToSelection = Select,
    .RemoveFromSelection = RemoveFromSelection,
    .get_IsSelected = IsSelected,
    .get_SelectionContainer = Container,
};

void muiUiaInitPatterns(muiUiaNode* node)
{
    node->invoke.lpVtbl = &s_invokeTable;
    node->toggle.lpVtbl = &s_toggleTable;
    node->expandCollapse.lpVtbl = &s_expandCollapseTable;
    node->scrollItem.lpVtbl = &s_scrollItemTable;
    node->selectionItem.lpVtbl = &s_selectionItemTable;
    muiUiaInitValuePatterns(node);
}
