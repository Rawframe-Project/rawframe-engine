// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The UI Automation interfaces the adapter implements, the providers and
// their control patterns (record mui-0008), declared here rather than
// taken from uiautomationcore.h: the Windows
// SDK's UI Automation headers define const variables, which in C every
// file including them defines again, so a program including them too
// could not link. Their names are the adapter's own; their layouts and
// values are UI Automation's, which test_uia.c checks against the
// headers at compile time.

#ifndef MAUL_UI_SRC_UIA_COM_H
#define MAUL_UI_SRC_UIA_COM_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// IUnknown, VARIANT, BSTR and SAFEARRAY.
#include <ole2.h>

// ProviderOptions.
enum
{
    UIA_OPTION_SERVER_SIDE = 0x2,
    UIA_OPTION_COM_THREADING = 0x20,
};

// NavigateDirection.
enum
{
    NAVIGATE_PARENT = 0,
    NAVIGATE_NEXT = 1,
    NAVIGATE_PREVIOUS = 2,
    NAVIGATE_FIRST = 3,
    NAVIGATE_LAST = 4,
};

// ToggleState.
enum
{
    TOGGLE_OFF = 0,
    TOGGLE_ON = 1,
    TOGGLE_MIXED = 2,
};

// ExpandCollapseState.
enum
{
    EXPAND_COLLAPSED = 0,
    EXPAND_EXPANDED = 1,
};

// ScrollAmount.
enum
{
    AMOUNT_LARGE_BACK = 0,
    AMOUNT_SMALL_BACK = 1,
    AMOUNT_LARGE_FORWARD = 3,
    AMOUNT_SMALL_FORWARD = 4,
};

// UiaRect: a rectangle on the screen, in pixels.
typedef struct muiUiaRect
{
    double left;
    double top;
    double width;
    double height;
} muiUiaRect;

typedef struct muiUiaSimple muiUiaSimple;
typedef struct muiUiaFragment muiUiaFragment;
typedef struct muiUiaFragmentRoot muiUiaFragmentRoot;

// IRawElementProviderSimple.
typedef struct muiUiaSimpleTable
{
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(muiUiaSimple* self, REFIID id, void** out);
    ULONG(STDMETHODCALLTYPE* AddRef)(muiUiaSimple* self);
    ULONG(STDMETHODCALLTYPE* Release)(muiUiaSimple* self);
    HRESULT(STDMETHODCALLTYPE* get_ProviderOptions)(muiUiaSimple* self, int* out);
    HRESULT(STDMETHODCALLTYPE* GetPatternProvider)(muiUiaSimple* self, int pattern, IUnknown** out);
    HRESULT(STDMETHODCALLTYPE* GetPropertyValue)(muiUiaSimple* self, int property, VARIANT* out);
    HRESULT(STDMETHODCALLTYPE* get_HostRawElementProvider)(muiUiaSimple* self, muiUiaSimple** out);
} muiUiaSimpleTable;

struct muiUiaSimple
{
    const muiUiaSimpleTable* lpVtbl;
};

// IRawElementProviderFragment.
typedef struct muiUiaFragmentTable
{
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(muiUiaFragment* self, REFIID id, void** out);
    ULONG(STDMETHODCALLTYPE* AddRef)(muiUiaFragment* self);
    ULONG(STDMETHODCALLTYPE* Release)(muiUiaFragment* self);
    HRESULT(STDMETHODCALLTYPE* Navigate)(muiUiaFragment* self, int direction, muiUiaFragment** out);
    HRESULT(STDMETHODCALLTYPE* GetRuntimeId)(muiUiaFragment* self, SAFEARRAY** out);
    HRESULT(STDMETHODCALLTYPE* get_BoundingRectangle)(muiUiaFragment* self, muiUiaRect* out);
    HRESULT(STDMETHODCALLTYPE* GetEmbeddedFragmentRoots)(muiUiaFragment* self, SAFEARRAY** out);
    HRESULT(STDMETHODCALLTYPE* SetFocus)(muiUiaFragment* self);
    HRESULT(STDMETHODCALLTYPE* get_FragmentRoot)(muiUiaFragment* self, muiUiaFragmentRoot** out);
} muiUiaFragmentTable;

struct muiUiaFragment
{
    const muiUiaFragmentTable* lpVtbl;
};

