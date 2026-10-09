// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Hit testing (record mui-0007). Layers are tried from the top, then the
// content they are not in. Each walk is preorder, as painting's, so the
// last node whose rounded border box holds the point is the topmost; a
// subtree whose hit mode is none, or below a clipping node the point is
// outside, is passed over whole. The point goes into each node's space as
// the walk goes down, in doubles, and back out as it comes up: places
// alone are taken off and added back exactly, and a local scale is
// divided out and multiplied back (record mui-0005).

#include "context.h"
#include "layer.h"
#include "layout_node.h"
#include "paint.h"
#include "place.h"
#include "tree.h"

#include "maul-ui/interaction.h"

#include <math.h>

// Whether a point, from a rounded box's top left, is in it: in the box,
// half open, and inside the circle of any corner whose square it is in.
static bool IsInside(float x, float y, float width, float height, muiCorners radii)
{
    if (!(x >= 0.0f && y >= 0.0f && x < width && y < height))
    {
        return false;
    }
    float right = width - x;
    float bottom = height - y;
    const float corners[4][3] = {{radii.topLeft, x, y},
                                 {radii.topRight, right, y},
                                 {radii.bottomRight, right, bottom},
                                 {radii.bottomLeft, x, bottom}};
    for (int i = 0; i < 4; i++)
    {
        float radius = corners[i][0];
        float dx = radius - corners[i][1];
        float dy = radius - corners[i][2];
        if (dx > 0.0f && dy > 0.0f && dx * dx + dy * dy > radius * radius)
        {
            return false;
        }
    }
    return true;
}

// Whether a point, from a box's top left, is in its rounded padding box:
// the border box less the borders, its corners' radii less the wider
// border at each.
static bool IsInsidePadding(const muiLayoutNode* layout, float x, float y, muiCorners radii)
{
    const muiEdges* border = &layout->style.border;
    float left = layout->rtl ? border->end : border->start;
    float right = layout->rtl ? border->start : border->end;
    const muiCorners inner = {
        fmaxf(radii.topLeft - fmaxf(left, border->top), 0.0f),
        fmaxf(radii.topRight - fmaxf(right, border->top), 0.0f),
        fmaxf(radii.bottomRight - fmaxf(right, border->bottom), 0.0f),
        fmaxf(radii.bottomLeft - fmaxf(left, border->bottom), 0.0f),
    };
    return IsInside(x - left, y - border->top, layout->rect.width - left - right,
                    layout->rect.height - border->top - border->bottom, inner);
}

// One walk: the point, and the topmost node found so far with the point
// in its border box.
typedef struct Walk
{
    const muiContext* context;
    float x;
    float y;
    uint32_t found;
    float foundX;
    float foundY;
} Walk;

// Tests a node at the point in its own space; whether the point may hit
// its children. A node scaled to nothing on an axis is hit nowhere.
static bool Visit(Walk* walk, uint32_t slot, double localX, double localY)
{
    const muiContext* context = walk->context;
    muiHitMode mode = context->interaction[slot - 1].hitMode;
    const muiVisualStyle* visual = &context->visual[slot - 1];
    if (mode == mui_hitNone || visual->scale.x == 0.0f || visual->scale.y == 0.0f)
    {
        return false;
    }
    const muiLayoutNode* layout = &context->layout[slot - 1];
    float x = (float)localX;
    float y = (float)localY;
    muiRect rect = {0.0f, 0.0f, layout->rect.width, layout->rect.height};
    muiCorners radii = muiCornersOf(&visual->radius, rect, layout->rtl);
    bool inside = IsInside(x, y, rect.width, rect.height, radii);
    if (inside && mode == mui_hitAuto)
    {
        walk->found = slot;
        walk->foundX = x;
        walk->foundY = y;
    }
    if (layout->style.scrollAxes != mui_scrollNone)
    {
        // A scroll container's children are cut at its padding box.
        return IsInsidePadding(layout, x, y, radii);
    }
    return inside || !visual->clip;
}

static float ShiftX(const muiContext* context, uint32_t slot)
{
    return muiScrollShiftX(&context->layout[slot - 1], &context->scrolls[slot - 1]);
}

static float ShiftY(const muiContext* context, uint32_t slot)
{
    return muiScrollShiftY(&context->layout[slot - 1], &context->scrolls[slot - 1]);
}

