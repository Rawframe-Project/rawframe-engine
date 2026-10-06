// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The event store (record mui-0007): the host's event function and the
// route of the event being dispatched, which the context holds and
// src/event.c keeps.

#ifndef MAUL_UI_SRC_EVENT_STORE_H
#define MAUL_UI_SRC_EVENT_STORE_H

#include "maul-ui/event.h"

#include <stdbool.h>

typedef struct muiEventStore
{
    muiEventFunction function;
    void* user;
    // The route, target first, one id per node the context can hold, so
    // any depth fits.
    muiNodeId* route;
    // Set while the event function runs: input and dispatch are refused.
    bool dispatching;
} muiEventStore;

static inline void muiEventInit(muiEventStore* store, muiNodeId* route)
{
    *store = (muiEventStore){.route = route};
}

#endif // MAUL_UI_SRC_EVENT_STORE_H
