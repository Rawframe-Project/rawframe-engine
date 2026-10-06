// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The UI Automation adapter's provider objects (record mui-0008): one
// object with three interfaces, muiUiaSimple,
// muiUiaFragment and, for the root, its FragmentRoot.

#include "uia.h"

#include <stddef.h>
#include <string.h>

// The interfaces' ids, here rather than from uuid.lib.
static const IID s_unknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const IID s_simple = {
    0xd6dd68d1, 0x86fd, 0x4332, {0x86, 0x66, 0x9a, 0xbe, 0xde, 0xa2, 0xd2, 0x4c}};
static const IID s_fragment = {
    0xf7063da8, 0x8359, 0x439c, {0x92, 0x97, 0xbb, 0xc5, 0x29, 0x9a, 0x7d, 0x87}};
static const IID s_fragmentRoot = {
    0x620ce2a5, 0xab8f, 0x40a9, {0x86, 0xcb, 0xde, 0x3c, 0x75, 0x59, 0x9b, 0x58}};

static muiUiaNode* FromSimple(muiUiaSimple* simple)
{
    return (muiUiaNode*)((char*)simple - offsetof(muiUiaNode, simple));
}

static muiUiaNode* FromFragment(muiUiaFragment* fragment)
{
    return (muiUiaNode*)((char*)fragment - offsetof(muiUiaNode, fragment));
}

static muiUiaNode* FromFragmentRoot(muiUiaFragmentRoot* fragmentRoot)
{
    return (muiUiaNode*)((char*)fragmentRoot - offsetof(muiUiaNode, fragmentRoot));
}

ULONG muiUiaAddReference(muiUiaNode* node)
{
    return (ULONG)InterlockedIncrement(&node->references);
}

static ULONG ReleaseReference(muiUiaNode* node)
{
    LONG left = InterlockedDecrement(&node->references);
    if (left == 0)
    {
        HeapFree(GetProcessHeap(), 0, node);
    }
    return (ULONG)left;
}

void muiUiaRelease(muiUiaNode* node)
{
    (void)ReleaseReference(node);
}

HRESULT muiUiaQuery(muiUiaNode* node, REFIID id, void** out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    if (memcmp(id, &s_unknown, sizeof(IID)) == 0 || memcmp(id, &s_simple, sizeof(IID)) == 0)
    {
        *out = &node->simple;
    }
    else if (memcmp(id, &s_fragment, sizeof(IID)) == 0)
    {
        *out = &node->fragment;
    }
    else if (memcmp(id, &s_fragmentRoot, sizeof(IID)) == 0 && node->id == 0)
    {
        *out = &node->fragmentRoot;
    }
    else
    {
        *out = muiUiaPatternInterface(node, id);
        if (*out == nullptr)
        {
            return E_NOINTERFACE;
        }
    }
    (void)muiUiaAddReference(node);
    return S_OK;
}

const muiAccessNode* muiUiaNodeFor(const muiUiaNode* node)
{
    if (node->adapter == nullptr)
    {
        return nullptr;
    }
    const muiAccessTree* tree = node->adapter->tree;
    return muiAccessTree_Find(tree, node->id != 0 ? node->id : muiAccessTree_GetRoot(tree));
}

// Gives out a node's object with a reference for the caller, or NULL.
static HRESULT GiveFragment(muiUiaAdapter* adapter, uint64_t id, muiUiaFragment** out)
{
    muiUiaNode* node = id != 0 ? muiUiaNodeOf(adapter, id) : nullptr;
    *out = node != nullptr ? &node->fragment : nullptr;
    if (node != nullptr)
    {
        (void)muiUiaAddReference(node);
    }
    return S_OK;
}

// muiUiaSimple.

static HRESULT STDMETHODCALLTYPE SimpleQuery(muiUiaSimple* simple, REFIID id, void** out)
{
    return muiUiaQuery(FromSimple(simple), id, out);
}

static ULONG STDMETHODCALLTYPE SimpleAddRef(muiUiaSimple* simple)
{
    return muiUiaAddReference(FromSimple(simple));
}

static ULONG STDMETHODCALLTYPE SimpleRelease(muiUiaSimple* simple)
{
    return ReleaseReference(FromSimple(simple));
}

