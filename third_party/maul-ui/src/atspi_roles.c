// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The AT-SPI adapter's roles and states (record mui-0008): each Maul UI
// role's AT-SPI role, the names AT-SPI gives its roles, and a node's
// state set from its flags. The values are atspi-constants.h's.

#include "atspi.h"

// The AT-SPI roles the adapter gives, with their names.
enum
{
    ROLE_CANVAS = 6,
    ROLE_CHECK_BOX = 7,
    ROLE_CHECK_MENU_ITEM = 8,
    ROLE_COMBO_BOX = 11,
    ROLE_DATE_EDITOR = 12,
    ROLE_DIALOG = 16,
    ROLE_FRAME = 23,
    ROLE_IMAGE = 27,
    ROLE_LABEL = 29,
    ROLE_LIST = 31,
    ROLE_LIST_ITEM = 32,
    ROLE_MENU = 33,
    ROLE_MENU_BAR = 34,
    ROLE_MENU_ITEM = 35,
    ROLE_PAGE_TAB = 37,
    ROLE_PAGE_TAB_LIST = 38,
    ROLE_PANEL = 39,
    ROLE_PASSWORD_TEXT = 40,
    ROLE_PROGRESS_BAR = 42,
    ROLE_PUSH_BUTTON = 43,
    ROLE_RADIO_BUTTON = 44,
    ROLE_RADIO_MENU_ITEM = 45,
    ROLE_SCROLL_BAR = 48,
    ROLE_SCROLL_PANE = 49,
    ROLE_SEPARATOR = 50,
    ROLE_SLIDER = 51,
    ROLE_SPIN_BUTTON = 52,
    ROLE_STATUS_BAR = 54,
    ROLE_TABLE = 55,
    ROLE_TABLE_CELL = 56,
    ROLE_TABLE_COLUMN_HEADER = 57,
    ROLE_TABLE_ROW_HEADER = 58,
    ROLE_TERMINAL = 60,
    ROLE_TOGGLE_BUTTON = 62,
    ROLE_TOOL_BAR = 63,
    ROLE_TOOL_TIP = 64,
    ROLE_TREE = 65,
    ROLE_TREE_TABLE = 66,
    ROLE_UNKNOWN = 67,
    ROLE_PARAGRAPH = 73,
    ROLE_APPLICATION = 75,
    ROLE_EMBEDDED = 78,
    ROLE_ENTRY = 79,
    ROLE_CAPTION = 81,
    ROLE_DOCUMENT_FRAME = 82,
    ROLE_HEADING = 83,
    ROLE_FORM = 87,
    ROLE_LINK = 88,
    ROLE_TABLE_ROW = 90,
    ROLE_TREE_ITEM = 91,
    ROLE_COMMENT = 97,
    ROLE_LIST_BOX = 98,
    ROLE_GROUPING = 99,
    ROLE_NOTIFICATION = 101,
    ROLE_LEVEL_BAR = 103,
    ROLE_TITLE_BAR = 104,
    ROLE_AUDIO = 106,
    ROLE_VIDEO = 107,
    ROLE_ARTICLE = 109,
    ROLE_LANDMARK = 110,
    ROLE_LOG = 111,
    ROLE_MARQUEE = 112,
    ROLE_TIMER = 115,
    ROLE_SWITCH = 130,
};

// The AT-SPI states the adapter gives.
enum
{
    STATE_ACTIVE = 1,
    STATE_BUSY = 3,
    STATE_CHECKED = 4,
    STATE_COLLAPSED = 5,
    STATE_EDITABLE = 7,
    STATE_ENABLED = 8,
    STATE_EXPANDABLE = 9,
    STATE_EXPANDED = 10,
    STATE_FOCUSABLE = 11,
    STATE_FOCUSED = 12,
    STATE_HORIZONTAL = 14,
    STATE_MODAL = 16,
    STATE_MULTI_LINE = 17,
    STATE_MULTISELECTABLE = 18,
    STATE_SELECTABLE = 22,
    STATE_SELECTED = 23,
    STATE_SENSITIVE = 24,
    STATE_SHOWING = 25,
    STATE_SINGLE_LINE = 26,
    STATE_VERTICAL = 29,
    STATE_VISIBLE = 30,
    STATE_INDETERMINATE = 32,
    STATE_REQUIRED = 33,
    STATE_INVALID_ENTRY = 36,
    STATE_IS_DEFAULT = 39,
    STATE_CHECKABLE = 41,
    STATE_HAS_POPUP = 42,
    STATE_READ_ONLY = 43,
};
static_assert(STATE_READ_ONLY == MUI_ATSPI_LAST_STATE, "the last state the adapter gives");

