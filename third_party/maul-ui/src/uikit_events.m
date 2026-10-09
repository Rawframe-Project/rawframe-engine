// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The UIAccessibility adapter's notifications (record mui-0008), as UIKit
// has them and Flutter posts them. UIKit has none for a name or a value
// changing: VoiceOver reads them again when it reaches the element. A
// new screen (the root changing, or a node turning modal) is told with
// the element VoiceOver should move to; else a layout change, with the
// focused element when the focus moved, or none. A live node's new name
// is announced, queued behind current speech when polite.

#include "access_record.h"
#include "uikit.h"

static void Announce(const muiUikitAdapter* adapter, const muiAccessNode* node)
{
    NSString* name = muiUikitNameOf(adapter, node->id);
    if (name == nil)
    {
        return;
    }
    if (node->values.live == mui_liveAssertive)
    {
        adapter->post(UIAccessibilityAnnouncementNotification, name);
        return;
    }
    NSDictionary* queued = @{UIAccessibilitySpeechAttributeQueueAnnouncement : @YES};
    NSAttributedString* text = [[NSAttributedString alloc] initWithString:name attributes:queued];
    adapter->post(UIAccessibilityAnnouncementNotification, text);
    [text release];
}

static bool IsModal(const muiAccessNode* node)
{
    return (node->flags & mui_accessModal) != 0;
}

void muiUikitTellAdded(muiUikitAdapter* adapter, uint64_t id)
{
    const muiAccessNode* node = muiAccessTree_Find(adapter->tree, id);
    if (node != nullptr && IsModal(node))
    {
        adapter->screen = id;
    }
}

void muiUikitTellUpdated(muiUikitAdapter* adapter, const muiAccessNode* old,
                         const muiAccessNode* node)
{
    if (IsModal(node) && !IsModal(old))
    {
        adapter->screen = node->id;
    }
    if (node->values.live != mui_liveOff && muiRecordNameDiffers(old, node))
    {
        Announce(adapter, node);
    }
}

void muiUikitTellChanges(muiUikitAdapter* adapter, uint64_t oldRoot)
{
    uint64_t root = muiAccessTree_GetRoot(adapter->tree);
    uint64_t focus = muiAccessTree_GetFocus(adapter->tree);
    if (adapter->screen == 0 && root != oldRoot && root != 0)
    {
        adapter->screen = focus != 0 ? focus : root;
    }
    if (adapter->screen != 0)
    {
        adapter->post(UIAccessibilityScreenChangedNotification,
                      muiUikitElementOf(adapter, adapter->screen));
    }
    else if (adapter->focusMoved)
    {
        adapter->post(UIAccessibilityLayoutChangedNotification, muiUikitElementOf(adapter, focus));
    }
    else if (adapter->reshaped)
    {
        adapter->post(UIAccessibilityLayoutChangedNotification, nil);
    }
}
