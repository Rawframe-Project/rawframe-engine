// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What style keeps: classes as property sets per variant, node types as
// class lists, and per node its classes, type, states and which of its
// properties are written directly (record mui-0004). Property sets live
// in a pool of their own so that a class pays only for the variants it
// uses.

#ifndef MAUL_UI_SRC_STYLE_STORE_H
#define MAUL_UI_SRC_STYLE_STORE_H

#include "pool.h"
#include "property.h"

#include "maul-ui/style.h"
#include "maul-ui/theme.h"

#include <stdbool.h>

enum
{
    // The variants a class can have: its base, its states and its
    // conditions.
    MUI_VARIANT_SLOTS = mui_variantCondition0 + MUI_MAX_CONDITIONS
};

typedef struct muiStyleClass
{
    // The property set of each variant, a slot of the set pool; 0 for
    // none.
    uint32_t sets[MUI_VARIANT_SLOTS];
    muiCondition conditions[MUI_MAX_CONDITIONS];
    uint32_t conditionCount;
} muiStyleClass;

typedef struct muiClassList
{
    muiStyleId classes[MUI_MAX_CLASSES];
    uint32_t count;
} muiClassList;

// One styling of a node with conditions: which held, as a signature, and
// the size they read. Direction is left out: no style value changes a
// size by way of direction, so it cannot oscillate on its own.
typedef struct muiConditionRun
{
    uint64_t outcome;
    float width;
    float height;
} muiConditionRun;

enum
{
    // The stylings a node remembers to see a cycle of two outcomes twice.
    MUI_CONDITION_HISTORY = 3
};

typedef struct muiNodeStyle
{
    muiClassList classes;
    muiNodeTypeId type;
    // The properties written directly, whose values are the node's
    // resolved ones.
    muiPropertyBits direct;
    // The states the host set; pointer input adds hover and press.
    muiState states;
    // The pointers whose hover chain, and whose press chain, holds the
    // node (src/pointer.c).
    uint8_t hovers;
    uint8_t presses;
    // The players focusing the node, and those showing it, as bits
    // (src/focus.c).
    uint8_t focusedBy;
    uint8_t shownBy;
    // Set by the host's edits of the node: the next styling is the host's,
    // not one its own layout asked for.
    bool edited;
    // The context's style edit count at the last styling.
    uint32_t editsSeen;
    // The stylings since the host last restyled the node, newest first.
    muiConditionRun history[MUI_CONDITION_HISTORY];
    uint32_t historyCount;
    // The node's first running transition, a record of the animation
    // pool; 0 for none. Here, not with layout's values, which every
    // layout walk reads.
    uint32_t firstAnimation;
    // Set once the node is styled: its first styling has nothing to move
    // from, so it applies at once, as CSS starts no transition for an
    // element without a before-change style.
    bool styled;
    // The theme set on the node, and the nearest node, itself or above,
    // whose theme lived when the node was last styled; 0 for none.
    muiThemeId theme;
    uint32_t scope;
} muiNodeStyle;

// A property of a set that reads a token, in the set's list of names.
typedef struct muiTokenName
{
    muiTokenId token;
    uint32_t next;
    muiProperty property;
} muiTokenName;

typedef struct muiStyleStore
{
    muiPool classPool;
    muiStyleClass* classes;
    muiPool typePool;
    muiClassList* types;
    muiPool setPool;
    muiPropertySet* sets;
    muiPool namePool;
    muiTokenName* names;
    // Per node, parallel to the node store's slots.
    muiNodeStyle* nodes;
    // Every property a class has given a value or a node has reset, never
    // cleared: any other property holds its default or a direct write,
    // so resolution passes it by.
    muiPropertyBits reach;
} muiStyleStore;

// Whether classes and count make a list a node or a type may hold.
static inline bool muiIsClassListValid(const muiStyleId* classes, uint32_t count)
{
    return count <= MUI_MAX_CLASSES && (classes != nullptr || count == 0);
}

static inline void muiSetClassList(muiClassList* list, const muiStyleId* classes, uint32_t count)
{
    *list = (muiClassList){.count = count};
    for (uint32_t i = 0; i < count; i++)
    {
        list->classes[i] = classes[i];
    }
}

// The states a node is in: the host's, and those pointers and focus give.
static inline muiState muiStatesOf(const muiNodeStyle* node)
{
    return (muiState)(node->states | (node->hovers != 0 ? mui_stateHovered : 0) |
                      (node->presses != 0 ? mui_statePressed : 0) |
                      (node->focusedBy != 0 ? mui_stateFocused : 0) |
                      (node->shownBy != 0 ? mui_stateFocusVisible : 0));
}

#endif // MAUL_UI_SRC_STYLE_STORE_H
