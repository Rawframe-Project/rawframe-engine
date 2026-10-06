// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The UI Automation adapter's property values (record mui-0008): control
// types from roles, texts as BSTRs, and states, positions and landmarks
// from a node's flags and values.

#include "allocator.h"
#include "uia.h"

#include <string.h>

// Landmark types and heading levels, from Windows 10's UI Automation,
// which MinGW's headers lack.
#define LANDMARK_CUSTOM     80000
#define LANDMARK_FORM       80001
#define LANDMARK_MAIN       80002
#define LANDMARK_NAVIGATION 80003
#define LANDMARK_SEARCH     80004
#define HEADING_LEVEL_1     80051

// Each role's control type; a role left out is a group.
static const uint16_t s_controlTypes[MUI_ROLE_LAST + 1] = {
    [mui_roleLabel] = CONTROL_TEXT,
    [mui_roleImage] = CONTROL_IMAGE,
    [mui_roleLink] = CONTROL_HYPERLINK,
    [mui_roleButton] = CONTROL_BUTTON,
    [mui_roleDefaultButton] = CONTROL_BUTTON,
    [mui_roleCheckBox] = CONTROL_CHECK_BOX,
    [mui_roleRadioButton] = CONTROL_RADIO_BUTTON,
    [mui_roleSwitch] = CONTROL_BUTTON,
    [mui_roleTextInput] = CONTROL_EDIT,
    [mui_roleMultilineTextInput] = CONTROL_EDIT,
    [mui_roleSearchInput] = CONTROL_EDIT,
    [mui_rolePasswordInput] = CONTROL_EDIT,
    [mui_roleNumberInput] = CONTROL_EDIT,
    [mui_roleEmailInput] = CONTROL_EDIT,
    [mui_rolePhoneNumberInput] = CONTROL_EDIT,
    [mui_roleUrlInput] = CONTROL_EDIT,
    [mui_roleDateInput] = CONTROL_EDIT,
    [mui_roleTimeInput] = CONTROL_EDIT,
    [mui_roleDateTimeInput] = CONTROL_EDIT,
    [mui_roleComboBox] = CONTROL_COMBO_BOX,
    [mui_roleEditableComboBox] = CONTROL_COMBO_BOX,
    [mui_roleListBox] = CONTROL_LIST,
    [mui_roleListBoxOption] = CONTROL_LIST_ITEM,
    [mui_roleList] = CONTROL_LIST,
    [mui_roleListItem] = CONTROL_LIST_ITEM,
    [mui_roleTree] = CONTROL_TREE,
    [mui_roleTreeItem] = CONTROL_TREE_ITEM,
    [mui_roleTreeGrid] = CONTROL_DATA_GRID,
    [mui_roleTable] = CONTROL_TABLE,
    [mui_roleRow] = CONTROL_DATA_ITEM,
    [mui_roleCell] = CONTROL_DATA_ITEM,
    [mui_roleRowHeader] = CONTROL_HEADER_ITEM,
    [mui_roleColumnHeader] = CONTROL_HEADER_ITEM,
    [mui_roleGrid] = CONTROL_DATA_GRID,
    [mui_roleGridCell] = CONTROL_DATA_ITEM,
    [mui_roleMenu] = CONTROL_MENU,
    [mui_roleMenuBar] = CONTROL_MENU_BAR,
    [mui_roleMenuItem] = CONTROL_MENU_ITEM,
    [mui_roleMenuItemCheckBox] = CONTROL_MENU_ITEM,
    [mui_roleMenuItemRadio] = CONTROL_MENU_ITEM,
    [mui_roleTab] = CONTROL_TAB_ITEM,
    [mui_roleTabList] = CONTROL_TAB,
    [mui_roleTabPanel] = CONTROL_PANE,
    [mui_roleToolbar] = CONTROL_TOOL_BAR,
    [mui_roleTooltip] = CONTROL_TOOL_TIP,
    [mui_roleDialog] = CONTROL_WINDOW,
    [mui_roleAlertDialog] = CONTROL_WINDOW,
    [mui_roleAlert] = CONTROL_TEXT,
    [mui_roleStatus] = CONTROL_STATUS_BAR,
    [mui_roleProgressIndicator] = CONTROL_PROGRESS_BAR,
    [mui_roleMeter] = CONTROL_PROGRESS_BAR,
    [mui_roleSlider] = CONTROL_SLIDER,
    [mui_roleSpinButton] = CONTROL_SPINNER,
    [mui_roleScrollBar] = CONTROL_SCROLL_BAR,
    [mui_roleScrollView] = CONTROL_PANE,
    [mui_roleSplitter] = CONTROL_SEPARATOR,
    [mui_rolePane] = CONTROL_PANE,
    [mui_roleWindow] = CONTROL_WINDOW,
    [mui_roleTitleBar] = CONTROL_TITLE_BAR,
    [mui_roleHeading] = CONTROL_TEXT,
    [mui_roleParagraph] = CONTROL_TEXT,
    [mui_roleDocument] = CONTROL_DOCUMENT,
    [mui_roleApplication] = CONTROL_PANE,
    [mui_roleCaption] = CONTROL_TEXT,
    [mui_roleDisclosureTriangle] = CONTROL_BUTTON,
    [mui_roleCanvas] = CONTROL_IMAGE,
    [mui_roleColorWell] = CONTROL_BUTTON,
    [mui_roleTerminal] = CONTROL_DOCUMENT,
    [mui_roleFeed] = CONTROL_LIST,
    [mui_roleMarquee] = CONTROL_TEXT,
};

