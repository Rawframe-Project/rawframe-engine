// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Android accessibility adapter's nodes (record mui-0008), as
// AccessKit maps them: a class name from the role, which TalkBack speaks
// the kind by; the states and the actions offered as the provider's
// bits; the box in the view's pixels; the shown parent and children by
// virtual id; a range; the live setting. A text view's text is its name
// and a text field's its value, the name then its hint; any other node's
// name is its content description, its value text its state.

#include "android.h"

#include <math.h>
#include <string.h>

// The provider's class names, by index (AccessProvider.CLASSES).
enum
{
    C_VIEW = 0,
    C_BUTTON,
    C_CHECK_BOX,
    C_RADIO_BUTTON,
    C_RADIO_GROUP,
    C_TOGGLE_BUTTON,
    C_EDIT_TEXT,
    C_SEEK_BAR,
    C_PROGRESS_BAR,
    C_IMAGE_VIEW,
    C_TAB_WIDGET,
    C_GRID_VIEW,
    C_LIST_VIEW,
    C_SPINNER,
    C_TEXT_VIEW,
    C_DIALOG,
    C_SCROLL_VIEW,
};

static const uint8_t s_classes[MUI_ROLE_LAST + 1] = {
    [mui_roleLabel] = C_TEXT_VIEW,
    [mui_roleImage] = C_IMAGE_VIEW,
    [mui_roleButton] = C_BUTTON,
    [mui_roleDefaultButton] = C_BUTTON,
    [mui_roleCheckBox] = C_CHECK_BOX,
    [mui_roleRadioButton] = C_RADIO_BUTTON,
    [mui_roleRadioGroup] = C_RADIO_GROUP,
    [mui_roleSwitch] = C_TOGGLE_BUTTON,
    [mui_roleTextInput] = C_EDIT_TEXT,
    [mui_roleMultilineTextInput] = C_EDIT_TEXT,
    [mui_roleSearchInput] = C_EDIT_TEXT,
    [mui_rolePasswordInput] = C_EDIT_TEXT,
    [mui_roleNumberInput] = C_EDIT_TEXT,
    [mui_roleEmailInput] = C_EDIT_TEXT,
    [mui_rolePhoneNumberInput] = C_EDIT_TEXT,
    [mui_roleUrlInput] = C_EDIT_TEXT,
    [mui_roleDateInput] = C_SPINNER,
    [mui_roleTimeInput] = C_SPINNER,
    [mui_roleDateTimeInput] = C_SPINNER,
    [mui_roleComboBox] = C_SPINNER,
    [mui_roleEditableComboBox] = C_SPINNER,
    [mui_roleListBox] = C_LIST_VIEW,
    [mui_roleList] = C_LIST_VIEW,
    [mui_roleTree] = C_LIST_VIEW,
    [mui_roleTreeGrid] = C_GRID_VIEW,
    [mui_roleTable] = C_GRID_VIEW,
    [mui_roleGrid] = C_GRID_VIEW,
    [mui_roleMenuItem] = C_BUTTON,
    [mui_roleMenuItemCheckBox] = C_CHECK_BOX,
    [mui_roleMenuItemRadio] = C_RADIO_BUTTON,
    [mui_roleTabList] = C_TAB_WIDGET,
    [mui_roleDialog] = C_DIALOG,
    [mui_roleAlertDialog] = C_DIALOG,
    [mui_roleProgressIndicator] = C_PROGRESS_BAR,
    [mui_roleMeter] = C_PROGRESS_BAR,
    [mui_roleSlider] = C_SEEK_BAR,
    [mui_roleScrollView] = C_SCROLL_VIEW,
    [mui_roleHeading] = C_TEXT_VIEW,
    [mui_roleParagraph] = C_TEXT_VIEW,
    [mui_roleCaption] = C_TEXT_VIEW,
    [mui_roleDisclosureTriangle] = C_BUTTON,
    [mui_roleCanvas] = C_IMAGE_VIEW,
    [mui_roleColorWell] = C_BUTTON,
};

// android.view.accessibility.AccessibilityNodeInfo.RangeInfo's float
// type.
#define RANGE_FLOAT 1

static bool Has(const muiAccessNode* node, muiAccessAction action)
{
    return (node->actions & (1u << action)) != 0;
}

// A button that toggles is a toggle button.
static int ClassOf(const muiAccessNode* node)
{
    if (node->role == mui_roleButton && (node->flags & mui_accessCheckable) != 0)
    {
        return C_TOGGLE_BUTTON;
    }
    return node->role <= MUI_ROLE_LAST ? s_classes[node->role] : C_VIEW;
}