// IRawElementProviderFragmentRoot.
typedef struct muiUiaFragmentRootTable
{
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(muiUiaFragmentRoot* self, REFIID id, void** out);
    ULONG(STDMETHODCALLTYPE* AddRef)(muiUiaFragmentRoot* self);
    ULONG(STDMETHODCALLTYPE* Release)(muiUiaFragmentRoot* self);
    HRESULT(STDMETHODCALLTYPE* ElementProviderFromPoint)(muiUiaFragmentRoot* self, double x,
                                                         double y, muiUiaFragment** out);
    HRESULT(STDMETHODCALLTYPE* GetFocus)(muiUiaFragmentRoot* self, muiUiaFragment** out);
} muiUiaFragmentRootTable;

struct muiUiaFragmentRoot
{
    const muiUiaFragmentRootTable* lpVtbl;
};

// The control patterns.

typedef struct muiUiaInvoke muiUiaInvoke;

// IInvokeProvider.
typedef struct muiUiaInvokeTable
{
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(muiUiaInvoke* self, REFIID id, void** out);
    ULONG(STDMETHODCALLTYPE* AddRef)(muiUiaInvoke* self);
    ULONG(STDMETHODCALLTYPE* Release)(muiUiaInvoke* self);
    HRESULT(STDMETHODCALLTYPE* Invoke)(muiUiaInvoke* self);
} muiUiaInvokeTable;

struct muiUiaInvoke
{
    const muiUiaInvokeTable* lpVtbl;
};

typedef struct muiUiaToggle muiUiaToggle;

// IToggleProvider.
typedef struct muiUiaToggleTable
{
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(muiUiaToggle* self, REFIID id, void** out);
    ULONG(STDMETHODCALLTYPE* AddRef)(muiUiaToggle* self);
    ULONG(STDMETHODCALLTYPE* Release)(muiUiaToggle* self);
    HRESULT(STDMETHODCALLTYPE* Toggle)(muiUiaToggle* self);
    HRESULT(STDMETHODCALLTYPE* get_ToggleState)(muiUiaToggle* self, int* out);
} muiUiaToggleTable;

struct muiUiaToggle
{
    const muiUiaToggleTable* lpVtbl;
};

typedef struct muiUiaExpandCollapse muiUiaExpandCollapse;

// IExpandCollapseProvider.
typedef struct muiUiaExpandCollapseTable
{
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(muiUiaExpandCollapse* self, REFIID id, void** out);
    ULONG(STDMETHODCALLTYPE* AddRef)(muiUiaExpandCollapse* self);
    ULONG(STDMETHODCALLTYPE* Release)(muiUiaExpandCollapse* self);
    HRESULT(STDMETHODCALLTYPE* Expand)(muiUiaExpandCollapse* self);
    HRESULT(STDMETHODCALLTYPE* Collapse)(muiUiaExpandCollapse* self);
    HRESULT(STDMETHODCALLTYPE* get_ExpandCollapseState)(muiUiaExpandCollapse* self, int* out);
} muiUiaExpandCollapseTable;

struct muiUiaExpandCollapse
{
    const muiUiaExpandCollapseTable* lpVtbl;
};

typedef struct muiUiaValue muiUiaValue;

// IValueProvider.
typedef struct muiUiaValueTable
{
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(muiUiaValue* self, REFIID id, void** out);
    ULONG(STDMETHODCALLTYPE* AddRef)(muiUiaValue* self);
    ULONG(STDMETHODCALLTYPE* Release)(muiUiaValue* self);
    HRESULT(STDMETHODCALLTYPE* SetValue)(muiUiaValue* self, LPCWSTR value);
    HRESULT(STDMETHODCALLTYPE* get_Value)(muiUiaValue* self, BSTR* out);
    HRESULT(STDMETHODCALLTYPE* get_IsReadOnly)(muiUiaValue* self, BOOL* out);
} muiUiaValueTable;

struct muiUiaValue
{
    const muiUiaValueTable* lpVtbl;
};

typedef struct muiUiaRangeValue muiUiaRangeValue;

