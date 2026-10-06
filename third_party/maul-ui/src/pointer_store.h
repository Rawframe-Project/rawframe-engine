// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The pointer store (record mui-0007): the pointers a context knows and
// the ring of records they posted, which the context holds and
// src/pointer.c keeps.

#ifndef MAUL_UI_SRC_POINTER_STORE_H
#define MAUL_UI_SRC_POINTER_STORE_H

#include "maul-ui/pointer.h"

#include <stdint.h>

// The most pointers a context knows at once: an edit's pointers fit a
// mask.
enum
{
    MUI_MAX_POINTERS = 32
};

// A pointer the context knows. hovered and pressed are counted in their
// chains' nodes; captured is in hovered's place while it lives.
typedef struct muiPointer
{
    uint32_t id;
    muiPointerKind kind;
    muiPointerButtons buttons;
    float x;
    float y;
    // Its last event's time.
    uint64_t timeNs;
    muiNodeId hovered;
    muiNodeId pressed;
    muiNodeId captured;
    // The root its last event was under, for drag records' points.
    muiNodeId root;
    // The node its press may drag, where the press began on the surface,
    // and whether the drag started.
    muiNodeId dragged;
    float pressX;
    float pressY;
    // A drag started this press (no click follows), and has not ended.
    bool dragStarted;
    bool dragging;
    // What its drag offers to drop, 0 for nothing, and the target under
    // it.
    uint32_t offerKind;
    uint64_t offerKey;
    muiNodeId target;
} muiPointer;

// The last press, which the next continues a series of.
typedef struct muiClickSeries
{
    uint64_t timeNs;
    float x;
    float y;
    muiPointerKind kind;
    uint8_t button;
    uint32_t count;
} muiClickSeries;

typedef struct muiPointerStore
{
    muiPointer* pointers;
    uint32_t count;
    uint32_t capacity;
    // A ring, as the notification queue's: once full, records are
    // counted into one dropped record.
    muiPointerRecord* records;
    uint32_t recordCapacity;
    uint32_t head;
    uint32_t recordCount;
    uint32_t dropped;
    muiClickSeries series;
    uint64_t clickIntervalNs;
    float clickDistance;
    float dragMouse;
    float dragTouch;
} muiPointerStore;

static inline void muiPointerInit(muiPointerStore* store, muiPointer* pointers, uint32_t capacity,
                                  muiPointerRecord* records, uint32_t recordCapacity)
{
    *store = (muiPointerStore){
        .pointers = pointers,
        .capacity = capacity,
        .records = records,
        .recordCapacity = recordCapacity,
        .clickIntervalNs = 500000000u,
        .clickDistance = 2.0f,
        .dragMouse = 4.0f,
        .dragTouch = 8.0f,
    };
}

#endif // MAUL_UI_SRC_POINTER_STORE_H