static jint FlagsOf(const muiAndroidAdapter* adapter, const muiAccessNode* node)
{
    uint32_t flags = node->flags;
    bool field = ClassOf(node) == C_EDIT_TEXT;
    jint bits = 0;
    bits |= (flags & mui_accessCheckable) != 0 ? MUI_ANDROID_CHECKABLE : 0;
    bits |= (flags & mui_accessChecked) != 0 ? MUI_ANDROID_CHECKED : 0;
    bits |= (flags & mui_accessDisabled) == 0 ? MUI_ANDROID_ENABLED : 0;
    bits |= (flags & mui_accessFocusable) != 0 ? MUI_ANDROID_FOCUSABLE : 0;
    bits |= muiAccessTree_GetFocus(adapter->tree) == node->id ? MUI_ANDROID_FOCUSED : 0;
    bits |= (flags & mui_accessSelected) != 0 ? MUI_ANDROID_SELECTED : 0;
    bits |= (flags & mui_accessScrolls) != 0 ? MUI_ANDROID_SCROLLABLE : 0;
    bits |= node->role == mui_rolePasswordInput ? MUI_ANDROID_PASSWORD : 0;
    bits |= field && (flags & mui_accessReadOnly) == 0 ? MUI_ANDROID_EDITABLE : 0;
    bits |= node->role == mui_roleHeading ? MUI_ANDROID_HEADING : 0;
    bits |= node->role == mui_roleMultilineTextInput ? MUI_ANDROID_MULTILINE : 0;
    return bits;
}

// The first of three actions the node has, or none.
static bool FirstOf(const muiAccessNode* node, const muiAccessAction* actions,
                    muiAccessAction* actionOut)
{
    for (int i = 0; i < 3; i++)
    {
        if (Has(node, actions[i]))
        {
            *actionOut = actions[i];
            return true;
        }
    }
    return false;
}

static const muiAccessAction s_backward[3] = {mui_actionScrollUp, mui_actionScrollLeft,
                                              mui_actionDecrement};
static const muiAccessAction s_forward[3] = {mui_actionScrollDown, mui_actionScrollRight,
                                             mui_actionIncrement};

// The host's action for the provider's at an index; whether there is
// one. A click on a focusable node without one of its own is still a
// click, which activates it.
static bool HostActionOf(const muiAndroidAdapter* adapter, const muiAccessNode* node, int action,
                         muiAccessAction* actionOut)
{
    uint32_t flags = node->flags;
    bool focused = muiAccessTree_GetFocus(adapter->tree) == node->id;
    bool focusable = (flags & mui_accessFocusable) != 0;
    bool expanded = (flags & mui_accessExpanded) != 0;
    switch (action)
    {
    case MUI_ANDROID_CLICK:
        *actionOut = mui_actionClick;
        return Has(node, mui_actionClick) || (focusable && (flags & mui_accessNumeric) == 0);
    case MUI_ANDROID_FOCUS:
        *actionOut = mui_actionFocus;
        return focusable && !focused;
    case MUI_ANDROID_CLEAR_FOCUS:
        *actionOut = mui_actionBlur;
        return focusable && focused;
    case MUI_ANDROID_SCROLL_BACKWARD:
        return FirstOf(node, s_backward, actionOut);
    case MUI_ANDROID_SCROLL_FORWARD:
        return FirstOf(node, s_forward, actionOut);
    case MUI_ANDROID_SET_PROGRESS:
        *actionOut = mui_actionSetValue;
        return Has(node, mui_actionSetValue) && (flags & mui_accessNumeric) != 0;
    case MUI_ANDROID_EXPAND:
        *actionOut = mui_actionExpand;
        return Has(node, mui_actionExpand) && !expanded;
    case MUI_ANDROID_COLLAPSE:
        *actionOut = mui_actionCollapse;
        return Has(node, mui_actionCollapse) && expanded;
    default:
        return false;
    }
}

static jint ActionsOf(const muiAndroidAdapter* adapter, const muiAccessNode* node)
{
    jint bits = 0;
    for (int action = MUI_ANDROID_CLICK; action <= MUI_ANDROID_COLLAPSE; action++)
    {
        muiAccessAction host = mui_actionClick;
        bits |= HostActionOf(adapter, node, action, &host) ? 1 << action : 0;
    }
    return bits;
}

bool muiAndroidAct(const muiAndroidAdapter* adapter, const muiAccessNode* node, int action,
                   float value)
{
    muiAccessAction host = mui_actionClick;
    if (!HostActionOf(adapter, node, action, &host))
    {
        return false;
    }
    const muiAccessRequest request = {.action = host, .target = node->id, .value = value};
    return adapter->action(adapter->user, &request);
}

