// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context's notification queue: a ring reserved at creation. A
// record that finds the ring full is counted instead, and so is every
// record after it until the ring drains, so that the count arrives as
// one mui_notificationDropped record in the place the first lost one
// had.

#ifndef MAUL_UI_SRC_NOTIFY_H
#define MAUL_UI_SRC_NOTIFY_H

#include "maul-ui/context.h"

#include <stdbool.h>

typedef struct muiNotifyQueue
{
    muiNotification* records;
    uint32_t capacity;
    // The oldest record's index and how many wait.
    uint32_t head;
    uint32_t count;
    // Records lost since the ring was last full.
    uint32_t dropped;
} muiNotifyQueue;

void muiNotifyInit(muiNotifyQueue* queue, muiNotification* records, uint32_t capacity);

void muiNotifyPost(muiNotifyQueue* queue, const muiNotification* record);

// Takes the oldest record; false when none waits.
bool muiNotifyTake(muiNotifyQueue* queue, muiNotification* recordOut);

#endif // MAUL_UI_SRC_NOTIFY_H
