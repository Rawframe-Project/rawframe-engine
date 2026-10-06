// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Routed input (record mui-0007): routing an event to a node for the
// modules that deliver input of their own.

#ifndef MAUL_UI_SRC_EVENT_H
#define MAUL_UI_SRC_EVENT_H

#include "maul-ui/event.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct muiContext muiContext;

// Whether input may be fed now: not from a measure, paint or event
// function.
bool muiMayFeed(const muiContext* context);

// Routes an event to the node at target, down from the top of its tree
// and back up; whether the host handled it.
bool muiRouteTo(muiContext* context, uint32_t target, const muiEvent* event);

#endif // MAUL_UI_SRC_EVENT_H