static int ControlTypeOf(muiRole role)
{
    int type = role <= MUI_ROLE_LAST ? s_controlTypes[role] : 0;
    return type != 0 ? type : CONTROL_GROUP;
}

// A landmark's type, 0 for none, and the name of a custom one.
static int LandmarkOf(muiRole role, const char** nameOut)
{
    *nameOut = nullptr;
    switch (role)
    {
    case mui_roleNavigation:
        return LANDMARK_NAVIGATION;
    case mui_roleMain:
        return LANDMARK_MAIN;
    case mui_roleSearch:
        return LANDMARK_SEARCH;
    case mui_roleForm:
        return LANDMARK_FORM;
    case mui_roleBanner:
        *nameOut = "banner";
        return LANDMARK_CUSTOM;
    case mui_roleComplementary:
        *nameOut = "complementary";
        return LANDMARK_CUSTOM;
    case mui_roleContentInfo:
        *nameOut = "content information";
        return LANDMARK_CUSTOM;
    case mui_roleRegion:
        *nameOut = "region";
        return LANDMARK_CUSTOM;
    default:
        return 0;
    }
}

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

HRESULT muiUiaSetText(VARIANT* out, const char* text, size_t length)
{
    if (text == nullptr || length == 0 || length > INT32_MAX)
    {
        return S_OK;
    }
    int wide = MultiByteToWideChar(CP_UTF8, 0, text, (int)length, nullptr, 0);
    BSTR string = wide > 0 ? SysAllocStringLen(nullptr, (UINT)wide) : nullptr;
    if (string == nullptr)
    {
        return wide > 0 ? E_OUTOFMEMORY : S_OK;
    }
    (void)MultiByteToWideChar(CP_UTF8, 0, text, (int)length, string, wide);
    out->vt = VT_BSTR;
    out->bstrVal = string;
    return S_OK;
}

static HRESULT SetNodeText(VARIANT* out, const muiAccessNode* node, muiAccessTextKind kind)
{
    return muiUiaSetText(out, node->text[kind], node->textLength[kind]);
}

static HRESULT SetName(VARIANT* out, const muiUiaAdapter* adapter, uint64_t id)
{
    size_t length = 0;
    if (muiAccessTree_GetName(adapter->tree, id, nullptr, 0, &length) != mui_errorCapacity)
    {
        return S_OK;
    }
    char* name = muiAllocate(&adapter->allocator, length + 1, 1);
    if (name == nullptr)
    {
        return E_OUTOFMEMORY;
    }
    HRESULT result =
        muiAccessTree_GetName(adapter->tree, id, name, length + 1, &length) == mui_success
            ? muiUiaSetText(out, name, length)
            : S_OK;
    muiRelease(&adapter->allocator, name, length + 1, 1);
    return result;
}

// Whether a clipping ancestor shows none of a node.
static bool IsOffscreen(const muiUiaAdapter* adapter, uint64_t id)
{
    const muiAccessTree* tree = adapter->tree;
    muiRect box = {0};
    (void)muiAccessTree_GetBounds(tree, id, &box);
    uint32_t steps = 0;
    for (uint64_t at = muiAccessTree_GetParent(tree, id); at != 0 && steps < adapter->nodes;
         at = muiAccessTree_GetParent(tree, at), steps++)
    {
        muiRect clip = {0};
        if ((muiAccessTree_Find(tree, at)->flags & mui_accessClipsChildren) != 0 &&
            muiAccessTree_GetBounds(tree, at, &clip) == mui_success &&
            (box.x + box.width <= clip.x || box.x >= clip.x + clip.width ||
             box.y + box.height <= clip.y || box.y >= clip.y + clip.height))
        {
            return true;
        }
    }
    return false;
}

