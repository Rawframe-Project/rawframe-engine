// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The UI Automation adapter's events (record mui-0008): what applying an
// update changed, raised while the tree holds all of it and only when a
// client listens. Focus moving raises the focus event; a node updated
// raises a property change for each property whose value differs from
// the record it replaced; a live region's text changing raises the live
// region event.

#include "uia.h"

#include <string.h>
#include <wchar.h>

// The properties compared between a node's old record and its new one.
static const int s_properties[] = {
    PROPERTY_NAME,
    PROPERTY_HELP_TEXT,
    PROPERTY_IS_ENABLED,
    PROPERTY_TOGGLE_TOGGLE_STATE,
    PROPERTY_EXPAND_COLLAPSE_EXPAND_COLLAPSE_STATE,
    PROPERTY_VALUE_VALUE,
    PROPERTY_RANGE_VALUE_VALUE,
    PROPERTY_SELECTION_ITEM_IS_SELECTED,
};

static void SetBool(VARIANT* out, bool value)
{
    out->vt = VT_BOOL;
    out->boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
}

static void SetInt(VARIANT* out, int value)
{
    out->vt = VT_I4;
    out->lVal = value;
}

// The text a node names itself by: its label, or a label node's value.
static muiAccessTextKind NameKindOf(const muiAccessNode* node)
{
    return node->text[mui_accessLabel] == nullptr && node->role == mui_roleLabel ? mui_accessValue
                                                                                 : mui_accessLabel;
}

// A property's value from one record, empty where the record has none.
// A name is the record's own text; labels from other nodes or contents
// are not compared.
static HRESULT ValueOf(const muiAccessNode* node, int property, VARIANT* out)
{
    VariantInit(out);
    uint32_t flags = node->flags;
    muiAccessTextKind kind = NameKindOf(node);
    switch (property)
    {
    case PROPERTY_NAME:
        return muiUiaSetText(out, node->text[kind], node->textLength[kind]);
    case PROPERTY_HELP_TEXT:
        return muiUiaSetText(out, node->text[mui_accessDescription],
                             node->textLength[mui_accessDescription]);
    case PROPERTY_IS_ENABLED:
        SetBool(out, (flags & mui_accessDisabled) == 0);
        return S_OK;
    case PROPERTY_TOGGLE_TOGGLE_STATE:
        if ((flags & mui_accessCheckable) != 0)
        {
            SetInt(out, (flags & mui_accessMixed) != 0     ? TOGGLE_MIXED
                        : (flags & mui_accessChecked) != 0 ? TOGGLE_ON
                                                           : TOGGLE_OFF);
        }
        return S_OK;
    case PROPERTY_EXPAND_COLLAPSE_EXPAND_COLLAPSE_STATE:
        if ((flags & mui_accessExpandable) != 0)
        {
            SetInt(out, (flags & mui_accessExpanded) != 0 ? EXPAND_EXPANDED : EXPAND_COLLAPSED);
        }
        return S_OK;
    case PROPERTY_VALUE_VALUE:
        return (flags & mui_accessNumeric) == 0 ? muiUiaSetText(out, node->text[mui_accessValue],
                                                                node->textLength[mui_accessValue])
                                                : S_OK;
    case PROPERTY_RANGE_VALUE_VALUE:
        if ((flags & mui_accessNumeric) != 0)
        {
            out->vt = VT_R8;
            out->dblVal = (double)node->value;
        }
        return S_OK;
    default:
        if ((flags & mui_accessSelectable) != 0)
        {
            SetBool(out, (flags & mui_accessSelected) != 0);
        }
        return S_OK;
    }
}

static bool Same(const VARIANT* a, const VARIANT* b)
{
    if (a->vt != b->vt)
    {
        return false;
    }
    switch (a->vt)
    {
    case VT_BSTR:
        return SysStringLen(a->bstrVal) == SysStringLen(b->bstrVal) &&
               wmemcmp(a->bstrVal, b->bstrVal, SysStringLen(a->bstrVal)) == 0;
    case VT_BOOL:
        return a->boolVal == b->boolVal;
    case VT_I4:
        return a->lVal == b->lVal;
    case VT_R8:
        return a->dblVal == b->dblVal;
    default:
        return true;
    }
}

static void Raise(muiUiaAdapter* adapter, uint64_t id, int event)
{
    muiUiaNode* node = muiUiaNodeOf(adapter, id);
    if (node != nullptr)
    {
        (void)adapter->uia.raiseEvent(&node->simple, event);
    }
}

// Raises a property change; the new name is the whole name, as clients
// read it.
static void RaiseChange(muiUiaAdapter* adapter, const muiAccessNode* now, int property,
                        VARIANT* old, VARIANT* value)
{
    muiUiaNode* node = muiUiaNodeOf(adapter, now->id);
    if (property == PROPERTY_NAME)
    {
        (void)VariantClear(value);
        (void)muiUiaPropertyValue(adapter, now, PROPERTY_NAME, value);
    }
    if (node != nullptr)
    {
        (void)adapter->uia.raisePropertyChanged(&node->simple, property, *old, *value);
    }
}

void muiUiaUpdated(void* user, const muiAccessTree* tree, const muiAccessNode* old)
{
    muiUiaAdapter* adapter = user;
    const muiAccessNode* now = muiAccessTree_Find(tree, old->id);
    if (!adapter->listening || now == nullptr || !muiAccessTree_IsShown(tree, now->id))
    {
        return;
    }
    bool spoken = false;
    for (size_t i = 0; i < sizeof(s_properties) / sizeof(s_properties[0]); i++)
    {
        VARIANT before;
        VARIANT after;
        if (SUCCEEDED(ValueOf(old, s_properties[i], &before)) &&
            SUCCEEDED(ValueOf(now, s_properties[i], &after)) && !Same(&before, &after))
        {
            spoken |= s_properties[i] == PROPERTY_NAME || s_properties[i] == PROPERTY_VALUE_VALUE;
            RaiseChange(adapter, now, s_properties[i], &before, &after);
        }
        (void)VariantClear(&before);
        (void)VariantClear(&after);
    }
    if (now->values.live != mui_liveOff && (spoken || old->values.live == mui_liveOff))
    {
        Raise(adapter, now->id, EVENT_LIVE_REGION_CHANGED);
    }
}

void muiUiaAdded(void* user, const muiAccessTree* tree, uint64_t id)
{
    muiUiaAdapter* adapter = user;
    const muiAccessNode* node = muiAccessTree_Find(tree, id);
    if (adapter->listening && node != nullptr && node->values.live != mui_liveOff &&
        node->text[NameKindOf(node)] != nullptr && muiAccessTree_IsShown(tree, id))
    {
        Raise(adapter, id, EVENT_LIVE_REGION_CHANGED);
    }
}

void muiUiaFocusMoved(void* user, const muiAccessTree* tree, uint64_t old, uint64_t focus)
{
    (void)old;
    muiUiaAdapter* adapter = user;
    if (adapter->listening && focus != 0 && muiAccessTree_IsShown(tree, focus))
    {
        Raise(adapter, focus, EVENT_AUTOMATION_FOCUS_CHANGED);
    }
}
