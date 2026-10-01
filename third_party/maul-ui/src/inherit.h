// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text style as nodes inherit it (record mui-0004): what a node's text
// computes to, from its own values where a layer gives them and its
// parent's where not, and the line height and letter spacing as written,
// which its children inherit as written.

#ifndef MAUL_UI_SRC_INHERIT_H
#define MAUL_UI_SRC_INHERIT_H

#include "layout_node.h"
#include "tree.h"

#include "maul-ui/text_style.h"

typedef struct muiTextRecord
{
    muiComputedTextStyle computed;
    muiDimension lineHeight;
    muiDimension letterSpacing;
    // The text properties a layer or a direct write gives the node, and
    // the parent it was computed under.
    muiPropertyMask given;
    muiNodeId parent;
} muiTextRecord;

// The per-node arrays inheritance reads and writes, parallel to the
// tree's slots.
typedef struct muiTextNodes
{
    muiTree* tree;
    const muiLayoutNode* layout;
    const muiTextStyle* own;
    muiTextRecord* records;
} muiTextNodes;

// The record of a node that is given nothing, under no parent: the
// defaults.
muiTextRecord muiRootTextRecord(void);

// Whether a node's record may be out of date: it was computed with
// other given properties or under another parent.
bool muiIsTextRecordStale(const muiTextNodes* nodes, uint32_t node, muiPropertyMask given);

// Computes a node's record, and its descendants' while theirs change.
// A node with host content whose record changes is marked to be measured
// again for what sizes text, or painted again for its color or alignment.
void muiInheritText(const muiTextNodes* nodes, uint32_t node);

#endif // MAUL_UI_SRC_INHERIT_H