// Walks a subtree with the point in its root's parent's children's space,
// leaving out the layers below its root, which are walked apart.
static void WalkSubtree(Walk* walk, uint32_t root, double x, double y)
{
    const muiContext* context = walk->context;
    const muiTree* tree = &context->tree;
    for (uint32_t at = root; at != 0;)
    {
        const muiPlace step = muiPlaceStep(context, at);
        double localX = muiUnplaceX(&step, x);
        double localY = muiUnplaceY(&step, y);
        const muiTreeNode* node = muiTreeAt(tree, at);
        // Layers are walked apart; exiting nodes take no input.
        bool skip =
            (at != root && muiIsLayerRoot(tree, at)) || (node->flags & MUI_TREE_EXITING) != 0;
        if (!skip && Visit(walk, at, localX, localY) && node->links.firstChild != 0)
        {
            // Children are where a scroll container moves them.
            x = localX - (double)ShiftX(context, at);
            y = localY - (double)ShiftY(context, at);
            at = node->links.firstChild;
            continue;
        }
        // On to the next sibling, or up to the first ancestor with one.
        while (at != root && muiTreeAt(tree, at)->links.next == 0)
        {
            at = muiTreeAt(tree, at)->links.parent;
            const muiPlace up = muiPlaceStep(context, at);
            x = muiPlaceX(&up, x + (double)ShiftX(context, at));
            y = muiPlaceY(&up, y + (double)ShiftY(context, at));
        }
        at = at == root ? 0 : muiTreeAt(tree, at)->links.next;
    }
}

// Tries the layers below the root from the top; true when one decides
// the walk: a node of it is hit, or it is modal.
static bool WalkLayers(Walk* walk, uint32_t root, bool* blockedOut)
{
    const muiContext* context = walk->context;
    for (uint32_t i = context->layers.count; i > 0; i--)
    {
        uint32_t layer = muiLayerAt(context, i - 1);
        if (layer == 0 || layer == root || !muiTreeIsAncestor(&context->tree, root, layer) ||
            muiTreeIsExiting(&context->tree, layer))
        {
            continue;
        }
        // The point in the space of the layer's parent's children,
        // through its ancestors' places, scrolling and scales; below one
        // scaled to nothing, the layer is hit nowhere.
        const muiPlace parent =
            muiPlaceOfChildren(context, root, muiTreeAt(&context->tree, layer)->links.parent);
        if (parent.scaleX == 0.0 || parent.scaleY == 0.0)
        {
            continue;
        }
        double x = muiUnplaceX(&parent, (double)walk->x);
        double y = muiUnplaceY(&parent, (double)walk->y);
        WalkSubtree(walk, layer, x, y);
        if (walk->found != 0)
        {
            return true;
        }
        if (context->interaction[layer - 1].layer == mui_layerModal)
        {
            // Blocked: the modal layer's root, at the point.
            const muiPlace step = muiPlaceStep(context, layer);
            walk->found = layer;
            walk->foundX = (float)muiUnplaceX(&step, x);
            walk->foundY = (float)muiUnplaceY(&step, y);
            *blockedOut = true;
            return true;
        }
    }
    return false;
}

muiResult muiHitTest(const muiContext* context, muiNodeId rootId, float x, float y, muiHit* hitOut)
{
    if (context == nullptr || hitOut == nullptr || rootId.index1 == 0 || !isfinite(x) ||
        !isfinite(y))
    {
        return mui_errorInvalid;
    }
    const muiTree* tree = &context->tree;
    uint32_t root = muiTreeResolve(tree, rootId);
    if (root == 0)
    {
        return mui_errorStale;
    }
    Walk walk = {context, x, y, 0, 0.0f, 0.0f};
    bool blocked = false;
    if (!WalkLayers(&walk, root, &blocked))
    {
        WalkSubtree(&walk, root, (double)x, (double)y);
    }
    *hitOut = (muiHit){.passThrough = true};
    if (walk.found != 0)
    {
        *hitOut = (muiHit){muiTreeIdOf(tree, walk.found), walk.foundX, walk.foundY,
                           !blocked && context->interaction[walk.found - 1].passThrough};
    }
    return mui_success;
}

muiResult muiNode_MapToRoot(const muiContext* context, muiNodeId nodeId, float x, float y,
                            float* xOut, float* yOut)
{
    if (context == nullptr || xOut == nullptr || yOut == nullptr || !isfinite(x) || !isfinite(y))
    {
        return mui_errorInvalid;
    }
    uint32_t slot = muiTreeResolve(&context->tree, nodeId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    // Through the node's place and scale, then each ancestor's with the
    // scrolling of its children, in doubles as hit testing goes.
    const muiPlace place = muiPlaceOf(context, 0, slot);
    *xOut = (float)muiPlaceX(&place, (double)x);
    *yOut = (float)muiPlaceY(&place, (double)y);
    return mui_success;
}
