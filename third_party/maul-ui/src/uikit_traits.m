// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The UIAccessibility adapter's traits (record mui-0008): a node's traits
// from its role and flags, as Flutter gives them; whether it is an
// element; its container's type; and VoiceOver's scroll directions as
// Maul UI's actions. UIKit's traits are variables, not constants, so the
// role table holds the adapter's own bits, translated at the call.

#include "uikit.h"

enum
{
    T_BUTTON = 1u << 0,
    T_LINK = 1u << 1,
    T_HEADER = 1u << 2,
    T_IMAGE = 1u << 3,
    T_TEXT = 1u << 4,
    T_SEARCH = 1u << 5,
    T_ADJUSTABLE = 1u << 6,
    T_TAB_BAR = 1u << 7,
    T_UPDATES = 1u << 8,
    // A toggle button where the SDK has the trait, else a button.
    T_TOGGLE = 1u << 9,
};

static const uint16_t s_traits[MUI_ROLE_LAST + 1] = {
    [mui_roleLabel] = T_TEXT,
    [mui_roleImage] = T_IMAGE,
    [mui_roleLink] = T_LINK,
    [mui_roleButton] = T_BUTTON,
    [mui_roleDefaultButton] = T_BUTTON,
    [mui_roleCheckBox] = T_BUTTON,
    [mui_roleRadioButton] = T_BUTTON,
    [mui_roleSwitch] = T_TOGGLE,
    [mui_roleSearchInput] = T_SEARCH,
    [mui_roleComboBox] = T_BUTTON,
    [mui_roleMenuItem] = T_BUTTON,
    [mui_roleMenuItemCheckBox] = T_BUTTON,
    [mui_roleMenuItemRadio] = T_BUTTON,
    [mui_roleTab] = T_BUTTON,
    [mui_roleTabList] = T_TAB_BAR,
    [mui_roleTimer] = T_UPDATES,
    [mui_roleSlider] = T_ADJUSTABLE,
    [mui_roleSpinButton] = T_ADJUSTABLE,
    [mui_roleHeading] = T_HEADER,
    [mui_roleDisclosureTriangle] = T_BUTTON,
    [mui_roleCanvas] = T_IMAGE,
    [mui_roleColorWell] = T_BUTTON,
};

static const UIAccessibilityContainerType s_containers[MUI_ROLE_LAST + 1] = {
    [mui_roleRadioGroup] = UIAccessibilityContainerTypeSemanticGroup,
    [mui_roleListBox] = UIAccessibilityContainerTypeList,
    [mui_roleList] = UIAccessibilityContainerTypeList,
    [mui_roleTree] = UIAccessibilityContainerTypeList,
    [mui_roleMenu] = UIAccessibilityContainerTypeList,
    [mui_roleMenuBar] = UIAccessibilityContainerTypeSemanticGroup,
    [mui_roleTabList] = UIAccessibilityContainerTypeSemanticGroup,
    [mui_roleToolbar] = UIAccessibilityContainerTypeSemanticGroup,
    [mui_roleDialog] = UIAccessibilityContainerTypeSemanticGroup,
    [mui_roleAlertDialog] = UIAccessibilityContainerTypeSemanticGroup,
    [mui_roleGroup] = UIAccessibilityContainerTypeSemanticGroup,
    [mui_roleRegion] = UIAccessibilityContainerTypeLandmark,
    [mui_roleNavigation] = UIAccessibilityContainerTypeLandmark,
    [mui_roleMain] = UIAccessibilityContainerTypeLandmark,
    [mui_roleBanner] = UIAccessibilityContainerTypeLandmark,
    [mui_roleComplementary] = UIAccessibilityContainerTypeLandmark,
    [mui_roleContentInfo] = UIAccessibilityContainerTypeLandmark,
    [mui_roleSearch] = UIAccessibilityContainerTypeLandmark,
    [mui_roleForm] = UIAccessibilityContainerTypeLandmark,
};

static bool Has(const muiAccessNode* node, muiAccessAction action)
{
    return (node->actions & (1u << action)) != 0;
}

// A button that toggles is a toggle button.
static uint32_t BitsOf(const muiAccessNode* node)
{
    uint32_t bits = node->role <= MUI_ROLE_LAST ? s_traits[node->role] : 0;
    if (node->role == mui_roleButton && (node->flags & mui_accessCheckable) != 0)
    {
        bits = T_TOGGLE;
    }
    if (Has(node, mui_actionIncrement) || Has(node, mui_actionDecrement))
    {
        bits |= T_ADJUSTABLE;
    }
    return bits;
}

