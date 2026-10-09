// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Where nodes are on the surface (record mui-0005): their laid-out places,
// their parents' scrolling and their local scales, as per-axis affines in
// doubles. With no scale, each is a sum of the places, added as before.

#ifndef MAUL_UI_SRC_PLACE_H
#define MAUL_UI_SRC_PLACE_H

#include <stdint.h>

typedef struct muiContext muiContext;

// A point's x goes to scaleX * x + offsetX, its y alike.
typedef struct muiPlace
{
    double scaleX;
    double scaleY;
    double offsetX;
    double offsetY;
} muiPlace;

// A node's own step: from its border box's space to its parent's
// children's, where layout put it, through its local scale.
muiPlace muiPlaceStep(const muiContext* context, uint32_t slot);

// From a node's border box's space to root's parent's children's: its
// step, and each ancestor's up to root with the scrolling of each parent
// on the way; to the top of the tree for root 0.
muiPlace muiPlaceOf(const muiContext* context, uint32_t root, uint32_t node);

// From the space of a node's children (where its scrolling moves them)
// to root's parent's children's.
muiPlace muiPlaceOfChildren(const muiContext* context, uint32_t root, uint32_t node);

// A point through a place.
static inline double muiPlaceX(const muiPlace* place, double x)
{
    return place->scaleX * x + place->offsetX;
}

static inline double muiPlaceY(const muiPlace* place, double y)
{
    return place->scaleY * y + place->offsetY;
}

// A point back through a place: 0 on an axis it scales to nothing.
static inline double muiUnplaceX(const muiPlace* place, double x)
{
    return place->scaleX != 0.0 ? (x - place->offsetX) / place->scaleX : 0.0;
}

static inline double muiUnplaceY(const muiPlace* place, double y)
{
    return place->scaleY != 0.0 ? (y - place->offsetY) / place->scaleY : 0.0;
}

#endif // MAUL_UI_SRC_PLACE_H
