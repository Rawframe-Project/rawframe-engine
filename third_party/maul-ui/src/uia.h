// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The UI Automation adapter (record mui-0008): the adapter, its provider
// objects, and the UI Automation functions it loads.

#ifndef MAUL_UI_SRC_UIA_H
#define MAUL_UI_SRC_UIA_H

#include "id_map.h"
#include "uia_com.h"
#include "uia_ids.h"

#include "maul-ui/access_uia.h"

// UI Automation's errors, as HRESULTs: UIA_E_ELEMENTNOTAVAILABLE and
// UIA_E_NOTSUPPORTED.
#define ELEMENT_GONE  ((HRESULT)0x80040201)
#define NOT_SUPPORTED ((HRESULT)0x80040204)

// The functions of uiautomationcore.dll the adapter calls, loaded when it
// is made, as MinGW has no import library of them.
typedef struct muiUiaFunctions
{
    HMODULE module;
    HRESULT(WINAPI* hostProviderFromHwnd)(HWND, muiUiaSimple**);
    LRESULT(WINAPI* returnRawElementProvider)(HWND, WPARAM, LPARAM, muiUiaSimple*);
    HRESULT(WINAPI* disconnectProvider)(muiUiaSimple*);
    BOOL(WINAPI* clientsAreListening)(void);
    HRESULT(WINAPI* raiseEvent)(muiUiaSimple*, int);
    HRESULT(WINAPI* raisePropertyChanged)(muiUiaSimple*, int, VARIANT, VARIANT);
} muiUiaFunctions;

// A provider object: one per node UI Automation asked for, and the root,
// which stands for whichever node is the tree's root. Once detached, it
// answers UIA_E_ELEMENTNOTAVAILABLE until its last reference goes. Each
// interface is
// a pointer to its table; the patterns are given where the node has what
// they need.
typedef struct muiUiaNode
{
    muiUiaSimple simple;
    muiUiaFragment fragment;
    muiUiaFragmentRoot fragmentRoot;
    muiUiaInvoke invoke;
    muiUiaToggle toggle;
    muiUiaExpandCollapse expandCollapse;
    muiUiaValue value;
    muiUiaRangeValue rangeValue;
    muiUiaScroll scroll;
    muiUiaScrollItem scrollItem;
    muiUiaSelectionItem selectionItem;
    LONG references;
    // NULL once detached.
    muiUiaAdapter* adapter;
    // The node's id; 0 for the root.
    uint64_t id;
} muiUiaNode;

struct muiUiaAdapter
{
    muiAllocator allocator;
    size_t blockSize;
    muiAccessTree* tree;
    HWND window;
    float scale;
    muiUiaActionFunction action;
    void* user;
    muiUiaFunctions uia;
    // Whether a client listened when the apply under way began.
    bool listening;
    muiUiaNode* root;
    // Provider objects by node id, each holding a reference.
    muiIdMap objects;
    // Room for any node's shown children.
    uint64_t* scratch;
    uint32_t nodes;
};

// A new provider object with one reference, or NULL when the process
// heap is out of memory.
muiUiaNode* muiUiaMakeNode(muiUiaAdapter* adapter, uint64_t id);

// Takes and lets go of a reference.
ULONG muiUiaAddReference(muiUiaNode* node);
void muiUiaRelease(muiUiaNode* node);

// Answers QueryInterface for any of an object's interfaces.
HRESULT muiUiaQuery(muiUiaNode* node, REFIID id, void** out);

// Points a new object's pattern interfaces at their tables: all of them,
// and Value's, RangeValue's and Scroll's.
void muiUiaInitPatterns(muiUiaNode* node);
void muiUiaInitValuePatterns(muiUiaNode* node);

// The interface of a pattern a node has, with a reference for the
// caller, or NULL.
IUnknown* muiUiaPatternOf(muiUiaNode* node, const muiAccessNode* held, int pattern);

// The pattern interface an id names, if the node has the pattern.
void* muiUiaPatternInterface(muiUiaNode* node, REFIID id);

// The provider object of a node held, made when first asked for; the
// root's for the tree's root. NULL for a node not held or no memory. No
// reference is added.
muiUiaNode* muiUiaNodeOf(muiUiaAdapter* adapter, uint64_t id);

// The node an object stands for, or NULL when detached or gone.
const muiAccessNode* muiUiaNodeFor(const muiUiaNode* node);

// A property's value for a node held.
HRESULT muiUiaPropertyValue(muiUiaAdapter* adapter, const muiAccessNode* node, int property,
                            VARIANT* out);

// A node's bounds in screen pixels.
muiUiaRect muiUiaScreenRect(const muiUiaAdapter* adapter, uint64_t id);

// The top left of the window's client area on the screen, in pixels.
POINT muiUiaClientOrigin(const muiUiaAdapter* adapter);

// UTF-8 as a BSTR in a VARIANT; left empty when there is none.
HRESULT muiUiaSetText(VARIANT* out, const char* text, size_t length);

// What applying an update changed, raised as UI Automation's events
// when a client listens.
void muiUiaAdded(void* user, const muiAccessTree* tree, uint64_t id);
void muiUiaUpdated(void* user, const muiAccessTree* tree, const muiAccessNode* old);
void muiUiaFocusMoved(void* user, const muiAccessTree* tree, uint64_t old, uint64_t focus);

// Asks the host to perform an action on a node.
HRESULT muiUiaPerform(muiUiaAdapter* adapter, muiAccessAction action, uint64_t target);
HRESULT muiUiaPerformRequest(muiUiaAdapter* adapter, const muiAccessRequest* request);

#endif // MAUL_UI_SRC_UIA_H
