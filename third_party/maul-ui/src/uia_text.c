// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The UI Automation adapter's Text pattern (record mui-0008, research
// 143): ITextProvider2 on a text input or a text being edited, giving
// the document, the selection and the caret as ranges (src/uia_range.c),
// and the range at a point. Embedded objects have no ranges of their own
// yet.

#include "access_record.h"
#include "uia.h"

#include <stddef.h>

static muiUiaNode* FromText(muiUiaText* self)
{
    return (muiUiaNode*)((char*)self - offsetof(muiUiaNode, text));
}

static HRESULT STDMETHODCALLTYPE TextQuery(muiUiaText* self, REFIID id, void** out)
{
    return muiUiaQuery(FromText(self), id, out);
}

static ULONG STDMETHODCALLTYPE TextAddRef(muiUiaText* self)
{
    return muiUiaAddReference(FromText(self));
}

static ULONG STDMETHODCALLTYPE TextRelease(muiUiaText* self)
{
    muiUiaNode* node = FromText(self);
    ULONG left = (ULONG)(node->references - 1);
    muiUiaRelease(node);
    return left;
}

// A new range of the node's text, or the error to answer.
static HRESULT Give(muiUiaNode* node, uint32_t start, uint32_t end, muiUiaRange** out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    *out = nullptr;
    if (muiUiaNodeFor(node) == nullptr)
    {
        return ELEMENT_GONE;
    }
    *out = muiUiaMakeRange(node, start, end);
    return *out != nullptr ? S_OK : E_OUTOFMEMORY;
}

// An array of one range, or none.
static HRESULT GiveArray(muiUiaRange* range, bool one, SAFEARRAY** out)
{
    *out = SafeArrayCreateVector(VT_UNKNOWN, 0, one ? 1 : 0);
    LONG index = 0;
    HRESULT result = *out == nullptr ? E_OUTOFMEMORY
                     : one           ? SafeArrayPutElement(*out, &index, (IUnknown*)range)
                                     : S_OK;
    if (FAILED(result) && *out != nullptr)
    {
        (void)SafeArrayDestroy(*out);
        *out = nullptr;
    }
    return result;
}

// The selection, a degenerate range at the caret when nothing is
// selected; none when the text is not being edited.
static HRESULT STDMETHODCALLTYPE GetSelection(muiUiaText* self, SAFEARRAY** out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    *out = nullptr;
    muiUiaNode* node = FromText(self);
    const muiAccessNode* held = muiUiaNodeFor(node);
    if (held == nullptr)
    {
        return ELEMENT_GONE;
    }
    const muiAccessTextMarks* marks = &held->marks;
    uint32_t start = marks->anchor < marks->focus ? marks->anchor : marks->focus;
    uint32_t end = marks->anchor < marks->focus ? marks->focus : marks->anchor;
    muiUiaRange* range = marks->selected ? muiUiaMakeRange(node, start, end) : nullptr;
    if (marks->selected && range == nullptr)
    {
        return E_OUTOFMEMORY;
    }
    HRESULT result = GiveArray(range, marks->selected, out);
    if (range != nullptr)
    {
        muiUiaReleaseRange(range);
    }
    return result;
}

static HRESULT STDMETHODCALLTYPE DocumentRange(muiUiaText* self, muiUiaRange** out)
{
    return Give(FromText(self), 0, UINT32_MAX, out);
}

// All of it is visible: a range scrolled out of the field is not known
// without character geometry.
static HRESULT STDMETHODCALLTYPE GetVisibleRanges(muiUiaText* self, SAFEARRAY** out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    *out = nullptr;
    muiUiaRange* range = nullptr;
    HRESULT result = DocumentRange(self, &range);
    if (SUCCEEDED(result))
    {
        result = GiveArray(range, true, out);
        muiUiaReleaseRange(range);
    }
    return result;
}

static HRESULT STDMETHODCALLTYPE RangeFromChild(muiUiaText* self, muiUiaSimple* child,
                                                muiUiaRange** out)
{
    (void)self;
    (void)child;
    if (out != nullptr)
    {
        *out = nullptr;
    }
    return E_INVALIDARG;
}

// The degenerate range before the character at a point, or nearest it;
// the text's start without clusters.
static HRESULT STDMETHODCALLTYPE RangeFromPoint(muiUiaText* self, muiUiaPoint point,
                                                muiUiaRange** out)
{
    muiUiaNode* node = FromText(self);
    const muiAccessNode* held = muiUiaNodeFor(node);
    uint32_t at = 0;
    if (held != nullptr)
    {
        float x = 0.0f;
        float y = 0.0f;
        muiUiaRootPointOf(node->adapter, point.x, point.y, &x, &y);
        if (muiAccessTree_GetTextOffsetAt(node->adapter->tree, held->id, x, y, &at) != mui_success)
        {
            at = 0;
        }
    }
    return Give(node, at, at, out);
}

static HRESULT STDMETHODCALLTYPE SupportedSelection(muiUiaText* self, int* out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    const muiAccessNode* held = muiUiaNodeFor(FromText(self));
    if (held == nullptr)
    {
        return ELEMENT_GONE;
    }
    *out = (held->actions & (1u << mui_actionSetSelection)) != 0 ? TEXT_SELECTION_SINGLE : 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE RangeFromAnnotation(muiUiaText* self, muiUiaSimple* annotation,
                                                     muiUiaRange** out)
{
    return RangeFromChild(self, annotation, out);
}

// The caret, and whether the text has the keyboard's focus.
static HRESULT STDMETHODCALLTYPE GetCaretRange(muiUiaText* self, BOOL* active, muiUiaRange** out)
{
    if (active == nullptr)
    {
        return E_POINTER;
    }
    muiUiaNode* node = FromText(self);
    const muiAccessNode* held = muiUiaNodeFor(node);
    *active = held != nullptr && muiRecordActiveFocus(node->adapter->tree) == held->id;
    uint32_t caret = held != nullptr && held->marks.selected ? held->marks.focus : 0;
    return Give(node, caret, caret, out);
}

static const muiUiaTextTable s_textTable = {
    TextQuery,          TextAddRef,          TextRelease,    GetSelection,
    GetVisibleRanges,   RangeFromChild,      RangeFromPoint, DocumentRange,
    SupportedSelection, RangeFromAnnotation, GetCaretRange};

void muiUiaInitTextPattern(muiUiaNode* node)
{
    node->text.lpVtbl = &s_textTable;
}
