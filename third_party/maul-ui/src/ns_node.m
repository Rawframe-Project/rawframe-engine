// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The NSAccessibility adapter's node objects (record mui-0008): the
// protocol answered from the tree at each call. A node's name is its
// title, or its value for static text; a check box's value is its state,
// a range's its number; the actions are asked of the host. Which methods
// apply is answered per node, as AccessKit does, so that clients offer
// only what a node has.

#include "allocator.h"
#include "ns.h"

// A node's text as a string, nil for none.
static NSString* StringOf(const char* text)
{
    return text != nullptr ? [NSString stringWithUTF8String:text] : nil;
}

static const muiAccessNode* NodeOf(const MUIAccessibilityNode* object)
{
    return object->adapter != nullptr ? muiAccessTree_Find(object->adapter->tree, object->nodeId)
                                      : nullptr;
}

NSString* muiNsNameOf(const muiNsAdapter* adapter, uint64_t id)
{
    size_t length = 0;
    char small[256];
    muiResult status = muiAccessTree_GetName(adapter->tree, id, small, sizeof(small), &length);
    if (status == mui_success)
    {
        return [NSString stringWithUTF8String:small];
    }
    if (status != mui_errorCapacity)
    {
        return nil;
    }
    char* name = muiAllocate(&adapter->allocator, length + 1, 1);
    NSString* string = nil;
    if (name != nullptr &&
        muiAccessTree_GetName(adapter->tree, id, name, length + 1, &length) == mui_success)
    {
        string = [NSString stringWithUTF8String:name];
    }
    if (name != nullptr)
    {
        muiRelease(&adapter->allocator, name, length + 1, 1);
    }
    return string;
}

static bool IsStaticText(const muiAccessNode* node)
{
    return [muiNsRoleOf(node) isEqualToString:NSAccessibilityStaticTextRole];
}

static bool Has(const muiAccessNode* node, muiAccessAction action)
{
    return (node->actions & (1u << action)) != 0;
}

@implementation MUIAccessibilityNode

- (id)accessibilityParent
{
    const muiAccessNode* node = NodeOf(self);
    if (node == nullptr)
    {
        return nil;
    }
    uint64_t parent = muiAccessTree_GetShownParent(adapter->tree, nodeId);
    return parent != 0 ? muiNsObjectOf(adapter, parent) : adapter->view;
}

- (NSArray*)accessibilityChildren
{
    uint32_t count = 0;
    if (NodeOf(self) == nullptr ||
        muiAccessTree_GetShownChildren(adapter->tree, nodeId, adapter->scratch, adapter->nodes,
                                       &count) != mui_success)
    {
        return @[];
    }
    NSMutableArray* children = [NSMutableArray arrayWithCapacity:count];
    for (uint32_t i = 0; i < count; i++)
    {
        MUIAccessibilityNode* child = muiNsObjectOf(adapter, adapter->scratch[i]);
        if (child != nil)
        {
            [children addObject:child];
        }
    }
    return children;
}

- (NSRect)accessibilityFrame
{
    return NodeOf(self) != nullptr ? muiNsScreenRectOf(adapter, nodeId) : NSZeroRect;
}

- (id)accessibilityWindow
{
    return NodeOf(self) != nullptr ? [adapter->view window] : nil;
}

- (id)accessibilityTopLevelUIElement
{
    return [self accessibilityWindow];
}

- (NSAccessibilityRole)accessibilityRole
{
    const muiAccessNode* node = NodeOf(self);
    return node != nullptr ? muiNsRoleOf(node) : NSAccessibilityUnknownRole;
}

- (NSAccessibilitySubrole)accessibilitySubrole
{
    const muiAccessNode* node = NodeOf(self);
    return node != nullptr ? muiNsSubroleOf(node) : nil;
}

- (NSString*)accessibilityRoleDescription
{
    const muiAccessNode* node = NodeOf(self);
    if (node == nullptr)
    {
        return nil;
    }
    NSString* own = StringOf(node->text[mui_accessRoleDescription]);
    return own != nil ? own
                      : NSAccessibilityRoleDescription(muiNsRoleOf(node), muiNsSubroleOf(node));
}

- (NSString*)accessibilityTitle
{
    const muiAccessNode* node = NodeOf(self);
    return node != nullptr && !IsStaticText(node) ? muiNsNameOf(adapter, nodeId) : nil;
}

- (NSString*)accessibilityHelp
{
    const muiAccessNode* node = NodeOf(self);
    return node != nullptr ? StringOf(node->text[mui_accessDescription]) : nil;
}

- (NSString*)accessibilityPlaceholderValue
{
    const muiAccessNode* node = NodeOf(self);
    return node != nullptr ? StringOf(node->text[mui_accessPlaceholder]) : nil;
}

// Static text's name; a check box's state (0, 1, or 2 for mixed); a
// range's number; else the node's value text.
- (id)accessibilityValue
{
    const muiAccessNode* node = NodeOf(self);
    if (node == nullptr)
    {
        return nil;
    }
    uint32_t flags = node->flags;
    if (IsStaticText(node))
    {
        return muiNsNameOf(adapter, nodeId);
    }
    if ((flags & mui_accessCheckable) != 0)
    {
        int state = (flags & mui_accessMixed) != 0 ? 2 : (flags & mui_accessChecked) != 0 ? 1 : 0;
        return [NSNumber numberWithInt:state];
    }
    if ((flags & mui_accessNumeric) != 0)
    {
        return [NSNumber numberWithDouble:(double)node->value];
    }
    return StringOf(node->text[mui_accessValue]);
}

