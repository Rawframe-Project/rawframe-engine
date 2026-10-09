// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Comparing two records of a node, for the adapters that tell what an
// update changed (record mui-0008).

#ifndef MAUL_UI_SRC_ACCESS_RECORD_H
#define MAUL_UI_SRC_ACCESS_RECORD_H

#include "maul-ui/access.h"

#include <stdbool.h>

// Whether two records' texts of a kind differ. The texts end in a NUL.
bool muiRecordTextDiffers(const muiAccessNode* old, const muiAccessNode* now,
                          muiAccessTextKind kind);

// The text a record names its node by: its label, or a label node's
// value.
muiAccessTextKind muiRecordNameKindOf(const muiAccessNode* node);

// Whether the record's own name changed: its text, or which text it is.
bool muiRecordNameDiffers(const muiAccessNode* old, const muiAccessNode* now);

#endif // MAUL_UI_SRC_ACCESS_RECORD_H