static HRESULT STDMETHODCALLTYPE Options(muiUiaSimple* simple, int* out)
{
    (void)simple;
    if (out == nullptr)
    {
        return E_POINTER;
    }
    *out = UIA_OPTION_SERVER_SIDE | UIA_OPTION_COM_THREADING;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Pattern(muiUiaSimple* simple, int pattern, IUnknown** out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    muiUiaNode* node = FromSimple(simple);
    const muiAccessNode* held = muiUiaNodeFor(node);
    *out = held != nullptr ? muiUiaPatternOf(node, held, pattern) : nullptr;
    return held != nullptr ? S_OK : ELEMENT_GONE;
}

static HRESULT STDMETHODCALLTYPE Property(muiUiaSimple* simple, int property, VARIANT* out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    VariantInit(out);
    muiUiaNode* node = FromSimple(simple);
    const muiAccessNode* held = muiUiaNodeFor(node);
    return held != nullptr ? muiUiaPropertyValue(node->adapter, held, property, out) : ELEMENT_GONE;
}

// The window's own provider hosts the root; other nodes have no host.
static HRESULT STDMETHODCALLTYPE Host(muiUiaSimple* simple, muiUiaSimple** out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    *out = nullptr;
    muiUiaNode* node = FromSimple(simple);
    if (node->adapter == nullptr)
    {
        return ELEMENT_GONE;
    }
    return node->id == 0 ? node->adapter->uia.hostProviderFromHwnd(node->adapter->window, out)
                         : S_OK;
}

static const muiUiaSimpleTable s_simpleTable = {SimpleQuery, SimpleAddRef, SimpleRelease, Options,
                                                Pattern,     Property,     Host};

// muiUiaFragment.

static HRESULT STDMETHODCALLTYPE FragmentQuery(muiUiaFragment* fragment, REFIID id, void** out)
{
    return muiUiaQuery(FromFragment(fragment), id, out);
}

static ULONG STDMETHODCALLTYPE FragmentAddRef(muiUiaFragment* fragment)
{
    return muiUiaAddReference(FromFragment(fragment));
}

static ULONG STDMETHODCALLTYPE FragmentRelease(muiUiaFragment* fragment)
{
    return ReleaseReference(FromFragment(fragment));
}

// A node's neighbour among its shown parent's shown children, 0 for none.
static uint64_t SiblingOf(muiUiaAdapter* adapter, uint64_t id, int step)
{
    const muiAccessTree* tree = adapter->tree;
    uint64_t parent = muiAccessTree_GetShownParent(tree, id);
    uint32_t count = 0;
    if (parent == 0 || muiAccessTree_GetShownChildren(tree, parent, adapter->scratch,
                                                      adapter->nodes, &count) != mui_success)
    {
        return 0;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        if (adapter->scratch[i] == id)
        {
            int64_t at = (int64_t)i + step;
            return at >= 0 && at < (int64_t)count ? adapter->scratch[at] : 0;
        }
    }
    return 0;
}

// A node's first or last shown child, 0 for none.
static uint64_t ChildOf(muiUiaAdapter* adapter, uint64_t id, bool last)
{
    uint32_t count = 0;
    if (muiAccessTree_GetShownChildren(adapter->tree, id, adapter->scratch, adapter->nodes,
                                       &count) != mui_success ||
        count == 0)
    {
        return 0;
    }
    return adapter->scratch[last ? count - 1 : 0];
}

static HRESULT STDMETHODCALLTYPE Navigate(muiUiaFragment* fragment, int direction,
                                          muiUiaFragment** out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    *out = nullptr;
    muiUiaNode* node = FromFragment(fragment);
    const muiAccessNode* held = muiUiaNodeFor(node);
    if (held == nullptr)
    {
        return ELEMENT_GONE;
    }
    muiUiaAdapter* adapter = node->adapter;
    // The window's provider takes the root's parent and siblings.
    bool root = node->id == 0;
    uint64_t to = 0;
    switch (direction)
    {
    case NAVIGATE_PARENT:
        to = root ? 0 : muiAccessTree_GetShownParent(adapter->tree, held->id);
        break;
    case NAVIGATE_NEXT:
        to = root ? 0 : SiblingOf(adapter, held->id, 1);
        break;
    case NAVIGATE_PREVIOUS:
        to = root ? 0 : SiblingOf(adapter, held->id, -1);
        break;
    case NAVIGATE_FIRST:
        to = ChildOf(adapter, held->id, false);
        break;
    case NAVIGATE_LAST:
        to = ChildOf(adapter, held->id, true);
        break;
    default:
        return E_INVALIDARG;
    }
    return GiveFragment(adapter, to, out);
}

// The root's id comes from the window; another node's from its id.
static HRESULT STDMETHODCALLTYPE RuntimeId(muiUiaFragment* fragment, SAFEARRAY** out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    *out = nullptr;
    muiUiaNode* node = FromFragment(fragment);
    if (muiUiaNodeFor(node) == nullptr)
    {
        return ELEMENT_GONE;
    }
    if (node->id == 0)
    {
        return E_NOTIMPL;
    }
    const int parts[3] = {APPEND_RUNTIME_ID, (int)(uint32_t)node->id,
                          (int)(uint32_t)(node->id >> 32)};
    SAFEARRAY* array = SafeArrayCreateVector(VT_I4, 0, 3);
    if (array == nullptr)
    {
        return E_OUTOFMEMORY;
    }
    for (LONG i = 0; i < 3; i++)
    {
        (void)SafeArrayPutElement(array, &i, (void*)&parts[i]);
    }
    *out = array;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE FragmentBounds(muiUiaFragment* fragment, muiUiaRect* out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    *out = (muiUiaRect){0};
    muiUiaNode* node = FromFragment(fragment);
    const muiAccessNode* held = muiUiaNodeFor(node);
    if (held == nullptr)
    {
        return ELEMENT_GONE;
    }
    *out = muiUiaScreenRect(node->adapter, held->id);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE EmbeddedRoots(muiUiaFragment* fragment, SAFEARRAY** out)
{
    (void)fragment;
    if (out == nullptr)
    {
        return E_POINTER;
    }
    *out = nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE FragmentSetFocus(muiUiaFragment* fragment)
{
    muiUiaNode* node = FromFragment(fragment);
    const muiAccessNode* held = muiUiaNodeFor(node);
    return held != nullptr ? muiUiaPerform(node->adapter, mui_actionFocus, held->id) : ELEMENT_GONE;
}

static HRESULT STDMETHODCALLTYPE RootOf(muiUiaFragment* fragment, muiUiaFragmentRoot** out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    *out = nullptr;
    muiUiaNode* node = FromFragment(fragment);
    if (muiUiaNodeFor(node) == nullptr)
    {
        return ELEMENT_GONE;
    }
    muiUiaNode* root = node->adapter->root;
    *out = &root->fragmentRoot;
    (void)muiUiaAddReference(root);
    return S_OK;
}

static const muiUiaFragmentTable s_fragmentTable = {
    FragmentQuery,  FragmentAddRef, FragmentRelease,  Navigate, RuntimeId,
    FragmentBounds, EmbeddedRoots,  FragmentSetFocus, RootOf};

// muiUiaFragmentRoot.

static HRESULT STDMETHODCALLTYPE RootQuery(muiUiaFragmentRoot* fragmentRoot, REFIID id, void** out)
{
    return muiUiaQuery(FromFragmentRoot(fragmentRoot), id, out);
}

static ULONG STDMETHODCALLTYPE RootAddRef(muiUiaFragmentRoot* fragmentRoot)
{
    return muiUiaAddReference(FromFragmentRoot(fragmentRoot));
}

static ULONG STDMETHODCALLTYPE RootRelease(muiUiaFragmentRoot* fragmentRoot)
{
    return ReleaseReference(FromFragmentRoot(fragmentRoot));
}

static bool Contains(const muiUiaRect* rect, double x, double y)
{
    return x >= rect->left && x < rect->left + rect->width && y >= rect->top &&
           y < rect->top + rect->height;
}

// The deepest node shown under a point on the screen: among each node's
// shown children, the last drawn that holds it.
static uint64_t NodeAt(muiUiaAdapter* adapter, double x, double y)
{
    uint64_t at = muiAccessTree_GetRoot(adapter->tree);
    for (uint32_t depth = 0; depth < adapter->nodes; depth++)
    {
        uint32_t count = 0;
        if (muiAccessTree_GetShownChildren(adapter->tree, at, adapter->scratch, adapter->nodes,
                                           &count) != mui_success)
        {
            return at;
        }
        uint64_t next = 0;
        for (uint32_t i = count; i > 0 && next == 0; i--)
        {
            const muiUiaRect rect = muiUiaScreenRect(adapter, adapter->scratch[i - 1]);
            next = Contains(&rect, x, y) ? adapter->scratch[i - 1] : 0;
        }
        if (next == 0)
        {
            return at;
        }
        at = next;
    }
    return at;
}

static HRESULT STDMETHODCALLTYPE FromPoint(muiUiaFragmentRoot* fragmentRoot, double x, double y,
                                           muiUiaFragment** out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    *out = nullptr;
    muiUiaNode* node = FromFragmentRoot(fragmentRoot);
    if (muiUiaNodeFor(node) == nullptr)
    {
        return ELEMENT_GONE;
    }
    return GiveFragment(node->adapter, NodeAt(node->adapter, x, y), out);
}

// The focus within the root; none when the root itself has it.
static HRESULT STDMETHODCALLTYPE RootFocus(muiUiaFragmentRoot* fragmentRoot, muiUiaFragment** out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    *out = nullptr;
    muiUiaNode* node = FromFragmentRoot(fragmentRoot);
    if (muiUiaNodeFor(node) == nullptr)
    {
        return ELEMENT_GONE;
    }
    const muiAccessTree* tree = node->adapter->tree;
    uint64_t focus = muiAccessTree_GetFocus(tree);
    return GiveFragment(node->adapter, focus != muiAccessTree_GetRoot(tree) ? focus : 0, out);
}

static const muiUiaFragmentRootTable s_fragmentRootTable = {RootQuery, RootAddRef, RootRelease,
                                                            FromPoint, RootFocus};

muiUiaNode* muiUiaMakeNode(muiUiaAdapter* adapter, uint64_t id)
{
    muiUiaNode* node = HeapAlloc(GetProcessHeap(), 0, sizeof(muiUiaNode));
    if (node != nullptr)
    {
        *node = (muiUiaNode){
            .simple = {&s_simpleTable},
            .fragment = {&s_fragmentTable},
            .fragmentRoot = {&s_fragmentRootTable},
            .references = 1,
            .adapter = adapter,
            .id = id,
        };
        muiUiaInitPatterns(node);
    }
    return node;
}
