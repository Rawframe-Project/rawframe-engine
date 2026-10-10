// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The UI Automation adapter's text ranges (record mui-0008, research
// 143): two byte offsets into a node's value text, normalized and moved
// by unit through the boundaries every adapter shares, read as UTF-16,
// and selected through the host. The text may change while a client
// holds a range: each use keeps the offsets within it.

#include "access_text.h"
#include "uia.h"

#include <string.h>

// UIA_IsReadOnlyAttributeId.
#define ATTRIBUTE_READ_ONLY 40015

static const IID s_unknown = {
    0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID s_range = {
    0x5347ad7b, 0xc355, 0x46f8, {0xaf, 0xf5, 0x90, 0x90, 0x33, 0x58, 0x2f, 0x63}};

struct muiUiaRange
{
    const muiUiaRangeTable* lpVtbl;
    LONG references;
    // A reference held.
    muiUiaNode* node;
    uint32_t start;
    uint32_t end;
};

static const muiUiaRangeTable s_rangeTable;

muiUiaRange* muiUiaMakeRange(muiUiaNode* node, uint32_t start, uint32_t end)
{
    muiUiaRange* range = HeapAlloc(GetProcessHeap(), 0, sizeof(muiUiaRange));
    if (range != nullptr)
    {
        (void)muiUiaAddReference(node);
        *range = (muiUiaRange){&s_rangeTable, 1, node, start, end};
    }
    return range;
}

// The unit of the shared boundaries a UI Automation unit is: format is
// words (runs carry no attributes yet), a page the document.
static muiAccessUnit UnitOf(int unit)
{
    switch (unit)
    {
    case UNIT_CHARACTER:
        return mui_unitCharacter;
    case UNIT_FORMAT:
    case UNIT_WORD:
        return mui_unitWord;
    case UNIT_LINE:
        return mui_unitLine;
    case UNIT_PARAGRAPH:
        return mui_unitParagraph;
    default:
        return mui_unitDocument;
    }
}

// The text a range reads, its offsets kept within it at the starts of
// characters; false for a node gone.
static bool TextOf(muiUiaRange* range, muiAccessText* textOut)
{
    const muiAccessNode* held = muiUiaNodeFor(range->node);
    if (held == nullptr)
    {
        return false;
    }
    *textOut = muiAccessValueOf(held);
    uint32_t start = range->start < textOut->length ? range->start : textOut->length;
    uint32_t end = range->end < textOut->length ? range->end : textOut->length;
    range->start = muiAccessByteOfPoints(textOut, muiAccessPointsBefore(textOut->bytes, start));
    range->end = muiAccessByteOfPoints(textOut, muiAccessPointsBefore(textOut->bytes, end));
    range->end = range->end > range->start ? range->end : range->start;
    return true;
}

static bool IsBoundary(const muiAccessText* text, muiAccessUnit unit, uint32_t at)
{
    return at == 0 || at == text->length ||
           muiAccessBoundaryAfter(text, unit, muiAccessBoundaryBefore(text, unit, at)) == at;
}

// The start of the unit holding a byte; at a boundary, the unit after it;
// at the text's end, the last unit.
static uint32_t UnitStart(const muiAccessText* text, muiAccessUnit unit, uint32_t at)
{
    if (at == text->length)
    {
        return muiAccessBoundaryBefore(text, unit, at);
    }
    return IsBoundary(text, unit, at) ? at : muiAccessBoundaryBefore(text, unit, at);
}

// Moves a byte by count boundaries of a unit, forward or back, no
// further than the text's ends, and when units stays, not onto the end,
// where no unit starts; how many it moved.
static int Step(const muiAccessText* text, muiAccessUnit unit, uint32_t* at, int count, bool units)
{
    int moved = 0;
    while (moved < count)
    {
        uint32_t next = muiAccessBoundaryAfter(text, unit, *at);
        if (next == *at || (units && next == text->length))
        {
            break;
        }
        *at = next;
        moved++;
    }
    while (moved > count)
    {
        if (*at == 0)
        {
            break;
        }
        *at = muiAccessBoundaryBefore(text, unit, *at);
        moved--;
    }
    return moved;
}

static HRESULT STDMETHODCALLTYPE Query(muiUiaRange* self, REFIID id, void** out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    if (memcmp(id, &s_unknown, sizeof(IID)) != 0 && memcmp(id, &s_range, sizeof(IID)) != 0)
    {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    *out = self;
    (void)InterlockedIncrement(&self->references);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE AddRef(muiUiaRange* self)
{
    return (ULONG)InterlockedIncrement(&self->references);
}

static ULONG STDMETHODCALLTYPE Release(muiUiaRange* self)
{
    LONG left = InterlockedDecrement(&self->references);
    if (left == 0)
    {
        muiUiaRelease(self->node);
        HeapFree(GetProcessHeap(), 0, self);
    }
    return (ULONG)left;
}

void muiUiaReleaseRange(muiUiaRange* range)
{
    (void)Release(range);
}

static HRESULT STDMETHODCALLTYPE Clone(muiUiaRange* self, muiUiaRange** out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    *out = muiUiaMakeRange(self->node, self->start, self->end);
    return *out != nullptr ? S_OK : E_OUTOFMEMORY;
}

// Whether another range is one of the same node's text.
static bool IsSameText(const muiUiaRange* range, const muiUiaRange* other)
{
    return other != nullptr && other->lpVtbl == &s_rangeTable && other->node == range->node;
}

static HRESULT STDMETHODCALLTYPE Compare(muiUiaRange* self, muiUiaRange* range, BOOL* out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    muiAccessText text;
    muiAccessText other;
    if (!TextOf(self, &text) || (IsSameText(self, range) && !TextOf(range, &other)))
    {
        return ELEMENT_GONE;
    }
    *out = IsSameText(self, range) && range->start == self->start && range->end == self->end;
    return S_OK;
}

static uint32_t EndpointOf(const muiUiaRange* range, int endpoint)
{
    return endpoint == ENDPOINT_START ? range->start : range->end;
}

// Moves an endpoint; crossing the other takes it along.
static void SetEndpoint(muiUiaRange* range, int endpoint, uint32_t at)
{
    if (endpoint == ENDPOINT_START)
    {
        range->start = at;
        range->end = range->end > at ? range->end : at;
    }
    else
    {
        range->end = at;
        range->start = range->start < at ? range->start : at;
    }
}

static HRESULT STDMETHODCALLTYPE CompareEndpoints(muiUiaRange* self, int endpoint,
                                                  muiUiaRange* target, int targetEndpoint, int* out)
{
    muiAccessText text;
    if (out == nullptr)
    {
        return E_POINTER;
    }
    if (!IsSameText(self, target))
    {
        return E_INVALIDARG;
    }
    if (!TextOf(self, &text) || !TextOf(target, &text))
    {
        return ELEMENT_GONE;
    }
    uint32_t a = EndpointOf(self, endpoint);
    uint32_t b = EndpointOf(target, targetEndpoint);
    *out = a < b ? -1 : (a > b ? 1 : 0);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Expand(muiUiaRange* self, int unit)
{
    muiAccessText text;
    if (!TextOf(self, &text))
    {
        return ELEMENT_GONE;
    }
    muiAccessUnit by = UnitOf(unit);
    self->start = UnitStart(&text, by, self->start);
    self->end = muiAccessBoundaryAfter(&text, by, self->start);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE FindAttribute(muiUiaRange* self, int attribute, VARIANT value,
                                               BOOL backward, muiUiaRange** out)
{
    (void)self;
    (void)attribute;
    (void)value;
    (void)backward;
    if (out == nullptr)
    {
        return E_POINTER;
    }
    *out = nullptr;
    return S_OK;
}

// A range's text as UTF-16, from the heap, and its length in units; NULL
// for none or no memory.
static WCHAR* WideOf(const muiAccessText* text, uint32_t start, uint32_t end, int* lengthOut)
{
    int wide = end > start ? MultiByteToWideChar(CP_UTF8, 0, text->bytes + start,
                                                 (int)(end - start), nullptr, 0)
                           : 0;
    WCHAR* string =
        wide > 0 ? HeapAlloc(GetProcessHeap(), 0, (size_t)wide * sizeof(WCHAR)) : nullptr;
    if (string != nullptr)
    {
        (void)MultiByteToWideChar(CP_UTF8, 0, text->bytes + start, (int)(end - start), string,
                                  wide);
    }
    *lengthOut = string != nullptr ? wide : 0;
    return string;
}

// The first place, or the last, that text is found in a range's text,
// as a range of its own; NULL when it is not there.
static HRESULT STDMETHODCALLTYPE FindText(muiUiaRange* self, BSTR text, BOOL backward,
                                          BOOL ignoreCase, muiUiaRange** out)
{
    muiAccessText value;
    if (out == nullptr || text == nullptr)
    {
        return out == nullptr ? E_POINTER : E_INVALIDARG;
    }
    *out = nullptr;
    if (!TextOf(self, &value))
    {
        return ELEMENT_GONE;
    }
    int length = 0;
    WCHAR* within = WideOf(&value, self->start, self->end, &length);
    int sought = (int)SysStringLen(text);
    int found = -1;
    for (int i = 0;
         within != nullptr && sought > 0 && i + sought <= length && (found < 0 || backward); i++)
    {
        if (CompareStringOrdinal(within + i, sought, text, sought, ignoreCase) == CSTR_EQUAL)
        {
            found = i;
        }
    }
    if (within != nullptr)
    {
        HeapFree(GetProcessHeap(), 0, within);
    }
    if (found < 0)
    {
        return within == nullptr && self->end > self->start ? E_OUTOFMEMORY : S_OK;
    }
    uint32_t before = muiAccessUtf16Before(value.bytes, self->start);
    uint32_t start = muiAccessByteOfUtf16(&value, before + (uint32_t)found);
    uint32_t end = muiAccessByteOfUtf16(&value, before + (uint32_t)(found + sought));
    *out = muiUiaMakeRange(self->node, start, end);
    return *out != nullptr ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE GetAttributeValue(muiUiaRange* self, int attribute, VARIANT* out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    VariantInit(out);
    const muiAccessNode* held = muiUiaNodeFor(self->node);
    if (held == nullptr)
    {
        return ELEMENT_GONE;
    }
    if (attribute == ATTRIBUTE_READ_ONLY)
    {
        out->vt = VT_BOOL;
        out->boolVal =
            (held->actions & (1u << mui_actionReplaceText)) == 0 ? VARIANT_TRUE : VARIANT_FALSE;
        return S_OK;
    }
    IUnknown* none = nullptr;
    muiUiaAdapter* adapter = self->node->adapter;
    if (adapter->uia.reservedNotSupported != nullptr &&
        SUCCEEDED(adapter->uia.reservedNotSupported(&none)) && none != nullptr)
    {
        out->vt = VT_UNKNOWN;
        out->punkVal = none;
    }
    return S_OK;
}

// An empty array of a type, as embedded objects are not given yet.
static HRESULT Empty(VARTYPE type, SAFEARRAY** out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    *out = SafeArrayCreateVector(type, 0, 0);
    return *out != nullptr ? S_OK : E_OUTOFMEMORY;
}

// Writes rectangles where the root is placed into an array's doubles,
// on the screen: left, top, width and height each.
static HRESULT PutRects(const muiUiaAdapter* adapter, const muiRect* rects, uint32_t count,
                        SAFEARRAY** out)
{
    *out = SafeArrayCreateVector(VT_R8, 0, count * 4);
    double* values = nullptr;
    HRESULT result = *out == nullptr ? E_OUTOFMEMORY
                     : count == 0    ? S_OK
                                     : SafeArrayAccessData(*out, (void**)&values);
    if (SUCCEEDED(result) && values != nullptr)
    {
        for (uint32_t i = 0; i < count; i++)
        {
            muiUiaRect rect = muiUiaScreenRectOf(adapter, rects[i]);
            values[4 * i] = rect.left;
            values[4 * i + 1] = rect.top;
            values[4 * i + 2] = rect.width;
            values[4 * i + 3] = rect.height;
        }
        result = SafeArrayUnaccessData(*out);
    }
    if (FAILED(result) && *out != nullptr)
    {
        (void)SafeArrayDestroy(*out);
        *out = nullptr;
    }
    return result;
}

// A rectangle each line the range lies on; the node's bounds for text
// without clusters; none for an empty range.
static HRESULT STDMETHODCALLTYPE GetBoundingRectangles(muiUiaRange* self, SAFEARRAY** out)
{
    muiAccessText text;
    if (out == nullptr)
    {
        return E_POINTER;
    }
    *out = nullptr;
    if (!TextOf(self, &text))
    {
        return ELEMENT_GONE;
    }
    muiUiaAdapter* adapter = self->node->adapter;
    uint64_t id = muiUiaNodeFor(self->node)->id;
    muiAccessRects got;
    muiResult status =
        muiAccessGetRects(adapter->tree, id, self->start, self->end, &adapter->allocator, &got);
    if (status == mui_errorCapacity)
    {
        return E_OUTOFMEMORY;
    }
    muiRect whole = {0};
    bool asWhole = status == mui_empty && self->start < self->end &&
                   muiAccessTree_GetBounds(adapter->tree, id, &whole) == mui_success;
    HRESULT result = status == mui_empty ? PutRects(adapter, &whole, asWhole ? 1 : 0, out)
                                         : PutRects(adapter, got.rects, got.count, out);
    muiAccessFreeRects(&got);
    return result;
}

static HRESULT STDMETHODCALLTYPE GetChildren(muiUiaRange* self, SAFEARRAY** out)
{
    return muiUiaNodeFor(self->node) != nullptr ? Empty(VT_UNKNOWN, out) : ELEMENT_GONE;
}

static HRESULT STDMETHODCALLTYPE GetEnclosingElement(muiUiaRange* self, muiUiaSimple** out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    *out = nullptr;
    if (muiUiaNodeFor(self->node) == nullptr)
    {
        return ELEMENT_GONE;
    }
    (void)muiUiaAddReference(self->node);
    *out = &self->node->simple;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE GetText(muiUiaRange* self, int maxLength, BSTR* out)
{
    muiAccessText text;
    if (out == nullptr)
    {
        return E_POINTER;
    }
    *out = nullptr;
    if (!TextOf(self, &text))
    {
        return ELEMENT_GONE;
    }
    int length = 0;
    WCHAR* wide = WideOf(&text, self->start, self->end, &length);
    if (wide == nullptr && self->end > self->start)
    {
        return E_OUTOFMEMORY;
    }
    // A limit of -1 is none.
    int kept = maxLength >= 0 && maxLength < length ? maxLength : length;
    *out = SysAllocStringLen(wide, (UINT)kept);
    if (wide != nullptr)
    {
        HeapFree(GetProcessHeap(), 0, wide);
    }
    return *out != nullptr ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE Move(muiUiaRange* self, int unit, int count, int* out)
{
    muiAccessText text;
    if (out == nullptr)
    {
        return E_POINTER;
    }
    *out = 0;
    if (!TextOf(self, &text))
    {
        return ELEMENT_GONE;
    }
    muiAccessUnit by = UnitOf(unit);
    if (self->start == self->end)
    {
        // An insertion point moves as the caret does, to the text's end too.
        uint32_t at = self->start;
        *out = Step(&text, by, &at, count, false);
        self->start = at;
        self->end = at;
        return S_OK;
    }
    uint32_t at = UnitStart(&text, by, self->start);
    *out = Step(&text, by, &at, count, true);
    self->start = at;
    self->end = muiAccessBoundaryAfter(&text, by, at);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE MoveEndpointByUnit(muiUiaRange* self, int endpoint, int unit,
                                                    int count, int* out)
{
    muiAccessText text;
    if (out == nullptr)
    {
        return E_POINTER;
    }
    *out = 0;
    if (!TextOf(self, &text))
    {
        return ELEMENT_GONE;
    }
    uint32_t at = EndpointOf(self, endpoint);
    *out = Step(&text, UnitOf(unit), &at, count, false);
    SetEndpoint(self, endpoint, at);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE MoveEndpointByRange(muiUiaRange* self, int endpoint,
                                                     muiUiaRange* target, int targetEndpoint)
{
    muiAccessText text;
    if (!IsSameText(self, target))
    {
        return E_INVALIDARG;
    }
    if (!TextOf(self, &text) || !TextOf(target, &text))
    {
        return ELEMENT_GONE;
    }
    SetEndpoint(self, endpoint, EndpointOf(target, targetEndpoint));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Select(muiUiaRange* self)
{
    muiAccessText text;
    if (!TextOf(self, &text))
    {
        return ELEMENT_GONE;
    }
    const muiAccessRequest request = {.action = mui_actionSetSelection,
                                      .target = muiUiaNodeFor(self->node)->id,
                                      .anchor = self->start,
                                      .focus = self->end};
    return muiUiaPerformRequest(self->node->adapter, &request);
}

// One selection at most: none is added or taken away.
static HRESULT STDMETHODCALLTYPE AddToSelection(muiUiaRange* self)
{
    return muiUiaNodeFor(self->node) != nullptr ? INVALID_OPERATION : ELEMENT_GONE;
}

static HRESULT STDMETHODCALLTYPE RemoveFromSelection(muiUiaRange* self)
{
    return muiUiaNodeFor(self->node) != nullptr ? INVALID_OPERATION : ELEMENT_GONE;
}

static HRESULT STDMETHODCALLTYPE ScrollIntoView(muiUiaRange* self, BOOL alignToTop)
{
    (void)alignToTop;
    const muiAccessNode* held = muiUiaNodeFor(self->node);
    return held != nullptr ? muiUiaPerform(self->node->adapter, mui_actionScrollIntoView, held->id)
                           : ELEMENT_GONE;
}

static const muiUiaRangeTable s_rangeTable = {Query,
                                              AddRef,
                                              Release,
                                              Clone,
                                              Compare,
                                              CompareEndpoints,
                                              Expand,
                                              FindAttribute,
                                              FindText,
                                              GetAttributeValue,
                                              GetBoundingRectangles,
                                              GetEnclosingElement,
                                              GetText,
                                              Move,
                                              MoveEndpointByUnit,
                                              MoveEndpointByRange,
                                              Select,
                                              AddToSelection,
                                              RemoveFromSelection,
                                              ScrollIntoView,
                                              GetChildren};
