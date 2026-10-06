// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The accessibility tree's consumer (record mui-0008): the index of held
// nodes by id, which applying updates edits.

#ifndef MAUL_UI_SRC_ACCESS_TREE_H
#define MAUL_UI_SRC_ACCESS_TREE_H

#include "access_tree_store.h"

#include <stdint.h>

// Where an id starts its search in the index.
uint32_t muiAccessHome(const muiAccessTree* tree, uint64_t id);

// Enters the node at slot in the index, or takes it out, by its id.
void muiAccessIndex(muiAccessTree* tree, uint32_t slot);
void muiAccessUnindex(muiAccessTree* tree, uint32_t slot);

#endif // MAUL_UI_SRC_ACCESS_TREE_H
