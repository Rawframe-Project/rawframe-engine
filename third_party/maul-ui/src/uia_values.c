// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The UI Automation adapter's patterns of values (record mui-0008):
// Value (a node's value text), RangeValue (a range's number) and Scroll
// (a container's offsets, as percents of how far it scrolls).

#include "uia.h"

#include <stddef.h>

// Value: the text is read here; setting it comes with text editing.

static muiUiaNode* FromValue(muiUiaValue* self)
{
    return (muiUiaNode*)((char*)self - offsetof(muiUiaNode, value));
}

static HRESULT STDMETHODCALLTYPE ValueQuery(muiUiaValue* self, REFIID id, void** out)
{
    return muiUiaQuery(FromValue(self), id, out);
}

static ULONG STDMETHODCALLTYPE ValueAddRef(muiUiaValue* self)
{
    return muiUiaAddReference(FromValue(self));
}

static ULONG STDMETHODCALLTYPE ValueRelease(muiUiaValue* self)
{
    muiUiaNode* node = FromValue(self);
    ULONG left = (ULONG)(node->references - 1);
    muiUiaRelease(node);
    return left;
}

static HRESULT STDMETHODCALLTYPE SetText(muiUiaValue* self, LPCWSTR text)
{
    (void)text;
    return muiUiaNodeFor(FromValue(self)) != nullptr ? NOT_SUPPORTED : ELEMENT_GONE;
}

