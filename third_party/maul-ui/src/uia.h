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

// UI Automation's errors, as HRESULTs: UIA_E_ELEMENTNOTAVAILABLE,
// UIA_E_NOTSUPPORTED, UIA_E_ELEMENTNOTENABLED and UIA_E_INVALIDOPERATION.
#define ELEMENT_GONE        ((HRESULT)0x80040201)
#define NOT_SUPPORTED       ((HRESULT)0x80040204)
#define ELEMENT_NOT_ENABLED ((HRESULT)0x80040200)
#define INVALID_OPERATION   ((HRESULT)0x80131509)

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
    // UiaGetReservedNotSupportedValue, for a text attribute not given;
    // NULL when the module lacks it.
    HRESULT(WINAPI* reservedNotSupported)(IUnknown**);
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
    muiUiaText text;
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
    // Whether a client listened when the apply under way began, and the
    // focus last shown.
    bool listening;
    uint64_t toldFocus;
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

// The Text pattern (src/uia_text.c), which a node being edited has
// (muiAccessIsEdited): its table set on a new object; a new range of a node's text from one byte
// offset to another (src/uia_range.c), holding a reference to the
// object, with one of its own, or NULL when the heap is out of memory;
// letting go of a reference to a range.
void muiUiaInitTextPattern(muiUiaNode* node);
muiUiaRange* muiUiaMakeRange(muiUiaNode* node, uint32_t start, uint32_t end);
void muiUiaReleaseRange(muiUiaRange* range);

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

// A rectangle where the root is placed, and a node's bounds, in screen
// pixels; a point in screen pixels where the root is placed.
muiUiaRect muiUiaScreenRectOf(const muiUiaAdapter* adapter, muiRect bounds);
muiUiaRect muiUiaScreenRect(const muiUiaAdapter* adapter, uint64_t id);
void muiUiaRootPointOf(const muiUiaAdapter* adapter, double x, double y, float* xOut, float* yOut);

// The top left of the window's client area on the screen, in pixels.
POINT muiUiaClientOrigin(const muiUiaAdapter* adapter);

// UTF-8 as a BSTR in a VARIANT; left empty when there is none.
HRESULT muiUiaSetText(VARIANT* out, const char* text, size_t length);

// What applying an update changed, raised as UI Automation's events
// when a client listens; the focus last, when the one shown moved (its
// active descendant included).
void muiUiaAdded(void* user, const muiAccessTree* tree, uint64_t id);
void muiUiaUpdated(void* user, const muiAccessTree* tree, const muiAccessNode* old);
void muiUiaTellFocus(muiUiaAdapter* adapter);

// Asks the host to perform an action on a node.
HRESULT muiUiaPerform(muiUiaAdapter* adapter, muiAccessAction action, uint64_t target);
HRESULT muiUiaPerformRequest(muiUiaAdapter* adapter, const muiAccessRequest* request);

#endif // MAUL_UI_SRC_UIA_H
