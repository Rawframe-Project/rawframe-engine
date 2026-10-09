// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The NSAccessibility adapter's roles (record mui-0008): each Maul UI
// role's AppKit role and subrole, as AccessKit maps them, by the strings
// the accessibility API defines (NSAccessibilityButtonRole is "AXButton");
// the roles AppKit names only by their strings are WebKit's.

#include "ns.h"

static const char* const s_roles[MUI_ROLE_LAST + 1] = {
    [mui_roleGeneric] = "AXGroup",
    [mui_roleLabel] = "AXStaticText",
    [mui_roleImage] = "AXImage",
    [mui_roleLink] = "AXLink",
    [mui_roleButton] = "AXButton",
    [mui_roleDefaultButton] = "AXButton",
    [mui_roleCheckBox] = "AXCheckBox",
    [mui_roleRadioButton] = "AXRadioButton",
    [mui_roleRadioGroup] = "AXRadioGroup",
    [mui_roleSwitch] = "AXCheckBox",
    [mui_roleTextInput] = "AXTextField",
    [mui_roleMultilineTextInput] = "AXTextArea",
    [mui_roleSearchInput] = "AXTextField",
    [mui_rolePasswordInput] = "AXTextField",
    [mui_roleNumberInput] = "AXTextField",
    [mui_roleEmailInput] = "AXTextField",
    [mui_rolePhoneNumberInput] = "AXTextField",
    [mui_roleUrlInput] = "AXTextField",
    [mui_roleDateInput] = "AXDateField",
    [mui_roleTimeInput] = "AXTimeField",
    [mui_roleDateTimeInput] = "AXDateField",
    [mui_roleComboBox] = "AXPopUpButton",
    [mui_roleEditableComboBox] = "AXComboBox",
    [mui_roleListBox] = "AXList",
    [mui_roleListBoxOption] = "AXStaticText",
    [mui_roleList] = "AXList",
    [mui_roleListItem] = "AXGroup",
    [mui_roleTree] = "AXOutline",
    [mui_roleTreeItem] = "AXRow",
    [mui_roleTreeGrid] = "AXTable",
    [mui_roleTable] = "AXTable",
    [mui_roleRow] = "AXRow",
    [mui_roleCell] = "AXCell",
    [mui_roleRowHeader] = "AXCell",
    [mui_roleColumnHeader] = "AXCell",
    [mui_roleRowGroup] = "AXGroup",
    [mui_roleGrid] = "AXTable",
    [mui_roleGridCell] = "AXCell",
    [mui_roleMenu] = "AXMenu",
    [mui_roleMenuBar] = "AXMenuBar",
    [mui_roleMenuItem] = "AXMenuItem",
    [mui_roleMenuItemCheckBox] = "AXMenuItem",
    [mui_roleMenuItemRadio] = "AXMenuItem",
    [mui_roleTab] = "AXRadioButton",
    [mui_roleTabList] = "AXTabGroup",
    [mui_roleTabPanel] = "AXGroup",
    [mui_roleToolbar] = "AXToolbar",
    [mui_roleTooltip] = "AXGroup",
    [mui_roleDialog] = "AXWindow",
    [mui_roleAlertDialog] = "AXWindow",
    [mui_roleAlert] = "AXGroup",
    [mui_roleStatus] = "AXGroup",
    [mui_roleLog] = "AXGroup",
    [mui_roleTimer] = "AXGroup",
    [mui_roleProgressIndicator] = "AXProgressIndicator",
    [mui_roleMeter] = "AXLevelIndicator",
    [mui_roleSlider] = "AXSlider",
    [mui_roleSpinButton] = "AXIncrementor",
    [mui_roleScrollBar] = "AXScrollBar",
    [mui_roleScrollView] = "AXGroup",
    [mui_roleSplitter] = "AXSplitter",
    [mui_roleGroup] = "AXGroup",
    [mui_rolePane] = "AXGroup",
    [mui_roleWindow] = "AXGroup",
    [mui_roleTitleBar] = "AXGroup",
    [mui_roleHeading] = "AXHeading",
    [mui_roleParagraph] = "AXGroup",
    [mui_roleRegion] = "AXGroup",
    [mui_roleNavigation] = "AXGroup",
    [mui_roleMain] = "AXGroup",
    [mui_roleBanner] = "AXGroup",
    [mui_roleComplementary] = "AXGroup",
    [mui_roleContentInfo] = "AXGroup",
    [mui_roleSearch] = "AXGroup",
    [mui_roleForm] = "AXGroup",
    [mui_roleArticle] = "AXGroup",
    [mui_roleDocument] = "AXGroup",
    [mui_roleApplication] = "AXGroup",
    [mui_roleFigure] = "AXGroup",
    [mui_roleCaption] = "AXGroup",
    [mui_roleNote] = "AXGroup",
    [mui_roleDetails] = "AXGroup",
    [mui_roleDisclosureTriangle] = "AXButton",
    [mui_roleCanvas] = "AXImage",
    [mui_roleVideo] = "AXGroup",
    [mui_roleAudio] = "AXGroup",
    [mui_roleColorWell] = "AXColorWell",
    [mui_roleTerminal] = "AXTextArea",
    [mui_roleFeed] = "AXGroup",
    [mui_roleMarquee] = "AXGroup",
};

