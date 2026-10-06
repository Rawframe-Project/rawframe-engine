// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Running transitions (record mui-0004): one record per moving property
// of a node, in a pool reserved at creation, linked from the node. A
// record holds where the property started and is going; the node's
// resolved values hold where it is now, which advancing moves.

#ifndef MAUL_UI_SRC_ANIMATION_H
#define MAUL_UI_SRC_ANIMATION_H

#include "easing.h"
#include "inherit.h"
#include "layout_node.h"
#include "pool.h"
#include "spring.h"
#include "style_store.h"
#include "tree.h"

#include "maul-ui/layout.h"
#include "maul-ui/transition.h"

#include <stdbool.h>

// A spec as created, with its curve worked out.
typedef struct muiTransitionSpec
{
    muiTransitionDef def;
    muiCurve curve;
} muiTransitionSpec;

typedef struct muiAnimation
{
    // The node, its next record, and the property.
    muiNodeId node;
    uint32_t next;
    muiProperty property;
    uint32_t channels;
    muiTransitionSpec spec;
    // When the property changed, and when its motion starts.
    uint64_t changedNs;
    uint64_t startNs;
    float from[MUI_MAX_CHANNELS];
    float to[MUI_MAX_CHANNELS];
    // The target as stored, written on arrival, so that a value moved
    // through other channels (a color through Oklab) ends exactly on it.
    muiPropertyValue target;
    // For a timed transition: its length, and the start a reversal would
    // return to with the share of the way it would cover (CSS
    // Transitions' reversing-adjusted start value and shortening factor).
    double seconds;
    float reversingStart[MUI_MAX_CHANNELS];
    double shortening;
    // For a spring: its shape, each channel's start from to, and how close
    // to rest each channel counts as at rest.
    muiSpringShape shape;
    muiSpringStart starts[MUI_MAX_CHANNELS];
    float rest[MUI_MAX_CHANNELS];
} muiAnimation;

typedef struct muiAnimationStore
{
    muiPool specPool;
    muiTransitionSpec* specs;
    muiPool pool;
    muiAnimation* records;
    // The records that run.
    uint32_t running;
} muiAnimationStore;

// What transitions work on: the store, the nodes' layout, visual and text
// values, their style data (which holds each node's first record), the
// tree they mark and the text records a moved text value recomputes.
typedef struct muiMotion
{
    muiAnimationStore* store;
    muiLayoutNode* nodes;
    muiVisualStyle* visuals;
    muiNodeStyle* styles;
    muiTree* tree;
    muiTextStyle* texts;
    muiTextRecord* textRecords;
    muiInteractionStyle* interactions;
} muiMotion;

// The live spec an id names, or NULL.
const muiTransitionSpec* muiFindSpec(const muiAnimationStore* store, muiTransitionId id);

// The record of a node's property, or 0.
uint32_t muiFindAnimation(const muiMotion* motion, uint32_t node, muiProperty property);

// Moves a node's property from its current channels to its value in
// target with spec, from now; a running record of it is retargeted, keeping a
// spring's speed or shortening a reversal. Returns false when no record
// is free, and the caller applies the change at once.
bool muiStartAnimation(const muiMotion* motion, uint32_t node, muiProperty property,
                       muiConstValuesRef target, const muiTransitionSpec* spec, uint64_t nowNs);

// Ends the transition of a node's property, if one runs, leaving the
// property where it is.
void muiStopAnimation(const muiMotion* motion, uint32_t node, muiProperty property);

// Moves every running transition to nowNs, or to its end when
// finish is set, marking the layout of each node it moves.
void muiAdvanceAnimations(const muiMotion* motion, uint64_t nowNs, bool finish);

// Whether a transition runs on root or below it.
bool muiIsAnimatingUnder(const muiAnimationStore* store, const muiTree* tree, uint32_t root);

#endif // MAUL_UI_SRC_ANIMATION_H