- (void)setAccessibilityValue:(id)value
{
    const muiAccessNode* node = NodeOf(self);
    if (node != nullptr && Has(node, mui_actionSetValue) &&
        [value respondsToSelector:@selector(floatValue)])
    {
        (void)muiNsAct(adapter, mui_actionSetValue, nodeId, [value floatValue]);
    }
}

- (id)accessibilityMinValue
{
    const muiAccessNode* node = NodeOf(self);
    return node != nullptr && (node->flags & mui_accessNumeric) != 0
               ? [NSNumber numberWithDouble:(double)node->minimum]
               : nil;
}

- (id)accessibilityMaxValue
{
    const muiAccessNode* node = NodeOf(self);
    return node != nullptr && (node->flags & mui_accessNumeric) != 0
               ? [NSNumber numberWithDouble:(double)node->maximum]
               : nil;
}

- (NSAccessibilityOrientation)accessibilityOrientation
{
    const muiAccessNode* node = NodeOf(self);
    muiAccessOrientation orientation = node != nullptr ? node->values.orientation : 0;
    return orientation == mui_orientationHorizontal ? NSAccessibilityOrientationHorizontal
           : orientation == mui_orientationVertical ? NSAccessibilityOrientationVertical
                                                    : NSAccessibilityOrientationUnknown;
}

- (BOOL)isAccessibilityElement
{
    return NodeOf(self) != nullptr && muiAccessTree_IsShown(adapter->tree, nodeId);
}

- (BOOL)isAccessibilityEnabled
{
    const muiAccessNode* node = NodeOf(self);
    return node != nullptr && (node->flags & mui_accessDisabled) == 0;
}

- (BOOL)isAccessibilityFocused
{
    return NodeOf(self) != nullptr && muiAccessTree_GetFocus(adapter->tree) == nodeId;
}

- (void)setAccessibilityFocused:(BOOL)focused
{
    if (focused && NodeOf(self) != nullptr)
    {
        (void)muiNsAct(adapter, mui_actionFocus, nodeId, 0.0f);
    }
}

- (BOOL)isAccessibilityRequired
{
    const muiAccessNode* node = NodeOf(self);
    return node != nullptr && (node->flags & mui_accessRequired) != 0;
}

- (BOOL)isAccessibilitySelected
{
    const muiAccessNode* node = NodeOf(self);
    return node != nullptr && (node->flags & mui_accessSelected) != 0;
}

- (BOOL)isAccessibilityExpanded
{
    const muiAccessNode* node = NodeOf(self);
    return node != nullptr && (node->flags & mui_accessExpanded) != 0;
}

- (BOOL)isAccessibilityModal
{
    const muiAccessNode* node = NodeOf(self);
    return node != nullptr && (node->flags & mui_accessModal) != 0;
}

// The click action, or expanding and collapsing what does that without
// one, as on the web.
- (BOOL)accessibilityPerformPress
{
    const muiAccessNode* node = NodeOf(self);
    if (node == nullptr)
    {
        return NO;
    }
    if (Has(node, mui_actionClick))
    {
        return muiNsAct(adapter, mui_actionClick, nodeId, 0.0f);
    }
    bool expanded = (node->flags & mui_accessExpanded) != 0;
    muiAccessAction toggle = expanded ? mui_actionCollapse : mui_actionExpand;
    return Has(node, toggle) && muiNsAct(adapter, toggle, nodeId, 0.0f);
}

- (BOOL)accessibilityPerformIncrement
{
    const muiAccessNode* node = NodeOf(self);
    return node != nullptr && Has(node, mui_actionIncrement) &&
           muiNsAct(adapter, mui_actionIncrement, nodeId, 0.0f);
}

- (BOOL)accessibilityPerformDecrement
{
    const muiAccessNode* node = NodeOf(self);
    return node != nullptr && Has(node, mui_actionDecrement) &&
           muiNsAct(adapter, mui_actionDecrement, nodeId, 0.0f);
}

- (id)accessibilityFocusedUIElement
{
    return NodeOf(self) != nullptr ? muiNsObjectOf(adapter, muiAccessTree_GetFocus(adapter->tree))
                                   : nil;
}

- (id)accessibilityHitTest:(NSPoint)point
{
    if (NodeOf(self) == nullptr)
    {
        return nil;
    }
    uint64_t hit = muiNsNodeAt(adapter, point);
    return hit != 0 ? muiNsObjectOf(adapter, hit) : nil;
}

// Which methods apply to this node.
- (BOOL)isAccessibilitySelectorAllowed:(SEL)selector
{
    const muiAccessNode* node = NodeOf(self);
    if (node == nullptr)
    {
        return NO;
    }
    uint32_t flags = node->flags;
    if (selector == @selector(setAccessibilityFocused:))
    {
        return (flags & mui_accessFocusable) != 0;
    }
    if (selector == @selector(accessibilityPerformPress))
    {
        return Has(node, mui_actionClick) || Has(node, mui_actionExpand) ||
               Has(node, mui_actionCollapse);
    }
    if (selector == @selector(accessibilityPerformIncrement))
    {
        return Has(node, mui_actionIncrement);
    }
    if (selector == @selector(accessibilityPerformDecrement))
    {
        return Has(node, mui_actionDecrement);
    }
    if (selector == @selector(setAccessibilityValue:))
    {
        return Has(node, mui_actionSetValue);
    }
    if (selector == @selector(isAccessibilitySelected))
    {
        return (flags & mui_accessSelectable) != 0;
    }
    if (selector == @selector(isAccessibilityExpanded))
    {
        return (flags & mui_accessExpandable) != 0;
    }
    if (selector == @selector(isAccessibilityModal))
    {
        return (flags & mui_accessModal) != 0;
    }
    return [super isAccessibilitySelectorAllowed:selector];
}

@end