// IRangeValueProvider.
typedef struct muiUiaRangeValueTable
{
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(muiUiaRangeValue* self, REFIID id, void** out);
    ULONG(STDMETHODCALLTYPE* AddRef)(muiUiaRangeValue* self);
    ULONG(STDMETHODCALLTYPE* Release)(muiUiaRangeValue* self);
    HRESULT(STDMETHODCALLTYPE* SetValue)(muiUiaRangeValue* self, double value);
    HRESULT(STDMETHODCALLTYPE* get_Value)(muiUiaRangeValue* self, double* out);
    HRESULT(STDMETHODCALLTYPE* get_IsReadOnly)(muiUiaRangeValue* self, BOOL* out);
    HRESULT(STDMETHODCALLTYPE* get_Maximum)(muiUiaRangeValue* self, double* out);
    HRESULT(STDMETHODCALLTYPE* get_Minimum)(muiUiaRangeValue* self, double* out);
    HRESULT(STDMETHODCALLTYPE* get_LargeChange)(muiUiaRangeValue* self, double* out);
    HRESULT(STDMETHODCALLTYPE* get_SmallChange)(muiUiaRangeValue* self, double* out);
} muiUiaRangeValueTable;

struct muiUiaRangeValue
{
    const muiUiaRangeValueTable* lpVtbl;
};

typedef struct muiUiaScroll muiUiaScroll;

// IScrollProvider.
typedef struct muiUiaScrollTable
{
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(muiUiaScroll* self, REFIID id, void** out);
    ULONG(STDMETHODCALLTYPE* AddRef)(muiUiaScroll* self);
    ULONG(STDMETHODCALLTYPE* Release)(muiUiaScroll* self);
    HRESULT(STDMETHODCALLTYPE* Scroll)(muiUiaScroll* self, int horizontal, int vertical);
    HRESULT(STDMETHODCALLTYPE* SetScrollPercent)(muiUiaScroll* self, double horizontal,
                                                 double vertical);
    HRESULT(STDMETHODCALLTYPE* get_HorizontalScrollPercent)(muiUiaScroll* self, double* out);
    HRESULT(STDMETHODCALLTYPE* get_VerticalScrollPercent)(muiUiaScroll* self, double* out);
    HRESULT(STDMETHODCALLTYPE* get_HorizontalViewSize)(muiUiaScroll* self, double* out);
    HRESULT(STDMETHODCALLTYPE* get_VerticalViewSize)(muiUiaScroll* self, double* out);
    HRESULT(STDMETHODCALLTYPE* get_HorizontallyScrollable)(muiUiaScroll* self, BOOL* out);
    HRESULT(STDMETHODCALLTYPE* get_VerticallyScrollable)(muiUiaScroll* self, BOOL* out);
} muiUiaScrollTable;

struct muiUiaScroll
{
    const muiUiaScrollTable* lpVtbl;
};

typedef struct muiUiaScrollItem muiUiaScrollItem;

// IScrollItemProvider.
typedef struct muiUiaScrollItemTable
{
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(muiUiaScrollItem* self, REFIID id, void** out);
    ULONG(STDMETHODCALLTYPE* AddRef)(muiUiaScrollItem* self);
    ULONG(STDMETHODCALLTYPE* Release)(muiUiaScrollItem* self);
    HRESULT(STDMETHODCALLTYPE* ScrollIntoView)(muiUiaScrollItem* self);
} muiUiaScrollItemTable;

struct muiUiaScrollItem
{
    const muiUiaScrollItemTable* lpVtbl;
};

typedef struct muiUiaSelectionItem muiUiaSelectionItem;

// ISelectionItemProvider.
typedef struct muiUiaSelectionItemTable
{
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(muiUiaSelectionItem* self, REFIID id, void** out);
    ULONG(STDMETHODCALLTYPE* AddRef)(muiUiaSelectionItem* self);
    ULONG(STDMETHODCALLTYPE* Release)(muiUiaSelectionItem* self);
    HRESULT(STDMETHODCALLTYPE* Select)(muiUiaSelectionItem* self);
    HRESULT(STDMETHODCALLTYPE* AddToSelection)(muiUiaSelectionItem* self);
    HRESULT(STDMETHODCALLTYPE* RemoveFromSelection)(muiUiaSelectionItem* self);
    HRESULT(STDMETHODCALLTYPE* get_IsSelected)(muiUiaSelectionItem* self, BOOL* out);
    HRESULT(STDMETHODCALLTYPE* get_SelectionContainer)(muiUiaSelectionItem* self,
                                                       muiUiaSimple** out);
} muiUiaSelectionItemTable;

struct muiUiaSelectionItem
{
    const muiUiaSelectionItemTable* lpVtbl;
};

#endif // MAUL_UI_SRC_UIA_COM_H
