// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The NSAccessibility adapter's internals (record mui-0008), for its
// Objective-C files: the adapter, the node objects, and what they share.

#ifndef MAUL_UI_SRC_NS_H
#define MAUL_UI_SRC_NS_H

#include "id_map.h"

#include "maul-ui/access_ns.h"

#import <AppKit/AppKit.h>

// The object of a shown node: the adapter and the node's id, looked up
// at each call, so that an object whose node is gone, or whose adapter
// is, answers nothing.
@interface MUIAccessibilityNode : NSAccessibilityElement
{
  @public
    muiNsAdapter* adapter;
    uint64_t nodeId;
}
@end

// Posts a notification on an element (NSAccessibilityPostNotification,
// or a test's).
typedef void (*muiNsPostFunction)(id element, NSAccessibilityNotificationName name,
                                  NSDictionary* info);

struct muiNsAdapter
{
    muiAllocator allocator;
    size_t blockSize;
    muiAccessTree* tree;
    NSView* view;
    float scale;
    muiNsActionFunction action;
    void* user;
    uint32_t nodes;
    // Room for any node's shown children.
    uint64_t* scratch;
    // The objects made, by node id; the adapter holds a reference to each.
    muiIdMap objectById;
    muiNsPostFunction post;
    // What the update being applied changed: the shown tree, the focus.
    bool reshaped;
    bool focusMoved;
};

// The object of a held node, made when first asked for; nil for none.
MUIAccessibilityNode* muiNsObjectOf(muiNsAdapter* adapter, uint64_t id);

// A node's box on the screen, in points.
NSRect muiNsScreenRectOf(const muiNsAdapter* adapter, uint64_t id);

// The deepest shown node under a point on the screen, 0 for none.
uint64_t muiNsNodeAt(const muiNsAdapter* adapter, NSPoint screen);

// Asks the host for an action on a node; whether it did it.
bool muiNsAct(const muiNsAdapter* adapter, muiAccessAction action, uint64_t id, float value);

// A node's whole name, as clients read it; nil for none.
NSString* muiNsNameOf(const muiNsAdapter* adapter, uint64_t id);

// The notifications (ns_events.m): what an updated record changed, an
// object whose node went, the layout, the focus.
void muiNsTellUpdated(muiNsAdapter* adapter, const muiAccessNode* old, const muiAccessNode* node);
void muiNsTellDestroyed(const muiNsAdapter* adapter, MUIAccessibilityNode* object);
void muiNsTellLayout(muiNsAdapter* adapter);
void muiNsTellFocus(muiNsAdapter* adapter);

// A node's AppKit role and subrole (nil for none).
NSAccessibilityRole muiNsRoleOf(const muiAccessNode* node);
NSAccessibilitySubrole muiNsSubroleOf(const muiAccessNode* node);

#endif // MAUL_UI_SRC_NS_H
