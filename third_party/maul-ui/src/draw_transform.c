// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The values of a list's transforms (records mui-0005, mui-0007): each
// its owner's scale or scroll after its parent entry's, from the
// context, so a build can give them once its walk is done and a paint
// function can ask for one before.

#include "draw_transform.h"

#include "layout_node.h"
#include "scroll_store.h"
#include "tree.h"

#include <math.h>

// The origin of a node's parent: its ancestors' places added up to the
// list's root, in doubles as hit testing adds them.
void muiParentOrigin(const muiContext* context, uint32_t root, uint32_t node, float* xOut,
                     float* yOut)
{
    const muiTree* tree = &context->tree;
    double x = 0.0;
    double y = 0.0;
    for (uint32_t at = node; at != root;)
    {
        at = muiTreeAt(tree, at)->links.parent;
        x += (double)context->layout[at - 1].rect.x;
        y += (double)context->layout[at - 1].rect.y;
    }
    *xOut = (float)x;
    *yOut = (float)y;
}

// A scaled node's transform: its parent entry's after a scale about the
// node's origin where layout puts it in the list, which stays. From
// layout, as the node may not be painted (below one that draws nothing).
static muiDrawTransform ScaledOf(const muiContext* context, uint32_t root, uint32_t owner,
                                 const muiDrawTransform* parent)
{
    const muiLocalScale* scale = &context->visual[owner - 1].scale;
    const muiRect* rect = &context->layout[owner - 1].rect;
    float x = 0.0f;
    float y = 0.0f;
    muiParentOrigin(context, root, owner, &x, &y);
    float fraction = context->layout[owner - 1].rtl ? 1.0f - scale->originX : scale->originX;
    x += rect->x + fraction * rect->width;
    y += rect->y + scale->originY * rect->height;
    // The scale's own: x' = s x + x (1 - s).
    float e = x * (1.0f - scale->x);
    float f = y * (1.0f - scale->y);
    return (muiDrawTransform){parent->a * scale->x,
                              parent->b * scale->x,
                              parent->c * scale->y,
                              parent->d * scale->y,
                              parent->a * e + parent->c * f + parent->e,
                              parent->b * e + parent->d * f + parent->f};
}

muiDrawTransform muiTransformAfter(const muiContext* context, uint32_t root, uint32_t owner,
                                   const muiDrawTransform* parent, float scale)
{
    uint32_t slot = owner & ~MUI_TRANSFORM_SCALE;
    if ((owner & MUI_TRANSFORM_SCALE) != 0)
    {
        return ScaledOf(context, root, slot, parent);
    }
    const muiScrollState* scroll = &context->scrolls[slot - 1];
    const muiLayoutNode* layout = &context->layout[slot - 1];
    float x = roundf(muiScrollShiftX(layout, scroll) * scale) / scale;
    float y = roundf(muiScrollShiftY(layout, scroll) * scale) / scale;
    muiDrawTransform value = *parent;
    value.e += parent->a * x + parent->c * y;
    value.f += parent->b * x + parent->d * y;
    return value;
}

void muiSetTransforms(const muiContext* context, uint32_t root, muiDrawTables* tables, float scale)
{
    for (uint32_t i = 1; i < tables->transformCount; i++)
    {
        tables->transforms[i] =
            muiTransformAfter(context, root, tables->transformOwners[i],
                              &tables->transforms[tables->transformParents[i]], scale);
    }
}

muiDrawTransform muiTransformOf(const muiContext* context, uint32_t root,
                                const muiDrawTables* tables, uint32_t index, float scale)
{
    muiDrawTransform value = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    // From the outermost entry in, each the one whose parent was the last
    // applied: chains are short, a node's scales and scrolls above it.
    for (uint32_t done = 0; done != index;)
    {
        uint32_t at = index;
        while (tables->transformParents[at] != done)
        {
            at = tables->transformParents[at];
        }
        value = muiTransformAfter(context, root, tables->transformOwners[at], &value, scale);
        done = at;
    }
    return value;
}
