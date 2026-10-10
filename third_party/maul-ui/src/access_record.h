// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Comparing two records of a node, for the adapters that tell what an
// update changed (record mui-0008).

#ifndef MAUL_UI_SRC_ACCESS_RECORD_H
#define MAUL_UI_SRC_ACCESS_RECORD_H

#include "maul-ui/access.h"
#include "maul-ui/access_tree.h"

#include <stdbool.h>

// Whether two records' texts of a kind differ. The texts end in a NUL.
bool muiRecordTextDiffers(const muiAccessNode* old, const muiAccessNode* now,
                          muiAccessTextKind kind);

// The text a record names its node by: its label, or a label node's
// value.
muiAccessTextKind muiRecordNameKindOf(const muiAccessNode* node);

// Whether the record's own name changed: its text, or which text it is.
bool muiRecordNameDiffers(const muiAccessNode* old, const muiAccessNode* now);

// The focus a platform is shown: the tree's focus, or the active
// descendant it names (mui_relationActiveDescendant) when the tree holds
// it, as browsers show aria-activedescendant; 0 for none.
uint64_t muiRecordActiveFocus(const muiAccessTree* tree);

#endif // MAUL_UI_SRC_ACCESS_RECORD_H