// The properties taken from a node's texts.
static bool TextProperty(VARIANT* out, const muiAccessNode* node, int property, HRESULT* result)
{
    const char* landmark = nullptr;
    switch (property)
    {
    case PROPERTY_HELP_TEXT:
    case PROPERTY_FULL_DESCRIPTION:
        *result = SetNodeText(out, node, mui_accessDescription);
        return true;
    case PROPERTY_LOCALIZED_CONTROL_TYPE:
        *result = SetNodeText(out, node, mui_accessRoleDescription);
        return true;
    case PROPERTY_ITEM_STATUS:
        *result = SetNodeText(out, node, mui_accessStateDescription);
        return true;
    case PROPERTY_ACCELERATOR_KEY:
        *result = SetNodeText(out, node, mui_accessKeyboardShortcut);
        return true;
    case PROPERTY_LOCALIZED_LANDMARK_TYPE:
        (void)LandmarkOf(node->role, &landmark);
        *result = node->text[mui_accessRoleDescription] != nullptr
                      ? SetNodeText(out, node, mui_accessRoleDescription)
                      : muiUiaSetText(out, landmark, landmark != nullptr ? strlen(landmark) : 0);
        return true;
    default:
        return false;
    }
}

// The properties taken from a node's flags.
static bool StateProperty(VARIANT* out, const muiUiaAdapter* adapter, const muiAccessNode* node,
                          int property)
{
    switch (property)
    {
    case PROPERTY_IS_KEYBOARD_FOCUSABLE:
        SetBool(out, (node->flags & mui_accessFocusable) != 0);
        return true;
    case PROPERTY_HAS_KEYBOARD_FOCUS:
        SetBool(out, node->id == muiAccessTree_GetFocus(adapter->tree));
        return true;
    case PROPERTY_IS_ENABLED:
        SetBool(out, (node->flags & mui_accessDisabled) == 0);
        return true;
    case PROPERTY_IS_OFFSCREEN:
        SetBool(out, IsOffscreen(adapter, node->id));
        return true;
    case PROPERTY_IS_PASSWORD:
        SetBool(out, node->role == mui_rolePasswordInput);
        return true;
    case PROPERTY_IS_REQUIRED_FOR_FORM:
        SetBool(out, (node->flags & mui_accessRequired) != 0);
        return true;
    case PROPERTY_IS_DIALOG:
        SetBool(out, node->role == mui_roleDialog || node->role == mui_roleAlertDialog);
        return true;
    default:
        return false;
    }
}

// The properties taken from a node's typed values; none for a value the
// node lacks.
static bool ValueProperty(VARIANT* out, const muiAccessNode* node, int property)
{
    const muiAccessValues* values = &node->values;
    const char* custom = nullptr;
    int number = 0;
    switch (property)
    {
    case PROPERTY_LEVEL:
        number = (int)values->level;
        break;
    case PROPERTY_POSITION_IN_SET:
        number = (int)values->setPosition;
        break;
    case PROPERTY_SIZE_OF_SET:
        number = (int)values->setSize;
        break;
    case PROPERTY_HEADING_LEVEL:
        number = node->role == mui_roleHeading && values->level >= 1 && values->level <= 9
                     ? HEADING_LEVEL_1 + (int)values->level - 1
                     : 0;
        break;
    case PROPERTY_LANDMARK_TYPE:
        number = LandmarkOf(node->role, &custom);
        break;
    case PROPERTY_ORIENTATION:
        // Maul UI's orientations are UI Automation's.
        number = (int)values->orientation;
        break;
    case PROPERTY_LIVE_SETTING:
        number = (int)values->live;
        break;
    default:
        return false;
    }
    if (number != 0)
    {
        SetInt(out, number);
    }
    return true;
}

HRESULT muiUiaPropertyValue(muiUiaAdapter* adapter, const muiAccessNode* node, int property,
                            VARIANT* out)
{
    HRESULT result = S_OK;
    if (property == PROPERTY_CONTROL_TYPE)
    {
        SetInt(out, ControlTypeOf(node->role));
    }
    else if (property == PROPERTY_NAME)
    {
        result = SetName(out, adapter, node->id);
    }
    else if (!TextProperty(out, node, property, &result))
    {
        (void)(StateProperty(out, adapter, node, property) || ValueProperty(out, node, property));
    }
    return result;
}