// Each Maul UI role's.
static const uint8_t s_roles[MUI_ROLE_LAST + 1] = {
    [mui_roleGeneric] = ROLE_PANEL,
    [mui_roleLabel] = ROLE_LABEL,
    [mui_roleImage] = ROLE_IMAGE,
    [mui_roleLink] = ROLE_LINK,
    [mui_roleButton] = ROLE_PUSH_BUTTON,
    [mui_roleDefaultButton] = ROLE_PUSH_BUTTON,
    [mui_roleCheckBox] = ROLE_CHECK_BOX,
    [mui_roleRadioButton] = ROLE_RADIO_BUTTON,
    [mui_roleRadioGroup] = ROLE_GROUPING,
    // A toggle button: AT-SPI's switch role (2.56) is past the last role
    // older clients know, which they show as "last defined" (as GTK and
    // the browsers map it).
    [mui_roleSwitch] = ROLE_TOGGLE_BUTTON,
    [mui_roleTextInput] = ROLE_ENTRY,
    [mui_roleMultilineTextInput] = ROLE_ENTRY,
    [mui_roleSearchInput] = ROLE_ENTRY,
    [mui_rolePasswordInput] = ROLE_PASSWORD_TEXT,
    [mui_roleNumberInput] = ROLE_SPIN_BUTTON,
    [mui_roleEmailInput] = ROLE_ENTRY,
    [mui_rolePhoneNumberInput] = ROLE_ENTRY,
    [mui_roleUrlInput] = ROLE_ENTRY,
    [mui_roleDateInput] = ROLE_DATE_EDITOR,
    [mui_roleTimeInput] = ROLE_DATE_EDITOR,
    [mui_roleDateTimeInput] = ROLE_DATE_EDITOR,
    [mui_roleComboBox] = ROLE_COMBO_BOX,
    [mui_roleEditableComboBox] = ROLE_COMBO_BOX,
    [mui_roleListBox] = ROLE_LIST_BOX,
    [mui_roleListBoxOption] = ROLE_LIST_ITEM,
    [mui_roleList] = ROLE_LIST,
    [mui_roleListItem] = ROLE_LIST_ITEM,
    [mui_roleTree] = ROLE_TREE,
    [mui_roleTreeItem] = ROLE_TREE_ITEM,
    [mui_roleTreeGrid] = ROLE_TREE_TABLE,
    [mui_roleTable] = ROLE_TABLE,
    [mui_roleRow] = ROLE_TABLE_ROW,
    [mui_roleCell] = ROLE_TABLE_CELL,
    [mui_roleRowHeader] = ROLE_TABLE_ROW_HEADER,
    [mui_roleColumnHeader] = ROLE_TABLE_COLUMN_HEADER,
    [mui_roleRowGroup] = ROLE_PANEL,
    [mui_roleGrid] = ROLE_TABLE,
    [mui_roleGridCell] = ROLE_TABLE_CELL,
    [mui_roleMenu] = ROLE_MENU,
    [mui_roleMenuBar] = ROLE_MENU_BAR,
    [mui_roleMenuItem] = ROLE_MENU_ITEM,
    [mui_roleMenuItemCheckBox] = ROLE_CHECK_MENU_ITEM,
    [mui_roleMenuItemRadio] = ROLE_RADIO_MENU_ITEM,
    [mui_roleTab] = ROLE_PAGE_TAB,
    [mui_roleTabList] = ROLE_PAGE_TAB_LIST,
    [mui_roleTabPanel] = ROLE_PANEL,
    [mui_roleToolbar] = ROLE_TOOL_BAR,
    [mui_roleTooltip] = ROLE_TOOL_TIP,
    [mui_roleDialog] = ROLE_DIALOG,
    [mui_roleAlertDialog] = ROLE_DIALOG,
    [mui_roleAlert] = ROLE_NOTIFICATION,
    [mui_roleStatus] = ROLE_STATUS_BAR,
    [mui_roleLog] = ROLE_LOG,
    [mui_roleTimer] = ROLE_TIMER,
    [mui_roleProgressIndicator] = ROLE_PROGRESS_BAR,
    [mui_roleMeter] = ROLE_LEVEL_BAR,
    [mui_roleSlider] = ROLE_SLIDER,
    [mui_roleSpinButton] = ROLE_SPIN_BUTTON,
    [mui_roleScrollBar] = ROLE_SCROLL_BAR,
    [mui_roleScrollView] = ROLE_SCROLL_PANE,
    [mui_roleSplitter] = ROLE_SEPARATOR,
    [mui_roleGroup] = ROLE_GROUPING,
    [mui_rolePane] = ROLE_PANEL,
    [mui_roleWindow] = ROLE_FRAME,
    [mui_roleTitleBar] = ROLE_TITLE_BAR,
    [mui_roleHeading] = ROLE_HEADING,
    [mui_roleParagraph] = ROLE_PARAGRAPH,
    [mui_roleRegion] = ROLE_LANDMARK,
    [mui_roleNavigation] = ROLE_LANDMARK,
    [mui_roleMain] = ROLE_LANDMARK,
    [mui_roleBanner] = ROLE_LANDMARK,
    [mui_roleComplementary] = ROLE_LANDMARK,
    [mui_roleContentInfo] = ROLE_LANDMARK,
    [mui_roleSearch] = ROLE_LANDMARK,
    [mui_roleForm] = ROLE_FORM,
    [mui_roleArticle] = ROLE_ARTICLE,
    [mui_roleDocument] = ROLE_DOCUMENT_FRAME,
    [mui_roleApplication] = ROLE_EMBEDDED,
    [mui_roleFigure] = ROLE_PANEL,
    [mui_roleCaption] = ROLE_CAPTION,
    [mui_roleNote] = ROLE_COMMENT,
    [mui_roleDetails] = ROLE_PANEL,
    [mui_roleDisclosureTriangle] = ROLE_TOGGLE_BUTTON,
    [mui_roleCanvas] = ROLE_CANVAS,
    [mui_roleVideo] = ROLE_VIDEO,
    [mui_roleAudio] = ROLE_AUDIO,
    [mui_roleColorWell] = ROLE_PUSH_BUTTON,
    [mui_roleTerminal] = ROLE_TERMINAL,
    [mui_roleFeed] = ROLE_PANEL,
    [mui_roleMarquee] = ROLE_MARQUEE,
};

