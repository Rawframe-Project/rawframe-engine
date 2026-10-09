// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The UIAccessibility adapter's internals (record mui-0008), for its
// Objective-C files: the adapter, the element and container objects,
// and what they share.

#ifndef MAUL_UI_SRC_UIKIT_H
#define MAUL_UI_SRC_UIKIT_H

#include "id_map.h"

#include "maul-ui/access_uikit.h"

#import <UIKit/UIKit.h>

// The element of a shown node, which VoiceOver stops on when the node
// says something; and the container of a shown node with shown
// children, never an element, whose elements are the node's element and
// then its children. Both hold the adapter and the node's id and look
// the node up at each call, so that one whose node is gone, or whose
// adapter is, answers nothing.
@interface MUIAccessibilityElement : UIAccessibilityElement
{
  @public
    muiUikitAdapter* adapter;
    uint64_t nodeId;
}
@end

@interface MUIAccessibilityContainer : UIAccessibilityElement
{
  @public
    muiUikitAdapter* adapter;
    uint64_t nodeId;
}
@end

// Posts a notification (UIAccessibilityPostNotification, or a test's).
typedef void (*muiUikitPostFunction)(UIAccessibilityNotifications notification, id argument);

struct muiUikitAdapter
{
    muiAllocator allocator;
    size_t blockSize;
    muiAccessTree* tree;
    UIView* view;
    float scale;
    muiUikitActionFunction action;
    void* user;
    uint32_t nodes;
    // Room for any node's shown children.
    uint64_t* scratch;
    // The objects made, by node id; the adapter holds a reference to each.
    muiIdMap elementById;
    muiIdMap containerById;
    muiUikitPostFunction post;
    // What the update being applied changed: the shown tree, the focus,
    // the node a new screen starts at (0 for none).
    bool reshaped;
    bool focusMoved;
    uint64_t screen;
};

// The element or the container of a held node, made when first asked
// for; nil for none.
MUIAccessibilityElement* muiUikitElementOf(muiUikitAdapter* adapter, uint64_t id);
MUIAccessibilityContainer* muiUikitContainerOf(muiUikitAdapter* adapter, uint64_t id);

// What stands for a shown node among its parent's elements: its
// container when it has shown children, else its element.
id muiUikitObjectOf(muiUikitAdapter* adapter, uint64_t node);

// The container a node's object lies in: its shown parent's, or the
// view for the root.
id muiUikitParentOf(muiUikitAdapter* adapter, uint64_t node);

// A node's shown children, into the adapter's scratch; how many.
uint32_t muiUikitChildrenOf(const muiUikitAdapter* adapter, uint64_t id);

// A node's box on the screen, in points.
CGRect muiUikitScreenRectOf(const muiUikitAdapter* adapter, uint64_t id);

// Asks the host for an action on a node; whether it did it.
bool muiUikitAct(const muiUikitAdapter* adapter, muiAccessAction action, uint64_t id, float value);

// A node's whole name, as clients read it; nil for none.
NSString* muiUikitNameOf(const muiUikitAdapter* adapter, uint64_t id);

// The notifications (uikit_events.m): a node added, an updated record,
// and, once the update is applied, what it changed, given the root
// before it.
void muiUikitTellAdded(muiUikitAdapter* adapter, uint64_t id);
void muiUikitTellUpdated(muiUikitAdapter* adapter, const muiAccessNode* old,
                         const muiAccessNode* node);
void muiUikitTellChanges(muiUikitAdapter* adapter, uint64_t oldRoot);

// A node's traits (uikit_traits.m), whether it is an element, and its
// container's type.
UIAccessibilityTraits muiUikitTraitsOf(const muiAccessNode* node);
bool muiUikitSays(const muiUikitAdapter* adapter, const muiAccessNode* node);
UIAccessibilityContainerType muiUikitContainerTypeOf(const muiAccessNode* node);

// A scroll by VoiceOver's direction as the node's action; whether the
// host did it.
bool muiUikitScroll(const muiUikitAdapter* adapter, const muiAccessNode* node,
                    UIAccessibilityScrollDirection direction);

#endif // MAUL_UI_SRC_UIKIT_H
