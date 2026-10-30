// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The layout node's helpers a header cannot define inline: a recursive
// one is never inlined, so each source calling it would link a copy of
// its own.

#include "layout_node.h"

bool muiScaledBelow(const muiTree* tree, muiLayoutNode* nodes, uint32_t node)
{
    muiLayoutCache* cache = &nodes[node - 1].cache;
    if (cache->scaledBelow == 0)
    {
        bool scaled = false;
        for (uint32_t c = muiTreeAt(tree, node)->links.firstChild; c != 0 && !scaled;
             c = muiTreeAt(tree, c)->links.next)
        {
            const muiLayoutStyle* style = &nodes[c - 1].style;
            const muiSizing* sizing = &style->sizing;
            scaled = muiIsScaled(sizing->width) || muiIsScaled(sizing->height) ||
                     muiIsScaled(sizing->minWidth) || muiIsScaled(sizing->maxWidth) ||
                     muiIsScaled(sizing->minHeight) || muiIsScaled(sizing->maxHeight) ||
                     muiIsScaled(style->item.basis) || sizing->aspectRatio != 0.0f ||
                     muiScaledBelow(tree, nodes, c);
        }
        cache->scaledBelow = scaled ? 2 : 1;
    }
    return cache->scaledBelow == 2;
}
