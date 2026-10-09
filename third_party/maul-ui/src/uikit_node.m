// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The UIAccessibility adapter's objects (record mui-0008), answered from
// the tree at each call. VoiceOver never looks inside an element, so a
// shown node with shown children is a container, not an element, whose
// first element is the node's own, as Flutter splits them; the
// container's frame is the view's, so that a child outside its parent's
// box is still found by touch. The label is the name; the value a
// toggle's state, a range's text or number, or the node's value text;
// the actions are asked of the host.

#include "uikit.h"

static NSString* StringOf(const char* text)
{
    return text != nullptr ? [NSString stringWithUTF8String:text] : nil;
}

static bool Has(const muiAccessNode* node, muiAccessAction action)
{
    return (node->actions & (1u << action)) != 0;
}

// A held node, when the object still has its adapter.
static const muiAccessNode* Find(const muiUikitAdapter* adapter, uint64_t id)
{
    return adapter != nullptr ? muiAccessTree_Find(adapter->tree, id) : nullptr;
}

static bool IsToggle(const muiAccessNode* node)
{
    return node->role == mui_roleSwitch ||
           (node->role == mui_roleButton && (node->flags & mui_accessCheckable) != 0);
}

// A range's value text, or its number as the user's locale writes it.
static NSString* RangeValueOf(const muiAccessNode* node)
{
    NSString* text = StringOf(node->text[mui_accessValue]);
    if (text != nil)
    {
        return text;
    }
    return
        [NSNumberFormatter localizedStringFromNumber:[NSNumber numberWithDouble:(double)node->value]
                                         numberStyle:NSNumberFormatterDecimalStyle];
}

@implementation MUIAccessibilityElement

- (BOOL)isAccessibilityElement
{
    const muiAccessNode* node = Find(adapter, nodeId);
    return node != nullptr && muiAccessTree_IsShown(adapter->tree, nodeId) &&
           muiUikitSays(adapter, node);
}

- (id)accessibilityContainer
{
    if (Find(adapter, nodeId) == nullptr)
    {
        return nil;
    }
    // The node's own container when it has one, else its parent's.
    id object = muiUikitObjectOf(adapter, nodeId);
    return object != self ? object : muiUikitParentOf(adapter, nodeId);
}

- (CGRect)accessibilityFrame
{
    return Find(adapter, nodeId) != nullptr ? muiUikitScreenRectOf(adapter, nodeId) : CGRectZero;
}

- (NSString*)accessibilityLabel
{
    return Find(adapter, nodeId) != nullptr ? muiUikitNameOf(adapter, nodeId) : nil;
}

- (NSString*)accessibilityHint
{
    const muiAccessNode* node = Find(adapter, nodeId);
    return node != nullptr ? StringOf(node->text[mui_accessDescription]) : nil;
}

// A toggle's state, 1 or 0, as UIKit's switches give it; a range's
// value; else the value text, but for a label, whose name it is.
- (NSString*)accessibilityValue
{
    const muiAccessNode* node = Find(adapter, nodeId);
    if (node == nullptr || node->role == mui_roleLabel)
    {
        return nil;
    }
    if (IsToggle(node))
    {
        return (node->flags & mui_accessChecked) != 0 ? @"1" : @"0";
    }
    if ((node->flags & mui_accessNumeric) != 0)
    {
        return RangeValueOf(node);
    }
    return StringOf(node->text[mui_accessValue]);
}

- (UIAccessibilityTraits)accessibilityTraits
{
    const muiAccessNode* node = Find(adapter, nodeId);
    return node != nullptr ? muiUikitTraitsOf(node) : UIAccessibilityTraitNone;
}

- (BOOL)accessibilityViewIsModal
{
    const muiAccessNode* node = Find(adapter, nodeId);
    return node != nullptr && (node->flags & mui_accessModal) != 0;
}

// The click action, or expanding and collapsing; a range without a
// click takes the double tap without acting, which would otherwise set
// it where the finger is. Otherwise UIKit taps the element's middle.
- (BOOL)accessibilityActivate
{
    const muiAccessNode* node = Find(adapter, nodeId);
    if (node == nullptr)
    {
        return NO;
    }
    if (Has(node, mui_actionClick))
    {
        return muiUikitAct(adapter, mui_actionClick, nodeId, 0.0f);
    }
    bool expanded = (node->flags & mui_accessExpanded) != 0;
    muiAccessAction toggle = expanded ? mui_actionCollapse : mui_actionExpand;
    if (Has(node, toggle))
    {
        return muiUikitAct(adapter, toggle, nodeId, 0.0f);
    }
    return (node->flags & mui_accessNumeric) != 0;
}