static UIAccessibilityTraits Toggle(void)
{
    if (@available(iOS 17.0, *))
    {
        return UIAccessibilityTraitToggleButton;
    }
    return UIAccessibilityTraitButton;
}

UIAccessibilityTraits muiUikitTraitsOf(const muiAccessNode* node)
{
    uint32_t bits = BitsOf(node);
    UIAccessibilityTraits traits = UIAccessibilityTraitNone;
    traits |= (bits & T_BUTTON) != 0 ? UIAccessibilityTraitButton : 0;
    traits |= (bits & T_LINK) != 0 ? UIAccessibilityTraitLink : 0;
    traits |= (bits & T_HEADER) != 0 ? UIAccessibilityTraitHeader : 0;
    traits |= (bits & T_IMAGE) != 0 ? UIAccessibilityTraitImage : 0;
    traits |= (bits & T_TEXT) != 0 ? UIAccessibilityTraitStaticText : 0;
    traits |= (bits & T_SEARCH) != 0 ? UIAccessibilityTraitSearchField : 0;
    traits |= (bits & T_ADJUSTABLE) != 0 ? UIAccessibilityTraitAdjustable : 0;
    traits |= (bits & T_TAB_BAR) != 0 ? UIAccessibilityTraitTabBar : 0;
    traits |= (bits & T_UPDATES) != 0 ? UIAccessibilityTraitUpdatesFrequently : 0;
    traits |= (bits & T_TOGGLE) != 0 ? Toggle() : 0;
    uint32_t flags = node->flags;
    // A toggle button's state is its value; a check box's, selected.
    bool checked = (flags & mui_accessChecked) != 0 && (bits & T_TOGGLE) == 0;
    traits |= (flags & mui_accessSelected) != 0 || checked ? UIAccessibilityTraitSelected : 0;
    traits |= (flags & mui_accessDisabled) != 0 ? UIAccessibilityTraitNotEnabled : 0;
    return traits;
}

// Scrolling alone does not make a node something to stop on: a pane
// with nothing to say is passed, VoiceOver scrolling it through its
// container.
bool muiUikitSays(const muiUikitAdapter* adapter, const muiAccessNode* node)
{
    const uint32_t says = mui_accessFocusable | mui_accessNumeric | mui_accessCheckable;
    const uint32_t acts = 1u << mui_actionClick | 1u << mui_actionExpand |
                          1u << mui_actionCollapse | 1u << mui_actionIncrement |
                          1u << mui_actionDecrement | 1u << mui_actionSetValue;
    if ((node->flags & says) != 0 || (node->actions & acts) != 0 || BitsOf(node) != 0 ||
        node->text[mui_accessValue] != nullptr || node->text[mui_accessDescription] != nullptr)
    {
        return true;
    }
    NSString* name = muiUikitNameOf(adapter, node->id);
    return name != nil && [name length] != 0;
}

UIAccessibilityContainerType muiUikitContainerTypeOf(const muiAccessNode* node)
{
    return node->role <= MUI_ROLE_LAST ? s_containers[node->role]
                                       : UIAccessibilityContainerTypeNone;
}

// Vertical directions name what comes into view, horizontal ones the
// finger's way, as Flutter reads them; next and previous go across
// where the node scrolls so, else down and up.
static muiAccessAction ScrollOf(const muiAccessNode* node, UIAccessibilityScrollDirection direction)
{
    bool across = Has(node, mui_actionScrollRight) || Has(node, mui_actionScrollLeft);
    switch (direction)
    {
    case UIAccessibilityScrollDirectionUp:
        return mui_actionScrollUp;
    case UIAccessibilityScrollDirectionDown:
        return mui_actionScrollDown;
    case UIAccessibilityScrollDirectionLeft:
        return mui_actionScrollRight;
    case UIAccessibilityScrollDirectionRight:
        return mui_actionScrollLeft;
    case UIAccessibilityScrollDirectionNext:
        return across ? mui_actionScrollRight : mui_actionScrollDown;
    case UIAccessibilityScrollDirectionPrevious:
        return across ? mui_actionScrollLeft : mui_actionScrollUp;
    }
    return mui_actionScrollDown;
}

bool muiUikitScroll(const muiUikitAdapter* adapter, const muiAccessNode* node,
                    UIAccessibilityScrollDirection direction)
{
    muiAccessAction action = ScrollOf(node, direction);
    return Has(node, action) && muiUikitAct(adapter, action, node->id, 0.0f);
}