static jint BitsOf(float value)
{
    jint bits = 0;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static void PackBox(const muiAndroidAdapter* adapter, uint64_t id, jint* packed)
{
    muiRect box = {0};
    (void)muiAccessTree_GetBounds(adapter->tree, id, &box);
    float scale = adapter->scale;
    packed[MUI_ANDROID_LEFT] = (jint)floorf(box.x * scale);
    packed[MUI_ANDROID_TOP] = (jint)floorf(box.y * scale);
    packed[MUI_ANDROID_RIGHT] = (jint)ceilf((box.x + box.width) * scale);
    packed[MUI_ANDROID_BOTTOM] = (jint)ceilf((box.y + box.height) * scale);
}

uint32_t muiAndroidPack(muiAndroidAdapter* adapter, const muiAccessNode* node)
{
    jint* packed = adapter->packed;
    packed[MUI_ANDROID_CLASS] = ClassOf(node);
    packed[MUI_ANDROID_FLAGS] = FlagsOf(adapter, node);
    packed[MUI_ANDROID_ACTIONS] = ActionsOf(adapter, node);
    PackBox(adapter, node->id, packed);
    uint64_t parent = muiAccessTree_GetShownParent(adapter->tree, node->id);
    packed[MUI_ANDROID_PARENT] =
        parent != 0 ? muiAndroidVirtualOf(adapter, parent) : MUI_ANDROID_NO_ID;
    bool numeric = (node->flags & mui_accessNumeric) != 0;
    packed[MUI_ANDROID_RANGE] = numeric ? RANGE_FLOAT : -1;
    packed[MUI_ANDROID_MINIMUM] = BitsOf(node->minimum);
    packed[MUI_ANDROID_MAXIMUM] = BitsOf(node->maximum);
    packed[MUI_ANDROID_CURRENT] = BitsOf(node->value);
    packed[MUI_ANDROID_LIVE] = (jint)node->values.live;
    uint32_t count = 0;
    if (muiAccessTree_GetShownChildren(adapter->tree, node->id, adapter->scratch, adapter->nodes,
                                       &count) != mui_success)
    {
        count = 0;
    }
    uint32_t written = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        jint child = muiAndroidVirtualOf(adapter, adapter->scratch[i]);
        if (child != MUI_ANDROID_NO_ID)
        {
            packed[MUI_ANDROID_CHILDREN + written++] = child;
        }
    }
    packed[MUI_ANDROID_COUNT] = (jint)written;
    return MUI_ANDROID_CHILDREN + written;
}

muiAndroidText muiAndroidTextOf(const muiAccessNode* node, int kind)
{
    int view = ClassOf(node);
    bool text = view == C_TEXT_VIEW;
    bool field = view == C_EDIT_TEXT;
    const char* value = node->text[mui_accessValue];
    switch (kind)
    {
    case MUI_ANDROID_TEXT:
        return text ? (muiAndroidText){.name = true}
                    : (muiAndroidText){.text = field ? value : nullptr};
    case MUI_ANDROID_DESCRIPTION:
        return (muiAndroidText){.name = !text && !field};
    case MUI_ANDROID_HINT:
        return field ? (muiAndroidText){.name = true, .text = node->text[mui_accessPlaceholder]}
                     : (muiAndroidText){.text = node->text[mui_accessDescription]};
    case MUI_ANDROID_ROLE:
        return (muiAndroidText){.text = node->text[mui_accessRoleDescription]};
    case MUI_ANDROID_STATE:
        return (muiAndroidText){.text = !text && !field ? value : nullptr};
    default:
        return (muiAndroidText){0};
    }
}

static bool Holds(const muiAccessTree* tree, uint64_t id, float x, float y)
{
    muiRect box = {0};
    return muiAccessTree_GetBounds(tree, id, &box) == mui_success && x >= box.x &&
           x < box.x + box.width && y >= box.y && y < box.y + box.height;
}

uint64_t muiAndroidNodeAt(muiAndroidAdapter* adapter, float x, float y)
{
    float unitX = x / adapter->scale;
    float unitY = y / adapter->scale;
    uint64_t at = muiAccessTree_GetRoot(adapter->tree);
    if (at == 0 || !Holds(adapter->tree, at, unitX, unitY))
    {
        return 0;
    }
    // Down through the last drawn shown child holding the place.
    for (uint32_t depth = 0; depth < adapter->nodes; depth++)
    {
        uint32_t count = 0;
        if (muiAccessTree_GetShownChildren(adapter->tree, at, adapter->scratch, adapter->nodes,
                                           &count) != mui_success)
        {
            count = 0;
        }
        uint64_t next = 0;
        for (uint32_t i = count; i > 0 && next == 0; i--)
        {
            uint64_t child = adapter->scratch[i - 1];
            next = Holds(adapter->tree, child, unitX, unitY) ? child : 0;
        }
        if (next == 0)
        {
            break;
        }
        at = next;
    }
    return at;
}