typedef struct RoleName
{
    uint32_t role;
    const char* name;
} RoleName;

static const RoleName s_names[] = {
    {ROLE_CANVAS, "canvas"},
    {ROLE_CHECK_BOX, "check box"},
    {ROLE_CHECK_MENU_ITEM, "check menu item"},
    {ROLE_COMBO_BOX, "combo box"},
    {ROLE_DATE_EDITOR, "date editor"},
    {ROLE_DIALOG, "dialog"},
    {ROLE_FRAME, "frame"},
    {ROLE_IMAGE, "image"},
    {ROLE_LABEL, "label"},
    {ROLE_LIST, "list"},
    {ROLE_LIST_ITEM, "list item"},
    {ROLE_MENU, "menu"},
    {ROLE_MENU_BAR, "menu bar"},
    {ROLE_MENU_ITEM, "menu item"},
    {ROLE_PAGE_TAB, "page tab"},
    {ROLE_PAGE_TAB_LIST, "page tab list"},
    {ROLE_PANEL, "panel"},
    {ROLE_PASSWORD_TEXT, "password text"},
    {ROLE_PROGRESS_BAR, "progress bar"},
    {ROLE_PUSH_BUTTON, "push button"},
    {ROLE_RADIO_BUTTON, "radio button"},
    {ROLE_RADIO_MENU_ITEM, "radio menu item"},
    {ROLE_SCROLL_BAR, "scroll bar"},
    {ROLE_SCROLL_PANE, "scroll pane"},
    {ROLE_SEPARATOR, "separator"},
    {ROLE_SLIDER, "slider"},
    {ROLE_SPIN_BUTTON, "spin button"},
    {ROLE_STATUS_BAR, "status bar"},
    {ROLE_TABLE, "table"},
    {ROLE_TABLE_CELL, "table cell"},
    {ROLE_TABLE_COLUMN_HEADER, "table column header"},
    {ROLE_TABLE_ROW_HEADER, "table row header"},
    {ROLE_TERMINAL, "terminal"},
    {ROLE_TOGGLE_BUTTON, "toggle button"},
    {ROLE_TOOL_BAR, "tool bar"},
    {ROLE_TOOL_TIP, "tool tip"},
    {ROLE_TREE, "tree"},
    {ROLE_TREE_TABLE, "tree table"},
    {ROLE_UNKNOWN, "unknown"},
    {ROLE_PARAGRAPH, "paragraph"},
    {ROLE_APPLICATION, "application"},
    {ROLE_EMBEDDED, "embedded"},
    {ROLE_ENTRY, "entry"},
    {ROLE_CAPTION, "caption"},
    {ROLE_DOCUMENT_FRAME, "document frame"},
    {ROLE_HEADING, "heading"},
    {ROLE_FORM, "form"},
    {ROLE_LINK, "link"},
    {ROLE_TABLE_ROW, "table row"},
    {ROLE_TREE_ITEM, "tree item"},
    {ROLE_COMMENT, "comment"},
    {ROLE_LIST_BOX, "list box"},
    {ROLE_GROUPING, "grouping"},
    {ROLE_NOTIFICATION, "notification"},
    {ROLE_LEVEL_BAR, "level bar"},
    {ROLE_TITLE_BAR, "title bar"},
    {ROLE_AUDIO, "audio"},
    {ROLE_VIDEO, "video"},
    {ROLE_ARTICLE, "article"},
    {ROLE_LANDMARK, "landmark"},
    {ROLE_LOG, "log"},
    {ROLE_MARQUEE, "marquee"},
    {ROLE_TIMER, "timer"},
};

