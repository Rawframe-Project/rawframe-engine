// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Where nodes are on the surface (record mui-0005).

#include "place.h"

#include "context.h"
#include "layout_node.h"
#include "scroll_store.h"
#include "tree.h"

muiPlace muiPlaceStep(const muiContext* context, uint32_t slot)
{
    const muiLayoutNode* layout = &context->layout[slot - 1];
    const muiLocalScale* scale = &context->visual[slot - 1].scale;
    if (scale->x == 1.0f && scale->y == 1.0f)
    {
        return (muiPlace){1.0, 1.0, (double)layout->rect.x, (double)layout->rect.y};
    }
    // About the origin, which stays: x' = rect.x + o + s (x - o).
    double originX =
        (double)(layout->rtl ? 1.0f - scale->originX : scale->originX) * (double)layout->rect.width;
    double originY = (double)scale->originY * (double)layout->rect.height;
    return (muiPlace){(double)scale->x, (double)scale->y,
                      (double)layout->rect.x + originX * (1.0 - (double)scale->x),
                      (double)layout->rect.y + originY * (1.0 - (double)scale->y)};
}

// outer after inner.
static muiPlace Then(const muiPlace* outer, const muiPlace* inner)
{
    return (muiPlace){outer->scaleX * inner->scaleX, outer->scaleY * inner->scaleY,
                      outer->scaleX * inner->offsetX + outer->offsetX,
                      outer->scaleY * inner->offsetY + outer->offsetY};
}

// From a node's children's space to its border box's: by its scrolling.
static muiPlace ShiftOf(const muiContext* context, uint32_t slot)
{
    const muiLayoutNode* layout = &context->layout[slot - 1];
    const muiScrollState* scroll = &context->scrolls[slot - 1];
    return (muiPlace){1.0, 1.0, (double)muiScrollShiftX(layout, scroll),
                      (double)muiScrollShiftY(layout, scroll)};
}

// From at's children's space up.
static muiPlace Up(const muiContext* context, uint32_t root, uint32_t at, muiPlace place)
{
    const muiTree* tree = &context->tree;
    for (;;)
    {
        const muiPlace shift = ShiftOf(context, at);
        place = Then(&shift, &place);
        const muiPlace step = muiPlaceStep(context, at);
        place = Then(&step, &place);
        if (at == root)
        {
            return place;
        }
        at = muiTreeAt(tree, at)->links.parent;
        if (at == 0)
        {
            return place;
        }
    }
}

muiPlace muiPlaceOf(const muiContext* context, uint32_t root, uint32_t node)
{
    muiPlace place = muiPlaceStep(context, node);
    uint32_t parent = node == root ? 0 : muiTreeAt(&context->tree, node)->links.parent;
    return parent != 0 ? Up(context, root, parent, place) : place;
}

muiPlace muiPlaceOfChildren(const muiContext* context, uint32_t root, uint32_t node)
{
    return Up(context, root, node, (muiPlace){1.0, 1.0, 0.0, 0.0});
}
