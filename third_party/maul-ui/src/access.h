// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Accessibility (record mui-0008): what the builder and the actions read
// of the host's data and the enabled roots.

#ifndef MAUL_UI_SRC_ACCESS_H
#define MAUL_UI_SRC_ACCESS_H

#include "access_store.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct muiContext muiContext;

// The entry of the live node at slot; NULL for one without data.
muiAccessEntry* muiAccessEntryOf(const muiContext* context, uint32_t slot);

// Whether text is well-formed UTF-8 without a NUL.
bool muiAccessIsUtf8(const unsigned char* text, size_t length);

// Whether two sets of typed values agree.
bool muiAccessSameValues(const muiAccessValues* a, const muiAccessValues* b);

// The enabled root at slot; NULL for one not enabled.
muiAccessRoot* muiAccessRootOf(const muiContext* context, uint32_t slot);

// What a node is now, as an update would send it, without its children's
// place in the update; its children, in the order sent, go to
// childrenOut (slots), their count returned; with childrenOut NULL, 0.
uint32_t muiAccessDerive(const muiContext* context, uint32_t slot, muiAccessNode* nodeOut,
                         uint32_t* childrenOut);

#endif // MAUL_UI_SRC_ACCESS_H