uint32_t muiAtspiRoleOf(const muiAccessTree* tree, const muiAccessNode* node)
{
    uint32_t role = node->role <= MUI_ROLE_LAST ? s_roles[node->role] : ROLE_UNKNOWN;
    // A window's root is its top-level object, which clients look for
    // as a frame or a dialog to follow focus within: no other window
    // stands for it on the bus.
    bool top = role == ROLE_FRAME || role == ROLE_DIALOG;
    return node->id == muiAccessTree_GetRoot(tree) && !top ? ROLE_FRAME : role;
}

const char* muiAtspiRoleName(uint32_t role)
{
    for (size_t i = 0; i < sizeof(s_names) / sizeof(s_names[0]); i++)
    {
        if (s_names[i].role == role)
        {
            return s_names[i].name;
        }
    }
    return "unknown";
}

static void Set(uint32_t states[2], uint32_t state, bool set)
{
    if (set)
    {
        states[state / 32] |= 1u << (state % 32);
    }
}

// Whether a role edits text.
static bool IsTextInput(muiRole role)
{
    return (role >= mui_roleTextInput && role <= mui_roleDateTimeInput) ||
           role == mui_roleEditableComboBox;
}

// Whether a clipping ancestor shows none of a node.
static bool IsClipped(const muiAccessTree* tree, uint64_t id, uint32_t limit)
{
    muiRect box = {0};
    (void)muiAccessTree_GetBounds(tree, id, &box);
    uint32_t steps = 0;
    for (uint64_t at = muiAccessTree_GetParent(tree, id); at != 0 && steps < limit;
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

// AT-SPI's names of the states the adapter gives, for events.
static const char* const s_stateNames[STATE_READ_ONLY + 1] = {
    [STATE_ACTIVE] = "active",           [STATE_BUSY] = "busy",
    [STATE_CHECKED] = "checked",         [STATE_COLLAPSED] = "collapsed",
    [STATE_EDITABLE] = "editable",       [STATE_ENABLED] = "enabled",
    [STATE_EXPANDABLE] = "expandable",   [STATE_EXPANDED] = "expanded",
    [STATE_FOCUSABLE] = "focusable",     [STATE_FOCUSED] = "focused",
    [STATE_HORIZONTAL] = "horizontal",   [STATE_MODAL] = "modal",
    [STATE_MULTI_LINE] = "multi-line",   [STATE_MULTISELECTABLE] = "multiselectable",
    [STATE_SELECTABLE] = "selectable",   [STATE_SELECTED] = "selected",
    [STATE_SENSITIVE] = "sensitive",     [STATE_SHOWING] = "showing",
    [STATE_SINGLE_LINE] = "single-line", [STATE_VERTICAL] = "vertical",
    [STATE_VISIBLE] = "visible",         [STATE_INDETERMINATE] = "indeterminate",
    [STATE_REQUIRED] = "required",       [STATE_INVALID_ENTRY] = "invalid-entry",
    [STATE_IS_DEFAULT] = "is-default",   [STATE_CHECKABLE] = "checkable",
    [STATE_HAS_POPUP] = "has-popup",     [STATE_READ_ONLY] = "read-only",
};

const char* muiAtspiStateName(uint32_t state)
{
    return state <= STATE_READ_ONLY && s_stateNames[state] != nullptr ? s_stateNames[state] : "";
}

void muiAtspiRecordStatesOf(const muiAccessNode* node, uint32_t statesOut[2])
{
    uint32_t flags = node->flags;
    const muiAccessValues* values = &node->values;
    bool enabled = (flags & mui_accessDisabled) == 0;
    bool text = IsTextInput(node->role);
    statesOut[0] = 0;
    statesOut[1] = 0;
    Set(statesOut, STATE_ENABLED, enabled);
    Set(statesOut, STATE_SENSITIVE, enabled);
    Set(statesOut, STATE_FOCUSABLE, (flags & mui_accessFocusable) != 0);
    Set(statesOut, STATE_CHECKABLE, (flags & mui_accessCheckable) != 0);
    Set(statesOut, STATE_CHECKED, (flags & mui_accessChecked) != 0);
    Set(statesOut, STATE_INDETERMINATE, (flags & mui_accessMixed) != 0);
    Set(statesOut, STATE_EXPANDABLE, (flags & mui_accessExpandable) != 0);
    Set(statesOut, STATE_EXPANDED, (flags & mui_accessExpanded) != 0);
    Set(statesOut, STATE_COLLAPSED,
        (flags & (mui_accessExpandable | mui_accessExpanded)) == mui_accessExpandable);
    Set(statesOut, STATE_SELECTABLE, (flags & mui_accessSelectable) != 0);
    Set(statesOut, STATE_SELECTED, (flags & mui_accessSelected) != 0);
    Set(statesOut, STATE_MULTISELECTABLE, (flags & mui_accessMultiselectable) != 0);
    Set(statesOut, STATE_REQUIRED, (flags & mui_accessRequired) != 0);
    Set(statesOut, STATE_READ_ONLY, (flags & mui_accessReadOnly) != 0);
    Set(statesOut, STATE_BUSY, (flags & mui_accessBusy) != 0);
    Set(statesOut, STATE_MODAL, (flags & mui_accessModal) != 0);
    Set(statesOut, STATE_EDITABLE, text && (flags & mui_accessReadOnly) == 0);
    Set(statesOut, STATE_SINGLE_LINE, text && node->role != mui_roleMultilineTextInput);
    Set(statesOut, STATE_MULTI_LINE, node->role == mui_roleMultilineTextInput);
    Set(statesOut, STATE_HORIZONTAL, values->orientation == mui_orientationHorizontal);
    Set(statesOut, STATE_VERTICAL, values->orientation == mui_orientationVertical);
    Set(statesOut, STATE_IS_DEFAULT, node->role == mui_roleDefaultButton);
    Set(statesOut, STATE_HAS_POPUP, values->popup != mui_popupNone);
    Set(statesOut, STATE_INVALID_ENTRY, values->invalid != mui_invalidNone);
}

bool muiAtspiShowsFocus(const muiAccessTree* tree, const muiAccessNode* node)
{
    return node->id == muiAccessTree_GetFocus(tree) &&
           (node->id != muiAccessTree_GetRoot(tree) || (node->flags & mui_accessFocusable) != 0);
}

void muiAtspiStatesOf(const muiAtspiAdapter* adapter, const muiAccessNode* node,
                      uint32_t statesOut[2])
{
    const muiAccessTree* tree = adapter->tree;
    bool shown = muiAccessTree_IsShown(tree, node->id);
    muiAtspiRecordStatesOf(node, statesOut);
    Set(statesOut, STATE_VISIBLE, shown);
    Set(statesOut, STATE_SHOWING, shown && !IsClipped(tree, node->id, adapter->nodes));
    Set(statesOut, STATE_FOCUSED, muiAtspiShowsFocus(tree, node));
    // A window's root is active: the host does not yet say when its
    // window loses the system's focus.
    Set(statesOut, STATE_ACTIVE, node->id == muiAccessTree_GetRoot(tree));
}