- (void)accessibilityIncrement
{
    const muiAccessNode* node = Find(adapter, nodeId);
    if (node != nullptr && Has(node, mui_actionIncrement))
    {
        (void)muiUikitAct(adapter, mui_actionIncrement, nodeId, 0.0f);
    }
}

- (void)accessibilityDecrement
{
    const muiAccessNode* node = Find(adapter, nodeId);
    if (node != nullptr && Has(node, mui_actionDecrement))
    {
        (void)muiUikitAct(adapter, mui_actionDecrement, nodeId, 0.0f);
    }
}

- (BOOL)accessibilityScroll:(UIAccessibilityScrollDirection)direction
{
    const muiAccessNode* node = Find(adapter, nodeId);
    return node != nullptr && muiUikitScroll(adapter, node, direction);
}

// Maul UI has no action for the escape gesture.
- (BOOL)accessibilityPerformEscape
{
    return NO;
}

// VoiceOver's cursor reached the node: shown, scrolled in.
- (void)accessibilityElementDidBecomeFocused
{
    const muiAccessNode* node = Find(adapter, nodeId);
    if (node != nullptr && Has(node, mui_actionScrollIntoView))
    {
        (void)muiUikitAct(adapter, mui_actionScrollIntoView, nodeId, 0.0f);
    }
}

@end

@implementation MUIAccessibilityContainer

- (BOOL)isAccessibilityElement
{
    return NO;
}

- (id)accessibilityContainer
{
    return Find(adapter, nodeId) != nullptr ? muiUikitParentOf(adapter, nodeId) : nil;
}

- (CGRect)accessibilityFrame
{
    if (Find(adapter, nodeId) == nullptr)
    {
        return CGRectZero;
    }
    UIView* view = adapter->view;
    return UIAccessibilityConvertFrameToScreenCoordinates([view bounds], view);
}

// The node's name, which VoiceOver reads entering a group or landmark.
- (NSString*)accessibilityLabel
{
    return Find(adapter, nodeId) != nullptr ? muiUikitNameOf(adapter, nodeId) : nil;
}

- (UIAccessibilityContainerType)accessibilityContainerType
{
    const muiAccessNode* node = Find(adapter, nodeId);
    return node != nullptr ? muiUikitContainerTypeOf(node) : UIAccessibilityContainerTypeNone;
}

- (BOOL)accessibilityViewIsModal
{
    const muiAccessNode* node = Find(adapter, nodeId);
    return node != nullptr && (node->flags & mui_accessModal) != 0;
}

// The node's element, then each shown child's object.
- (NSArray*)accessibilityElements
{
    if (Find(adapter, nodeId) == nullptr)
    {
        return @[];
    }
    uint32_t count = muiUikitChildrenOf(adapter, nodeId);
    NSMutableArray* elements = [NSMutableArray arrayWithCapacity:count + 1];
    MUIAccessibilityElement* own = muiUikitElementOf(adapter, nodeId);
    if (own != nil)
    {
        [elements addObject:own];
    }
    // The objects are made after the ids are read: making one asks the
    // tree, not the scratch.
    for (uint32_t i = 0; i < count; i++)
    {
        id child = muiUikitObjectOf(adapter, adapter->scratch[i]);
        if (child != nil)
        {
            [elements addObject:child];
        }
    }
    return elements;
}

- (NSInteger)accessibilityElementCount
{
    return (NSInteger)[[self accessibilityElements] count];
}

- (id)accessibilityElementAtIndex:(NSInteger)index
{
    NSArray* elements = [self accessibilityElements];
    return index >= 0 && (NSUInteger)index < [elements count] ? elements[(NSUInteger)index] : nil;
}

- (NSInteger)indexOfAccessibilityElement:(id)element
{
    NSUInteger index = [[self accessibilityElements] indexOfObject:element];
    return index != NSNotFound ? (NSInteger)index : NSNotFound;
}

- (BOOL)accessibilityScroll:(UIAccessibilityScrollDirection)direction
{
    const muiAccessNode* node = Find(adapter, nodeId);
    return node != nullptr && muiUikitScroll(adapter, node, direction);
}

@end
