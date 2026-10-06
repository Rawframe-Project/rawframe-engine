// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Hit testing (record mui-0007). Layers are tried from the top, then the
// content they are not in. Each walk is preorder, as painting's, so the
// last node whose rounded border box holds the point is the topmost; a
// subtree whose hit mode is none, or below a clipping node the point is
// outside, is passed over whole. Origins add up in doubles, so walking
// back up takes off exactly what walking down added.

#include "context.h"
#include "layer.h"
#include "layout_node.h"
#include "paint.h"
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

// Tests a node at its origin; whether the point may hit its children.
static bool Visit(Walk* walk, uint32_t slot, double originX, double originY)
{
    const muiContext* context = walk->context;
    muiHitMode mode = context->interaction[slot - 1].hitMode;
    if (mode == mui_hitNone)
    {
        return false;
    }
    const muiLayoutNode* layout = &context->layout[slot - 1];
    const muiVisualStyle* visual = &context->visual[slot - 1];
    float x = walk->x - (float)originX;
    float y = walk->y - (float)originY;
    muiRect rect = {0.0f, 0.0f, layout->rect.width, layout->rect.height};
    bool inside =
        IsInside(x, y, rect.width, rect.height, muiCornersOf(&visual->radius, rect, layout->rtl));
    if (inside && mode == mui_hitAuto)
    {
        walk->found = slot;
        walk->foundX = x;
        walk->foundY = y;
    }
    return inside || !visual->clip;
}

// Walks a subtree from its parent's origin, leaving out the layers
// below its root, which are walked apart.
static void WalkSubtree(Walk* walk, uint32_t root, double parentX, double parentY)
{
    const muiContext* context = walk->context;
    const muiTree* tree = &context->tree;
    for (uint32_t at = root; at != 0;)
    {
        const muiRect* rect = &context->layout[at - 1].rect;
        double originX = parentX + (double)rect->x;
        double originY = parentY + (double)rect->y;
        const muiTreeNode* node = muiTreeAt(tree, at);
        bool apart = at != root && muiIsLayerRoot(tree, at);
        if (!apart && Visit(walk, at, originX, originY) && node->links.firstChild != 0)
        {
            parentX = originX;
            parentY = originY;
            at = node->links.firstChild;
            continue;
        }
        // On to the next sibling, or up to the first ancestor with one.
        while (at != root && muiTreeAt(tree, at)->links.next == 0)
        {
            at = muiTreeAt(tree, at)->links.parent;
            parentX -= (double)context->layout[at - 1].rect.x;
            parentY -= (double)context->layout[at - 1].rect.y;
        }
        at = at == root ? 0 : muiTreeAt(tree, at)->links.next;
    }
}

// The origin of a node's parent, its ancestors' places added up to the
// root.
static void ParentOrigin(const muiContext* context, uint32_t root, uint32_t node, double* xOut,
                         double* yOut)
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
    *xOut = x;
    *yOut = y;
}

// Tries the layers below the root from the top; true when one decides
// the walk: a node of it is hit, or it is modal.
static bool WalkLayers(Walk* walk, uint32_t root, bool* blockedOut)
{
    const muiContext* context = walk->context;
    for (uint32_t i = context->layers.count; i > 0; i--)
    {
        uint32_t layer = muiLayerAt(context, i - 1);
        if (layer == 0 || layer == root || !muiTreeIsAncestor(&context->tree, root, layer))
        {
            continue;
        }
        double x = 0.0;
        double y = 0.0;
        ParentOrigin(context, root, layer, &x, &y);
        WalkSubtree(walk, layer, x, y);
        if (walk->found != 0)
        {
            return true;
        }
        if (context->interaction[layer - 1].layer == mui_layerModal)
        {
            // Blocked: the modal layer's root, at the point.
            const muiRect* rect = &context->layout[layer - 1].rect;
            walk->found = layer;
            walk->foundX = walk->x - (float)(x + (double)rect->x);
            walk->foundY = walk->y - (float)(y + (double)rect->y);
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
        WalkSubtree(&walk, root, 0.0, 0.0);
    }
    *hitOut = (muiHit){.passThrough = true};
    if (walk.found != 0)
    {
        *hitOut = (muiHit){muiTreeIdOf(tree, walk.found), walk.foundX, walk.foundY,
                           !blocked && context->interaction[walk.found - 1].passThrough};
    }
    return mui_success;
}