static HRESULT STDMETHODCALLTYPE Text(muiUiaValue* self, BSTR* out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    *out = nullptr;
    const muiAccessNode* held = muiUiaNodeFor(FromValue(self));
    if (held == nullptr)
    {
        return ELEMENT_GONE;
    }
    const char* text = held->text[mui_accessValue];
    uint32_t length = held->textLength[mui_accessValue];
    int wide = text != nullptr && length != 0 && length <= INT32_MAX
                   ? MultiByteToWideChar(CP_UTF8, 0, text, (int)length, nullptr, 0)
                   : 0;
    *out = SysAllocStringLen(nullptr, (UINT)(wide > 0 ? wide : 0));
    if (*out == nullptr)
    {
        return E_OUTOFMEMORY;
    }
    if (wide > 0)
    {
        (void)MultiByteToWideChar(CP_UTF8, 0, text, (int)length, *out, wide);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE TextIsReadOnly(muiUiaValue* self, BOOL* out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    const muiAccessNode* held = muiUiaNodeFor(FromValue(self));
    if (held == nullptr)
    {
        return ELEMENT_GONE;
    }
    *out = (held->flags & mui_accessReadOnly) != 0;
    return S_OK;
}

static const muiUiaValueTable s_valueTable = {
    .QueryInterface = ValueQuery,
    .AddRef = ValueAddRef,
    .Release = ValueRelease,
    .SetValue = SetText,
    .get_Value = Text,
    .get_IsReadOnly = TextIsReadOnly,
};

// RangeValue.

static muiUiaNode* FromRange(muiUiaRangeValue* self)
{
    return (muiUiaNode*)((char*)self - offsetof(muiUiaNode, rangeValue));
}

static HRESULT STDMETHODCALLTYPE RangeQuery(muiUiaRangeValue* self, REFIID id, void** out)
{
    return muiUiaQuery(FromRange(self), id, out);
}

static ULONG STDMETHODCALLTYPE RangeAddRef(muiUiaRangeValue* self)
{
    return muiUiaAddReference(FromRange(self));
}

static ULONG STDMETHODCALLTYPE RangeRelease(muiUiaRangeValue* self)
{
    muiUiaNode* node = FromRange(self);
    ULONG left = (ULONG)(node->references - 1);
    muiUiaRelease(node);
    return left;
}

static HRESULT STDMETHODCALLTYPE SetNumber(muiUiaRangeValue* self, double value)
{
    muiUiaNode* node = FromRange(self);
    const muiAccessNode* held = muiUiaNodeFor(node);
    if (held == nullptr)
    {
        return ELEMENT_GONE;
    }
    if (!(value >= (double)held->minimum && value <= (double)held->maximum))
    {
        return E_INVALIDARG;
    }
    const muiAccessRequest request = {
        .action = mui_actionSetValue, .target = held->id, .value = (float)value};
    return muiUiaPerformRequest(node->adapter, &request);
}

// Which of a range's numbers a getter reads.
typedef enum Number
{
    Number_value,
    Number_minimum,
    Number_maximum,
    Number_small,
    Number_large,
} Number;

static HRESULT Read(muiUiaRangeValue* self, Number number, double* out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    const muiAccessNode* held = muiUiaNodeFor(FromRange(self));
    if (held == nullptr)
    {
        return ELEMENT_GONE;
    }
    double span = (double)held->maximum - (double)held->minimum;
    double step = (double)held->step;
    // A large change is a tenth of the range, and never below a step.
    double large = span / 10.0 > step ? span / 10.0 : step;
    const double numbers[] = {(double)held->value, (double)held->minimum, (double)held->maximum,
                              step, large};
    *out = numbers[number];
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Current(muiUiaRangeValue* self, double* out)
{
    return Read(self, Number_value, out);
}

static HRESULT STDMETHODCALLTYPE Minimum(muiUiaRangeValue* self, double* out)
{
    return Read(self, Number_minimum, out);
}

static HRESULT STDMETHODCALLTYPE Maximum(muiUiaRangeValue* self, double* out)
{
    return Read(self, Number_maximum, out);
}

static HRESULT STDMETHODCALLTYPE SmallChange(muiUiaRangeValue* self, double* out)
{
    return Read(self, Number_small, out);
}

static HRESULT STDMETHODCALLTYPE LargeChange(muiUiaRangeValue* self, double* out)
{
    return Read(self, Number_large, out);
}

static HRESULT STDMETHODCALLTYPE NumberIsReadOnly(muiUiaRangeValue* self, BOOL* out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    const muiAccessNode* held = muiUiaNodeFor(FromRange(self));
    if (held == nullptr)
    {
        return ELEMENT_GONE;
    }
    *out = (held->flags & mui_accessReadOnly) != 0 ||
           (held->actions & (1u << mui_actionSetValue)) == 0;
    return S_OK;
}

static const muiUiaRangeValueTable s_rangeValueTable = {
    .QueryInterface = RangeQuery,
    .AddRef = RangeAddRef,
    .Release = RangeRelease,
    .SetValue = SetNumber,
    .get_Value = Current,
    .get_IsReadOnly = NumberIsReadOnly,
    .get_Maximum = Maximum,
    .get_Minimum = Minimum,
    .get_LargeChange = LargeChange,
    .get_SmallChange = SmallChange,
};

// Scroll: a container's offsets as percents of how far it scrolls, and
// its port as a percent of its content.

static muiUiaNode* FromScroll(muiUiaScroll* self)
{
    return (muiUiaNode*)((char*)self - offsetof(muiUiaNode, scroll));
}

static HRESULT STDMETHODCALLTYPE ScrollQuery(muiUiaScroll* self, REFIID id, void** out)
{
    return muiUiaQuery(FromScroll(self), id, out);
}

static ULONG STDMETHODCALLTYPE ScrollAddRef(muiUiaScroll* self)
{
    return muiUiaAddReference(FromScroll(self));
}

static ULONG STDMETHODCALLTYPE ScrollRelease(muiUiaScroll* self)
{
    muiUiaNode* node = FromScroll(self);
    ULONG left = (ULONG)(node->references - 1);
    muiUiaRelease(node);
    return left;
}

// The page action an amount asks for on an axis, or none. A small step
// scrolls a page too: the tree knows no line height.
static bool PageAction(int amount, bool horizontal, muiAccessAction* actionOut)
{
    bool back = amount == AMOUNT_LARGE_BACK || amount == AMOUNT_SMALL_BACK;
    bool forward = amount == AMOUNT_LARGE_FORWARD || amount == AMOUNT_SMALL_FORWARD;
    *actionOut = horizontal ? (back ? mui_actionScrollLeft : mui_actionScrollRight)
                            : (back ? mui_actionScrollUp : mui_actionScrollDown);
    return back || forward;
}

static HRESULT STDMETHODCALLTYPE ScrollBy(muiUiaScroll* self, int horizontal, int vertical)
{
    muiUiaNode* node = FromScroll(self);
    const muiAccessNode* held = muiUiaNodeFor(node);
    if (held == nullptr)
    {
        return ELEMENT_GONE;
    }
    HRESULT result = S_OK;
    muiAccessAction action = mui_actionScrollRight;
    if (PageAction(horizontal, true, &action))
    {
        result = muiUiaPerform(node->adapter, action, held->id);
    }
    if (SUCCEEDED(result) && PageAction(vertical, false, &action))
    {
        result = muiUiaPerform(node->adapter, action, held->id);
    }
    return result;
}

// An offset from a percent, or the offset as it is for SCROLL_NONE.
static bool OffsetOf(double percent, float offset, float limit, double* out)
{
    if (percent == SCROLL_NONE)
    {
        *out = (double)offset;
        return true;
    }
    *out = percent / 100.0 * (double)limit;
    return percent >= 0.0 && percent <= 100.0;
}

static HRESULT STDMETHODCALLTYPE ScrollTo(muiUiaScroll* self, double horizontal, double vertical)
{
    muiUiaNode* node = FromScroll(self);
    const muiAccessNode* held = muiUiaNodeFor(node);
    if (held == nullptr)
    {
        return ELEMENT_GONE;
    }
    muiAccessRequest request = {.action = mui_actionSetScrollOffset, .target = held->id};
    double x = 0.0;
    double y = 0.0;
    if (!OffsetOf(horizontal, held->scrollX, held->scrollXMax, &x) ||
        !OffsetOf(vertical, held->scrollY, held->scrollYMax, &y))
    {
        return E_INVALIDARG;
    }
    request.x = (float)x;
    request.y = (float)y;
    return muiUiaPerformRequest(node->adapter, &request);
}

// Which of a container's measures a getter reads.
typedef enum Measure
{
    Measure_percentX,
    Measure_percentY,
    Measure_viewX,
    Measure_viewY,
} Measure;

static double MeasureOf(const muiAccessNode* node, Measure measure)
{
    bool x = measure == Measure_percentX || measure == Measure_viewX;
    double offset = (double)(x ? node->scrollX : node->scrollY);
    double limit = (double)(x ? node->scrollXMax : node->scrollYMax);
    double port = (double)(x ? node->bounds.width : node->bounds.height);
    if (measure == Measure_percentX || measure == Measure_percentY)
    {
        return limit > 0.0 ? offset / limit * 100.0 : SCROLL_NONE;
    }
    return port + limit > 0.0 ? port / (port + limit) * 100.0 : 100.0;
}

static HRESULT ReadMeasure(muiUiaScroll* self, Measure measure, double* out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    const muiAccessNode* held = muiUiaNodeFor(FromScroll(self));
    if (held == nullptr)
    {
        return ELEMENT_GONE;
    }
    *out = MeasureOf(held, measure);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE PercentX(muiUiaScroll* self, double* out)
{
    return ReadMeasure(self, Measure_percentX, out);
}

static HRESULT STDMETHODCALLTYPE PercentY(muiUiaScroll* self, double* out)
{
    return ReadMeasure(self, Measure_percentY, out);
}

static HRESULT STDMETHODCALLTYPE ViewX(muiUiaScroll* self, double* out)
{
    return ReadMeasure(self, Measure_viewX, out);
}

static HRESULT STDMETHODCALLTYPE ViewY(muiUiaScroll* self, double* out)
{
    return ReadMeasure(self, Measure_viewY, out);
}

static HRESULT Scrollable(muiUiaScroll* self, bool horizontal, BOOL* out)
{
    if (out == nullptr)
    {
        return E_POINTER;
    }
    const muiAccessNode* held = muiUiaNodeFor(FromScroll(self));
    if (held == nullptr)
    {
        return ELEMENT_GONE;
    }
    *out = (horizontal ? held->scrollXMax : held->scrollYMax) > 0.0f;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ScrollableX(muiUiaScroll* self, BOOL* out)
{
    return Scrollable(self, true, out);
}

static HRESULT STDMETHODCALLTYPE ScrollableY(muiUiaScroll* self, BOOL* out)
{
    return Scrollable(self, false, out);
}

static const muiUiaScrollTable s_scrollTable = {
    .QueryInterface = ScrollQuery,
    .AddRef = ScrollAddRef,
    .Release = ScrollRelease,
    .Scroll = ScrollBy,
    .SetScrollPercent = ScrollTo,
    .get_HorizontalScrollPercent = PercentX,
    .get_VerticalScrollPercent = PercentY,
    .get_HorizontalViewSize = ViewX,
    .get_VerticalViewSize = ViewY,
    .get_HorizontallyScrollable = ScrollableX,
    .get_VerticallyScrollable = ScrollableY,
};

void muiUiaInitValuePatterns(muiUiaNode* node)
{
    node->value.lpVtbl = &s_valueTable;
    node->rangeValue.lpVtbl = &s_rangeValueTable;
    node->scroll.lpVtbl = &s_scrollTable;
}
