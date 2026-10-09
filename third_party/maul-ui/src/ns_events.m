// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The NSAccessibility adapter's notifications (record mui-0008), as
// AccessKit posts them: a title or a value changed on the node's object,
// the focused element changed on the one focused, an element destroyed
// when its node goes, the layout changed when the shown tree may have,
// and an announcement on the window for a live node's new name, with a
// high priority when it is assertive.

#include "access_record.h"
#include "ns.h"

static bool ValueChanged(const muiAccessNode* old, const muiAccessNode* node)
{
    const uint32_t state = mui_accessChecked | mui_accessMixed;
    bool numeric = (node->flags & mui_accessNumeric) != 0;
    return ((old->flags ^ node->flags) & state) != 0 || (numeric && old->value != node->value) ||
           muiRecordTextDiffers(old, node, mui_accessValue);
}

// Says a live node's name on the window.
static void Announce(const muiNsAdapter* adapter, const muiAccessNode* node)
{
    NSWindow* window = [adapter->view window];
    NSString* name = muiNsNameOf(adapter, node->id);
    if (window == nil || name == nil)
    {
        return;
    }
    NSAccessibilityPriorityLevel priority = node->values.live == mui_liveAssertive
                                                ? NSAccessibilityPriorityHigh
                                                : NSAccessibilityPriorityMedium;
    NSDictionary* info = @{
        NSAccessibilityAnnouncementKey : name,
        NSAccessibilityPriorityKey : [NSNumber numberWithInteger:priority],
    };
    adapter->post(window, NSAccessibilityAnnouncementRequestedNotification, info);
}

void muiNsTellUpdated(muiNsAdapter* adapter, const muiAccessNode* old, const muiAccessNode* node)
{
    bool named = muiRecordNameDiffers(old, node);
    bool valued = ValueChanged(old, node);
    MUIAccessibilityNode* object = named || valued ? muiNsObjectOf(adapter, node->id) : nil;
    if (object == nil)
    {
        return;
    }
    // Static text's name is its value.
    bool text = [muiNsRoleOf(node) isEqualToString:NSAccessibilityStaticTextRole];
    if (named)
    {
        adapter->post(object,
                      text ? NSAccessibilityValueChangedNotification
                           : NSAccessibilityTitleChangedNotification,
                      nil);
    }
    if (valued && !(named && text))
    {
        adapter->post(object, NSAccessibilityValueChangedNotification, nil);
    }
    if (named && node->values.live != mui_liveOff)
    {
        Announce(adapter, node);
    }
}

void muiNsTellDestroyed(const muiNsAdapter* adapter, MUIAccessibilityNode* object)
{
    adapter->post(object, NSAccessibilityUIElementDestroyedNotification, nil);
}

void muiNsTellLayout(muiNsAdapter* adapter)
{
    MUIAccessibilityNode* root = muiNsObjectOf(adapter, muiAccessTree_GetRoot(adapter->tree));
    if (root != nil)
    {
        adapter->post(root, NSAccessibilityLayoutChangedNotification, nil);
    }
}

void muiNsTellFocus(muiNsAdapter* adapter)
{
    MUIAccessibilityNode* focus = muiNsObjectOf(adapter, muiAccessTree_GetFocus(adapter->tree));
    if (focus != nil)
    {
        adapter->post(focus, NSAccessibilityFocusedUIElementChangedNotification, nil);
    }
}