static const char* const s_subroles[MUI_ROLE_LAST + 1] = {
    [mui_roleSwitch] = "AXSwitch",
    [mui_roleSearchInput] = "AXSearchField",
    [mui_rolePasswordInput] = "AXSecureTextField",
    [mui_roleTreeItem] = "AXOutlineRow",
    [mui_roleTab] = "AXTabButton",
    [mui_roleTabPanel] = "AXTabPanel",
    [mui_roleTooltip] = "AXUserInterfaceTooltip",
    [mui_roleDialog] = "AXDialog",
    [mui_roleAlertDialog] = "AXDialog",
    [mui_roleAlert] = "AXApplicationAlert",
    [mui_roleStatus] = "AXApplicationStatus",
    [mui_roleLog] = "AXApplicationLog",
    [mui_roleTimer] = "AXApplicationTimer",
    [mui_roleMeter] = "AXMeter",
    [mui_roleGroup] = "AXApplicationGroup",
    [mui_roleRegion] = "AXLandmarkRegion",
    [mui_roleNavigation] = "AXLandmarkNavigation",
    [mui_roleMain] = "AXLandmarkMain",
    [mui_roleBanner] = "AXLandmarkBanner",
    [mui_roleComplementary] = "AXLandmarkComplementary",
    [mui_roleContentInfo] = "AXLandmarkContentInfo",
    [mui_roleSearch] = "AXLandmarkSearch",
    [mui_roleForm] = "AXLandmarkForm",
    [mui_roleArticle] = "AXDocumentArticle",
    [mui_roleDocument] = "AXDocument",
    [mui_roleApplication] = "AXWebApplication",
    [mui_roleNote] = "AXDocumentNote",
    [mui_roleFeed] = "AXApplicationGroup",
    [mui_roleMarquee] = "AXApplicationMarquee",
};

// A button that toggles is a check box with the toggle subrole.
static bool IsToggle(const muiAccessNode* node)
{
    return node->role == mui_roleButton && (node->flags & mui_accessCheckable) != 0;
}

NSAccessibilityRole muiNsRoleOf(const muiAccessNode* node)
{
    const char* role = IsToggle(node)                ? "AXCheckBox"
                       : node->role <= MUI_ROLE_LAST ? s_roles[node->role]
                                                     : "AXUnknown";
    return [NSString stringWithUTF8String:role];
}

NSAccessibilitySubrole muiNsSubroleOf(const muiAccessNode* node)
{
    const char* subrole = IsToggle(node)                ? "AXToggle"
                          : node->role <= MUI_ROLE_LAST ? s_subroles[node->role]
                                                        : nullptr;
    return subrole != nullptr ? [NSString stringWithUTF8String:subrole] : nil;
}
