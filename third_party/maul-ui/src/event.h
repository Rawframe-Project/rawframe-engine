// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Routed input: routing an event to a node for the modules that deliver
// input of their own.

#ifndef MAUL_UI_SRC_EVENT_H
#define MAUL_UI_SRC_EVENT_H

#include "maul-ui/event.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct muiContext muiContext;

// Whether the event function is running: what feeds input, dispatches,
// performs an access action, sets the event function or destroys the
// context is refused then with mui_errorBusy.
bool muiIsRouting(const muiContext* context);

// Routes an event to the node at target, down from the top of its tree
// and back up; whether the host handled it.
bool muiRouteTo(muiContext* context, uint32_t target, const muiEvent* event);

#endif // MAUL_UI_SRC_EVENT_H
