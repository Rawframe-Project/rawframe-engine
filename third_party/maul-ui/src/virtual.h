// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Virtualization (record mui-0007), within the library.

#ifndef MAUL_UI_SRC_VIRTUAL_H
#define MAUL_UI_SRC_VIRTUAL_H

#include "context.h"
#include "virtual_store.h"

#include <stdbool.h>
#include <stdint.h>

// The entry of the list at slot, and of a node; NULL for none, with the
// status to return for a node in statusOut.
muiVirtualEntry* muiVirtualEntryOf(const muiContext* context, uint32_t slot);
muiVirtualEntry* muiVirtualFind(const muiContext* context, muiNodeId nodeId, muiResult* statusOut);

// Takes out the entries of lists destroyed since.
void muiVirtualPurge(muiContext* context);

// Whether an entry holds item storage: an estimated list with items.
bool muiVirtualStores(const muiVirtualEntry* entry);

// The first start from which count items fit in the item storage, apart
// from every entry's but self's; the capacity when none does.
uint32_t muiVirtualRoom(const muiVirtualStore* store, const muiVirtualEntry* self, uint32_t count);

// Builds an estimated entry's tree from its items' extents in O(count).
void muiVirtualBuild(muiVirtualStore* store, const muiVirtualEntry* entry);

// Where item i begins: the extents and gaps of the items before it.
double muiVirtualOffset(const muiVirtualStore* store, const muiVirtualEntry* entry, uint32_t i);

// Where the list at slot's viewport begins in its items' coordinates.
double muiVirtualViewStart(const muiContext* context, uint32_t slot, const muiVirtualEntry* entry);

// Moves the list at slot's offset along its axis by delta, not below 0.
void muiVirtualShift(muiContext* context, uint32_t slot, const muiVirtualEntry* entry,
                     double delta);

// After layout, before scrolling moves: measures the bound items of the
// lists under root into their extents, places them at their offsets, and
// sizes each list's content to all its items.
void muiVirtualPlace(muiContext* context, uint32_t root);

// After scrolling moves: finds each list's window under root, reporting
// those that changed.
void muiVirtualWindows(muiContext* context, uint32_t root);

#endif // MAUL_UI_SRC_VIRTUAL_H
