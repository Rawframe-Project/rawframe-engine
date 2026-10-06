// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Host content's glyph runs and rectangles (record mui-0005). A run's
// glyphs are copied into the list's glyph table as given; its baseline
// snaps to a device pixel at the identity transform, and its x keeps
// subpixel precision. A rectangle is a box of one fill, snapped as boxes
// are.

#include "paint_host.h"

#include "layout_node.h"
#include "tree.h"

#include "maul-ui/draw.h"

#include <math.h>
#include <string.h>

struct muiDrawSink
{
    muiPainter* painter;
    const muiPaintState* state;
    // The content box's top left on the surface.
    float x;
    float y;
};

static bool IsUnit(float value)
{
    return value >= 0.0f && value <= 1.0f;
}

static bool IsRunValid(const muiGlyphRun* run)
{
    return isfinite(run->size) && run->size > 0.0f && IsUnit(run->color.r) &&
           IsUnit(run->color.g) && IsUnit(run->color.b) && IsUnit(run->color.a) &&
           isfinite(run->originX) && isfinite(run->originY);
}

static bool AreGlyphsValid(const muiGlyph* glyphs, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
    {
        if (!isfinite(glyphs[i].x) || !isfinite(glyphs[i].y))
        {
            return false;
        }
    }
    return true;
}

muiResult muiDrawSink_AddGlyphRun(muiDrawSink* sink, const muiGlyphRun* run, const muiGlyph* glyphs,
                                  uint32_t glyphCount)
{
    if (sink == nullptr)
    {
        return mui_errorInvalid;
    }
    muiPainter* painter = sink->painter;
    if (run == nullptr || glyphs == nullptr || glyphCount == 0 || !IsRunValid(run) ||
        !AreGlyphsValid(glyphs, glyphCount))
    {
        painter->misuse++;
        return mui_errorInvalid;
    }
    muiDrawTables* out = painter->out;
    if (glyphCount > painter->glyphCapacity - out->glyphCount)
    {
        painter->full = true;
        return mui_errorCapacity;
    }
    muiDrawCommand* command = muiTakeCommand(painter, mui_drawGlyphRun, sink->state->clip);
    if (command == nullptr)
    {
        return mui_errorCapacity;
    }
    muiDrawGlyphRun* drawn = &command->glyphRun;
    drawn->font = run->font;
    drawn->originX = sink->x + run->originX;
    drawn->originY = muiSnapEdge(sink->y + run->originY, painter->scale);
    drawn->size = run->size;
    drawn->firstGlyph = out->glyphCount;
    drawn->glyphCount = glyphCount;
    drawn->color = muiPaintColor(painter, run->color, sink->state->opacity);
    memcpy(&out->glyphs[out->glyphCount], glyphs, glyphCount * sizeof *glyphs);
    out->glyphCount += glyphCount;
    return mui_success;
}

muiResult muiDrawSink_AddRect(muiDrawSink* sink, muiRect rect, muiColor color)
{
    if (sink == nullptr)
    {
        return mui_errorInvalid;
    }
    muiPainter* painter = sink->painter;
    if (!isfinite(rect.x) || !isfinite(rect.y) || !isfinite(rect.width) || !isfinite(rect.height) ||
        rect.width < 0.0f || rect.height < 0.0f || !IsUnit(color.r) || !IsUnit(color.g) ||
        !IsUnit(color.b) || !IsUnit(color.a))
    {
        painter->misuse++;
        return mui_errorInvalid;
    }
    muiDrawCommand* command = muiTakeCommand(painter, mui_drawBox, sink->state->clip);
    if (command == nullptr)
    {
        return mui_errorCapacity;
    }
    rect.x += sink->x;
    rect.y += sink->y;
    command->box = (muiDrawBox){.rect = muiSnapRect(rect, painter->scale),
                                .fill = muiPaintColor(painter, color, sink->state->opacity)};
    return mui_success;
}

void muiPaintHostContent(muiPainter* painter, uint32_t slot, const muiPaintState* state)
{
    const muiContext* context = painter->context;
    const muiLayoutNode* layout = &context->layout[slot - 1];
    if (painter->paint == nullptr || layout->style.content != mui_contentHost)
    {
        return;
    }
    // The content box, as layout sized it: inside the border and padding,
    // whose start is the right in a right-to-left node.
    const muiEdges* border = &layout->style.border;
    const muiEdges* padding = &layout->style.padding;
    float start = border->start + padding->start;
    float end = border->end + padding->end;
    float top = border->top + padding->top;
    float bottom = border->bottom + padding->bottom;
    float left = layout->rtl ? end : start;
    muiDrawSink sink = {painter, state, state->x + left, state->y + top};
    float width = fmaxf(layout->rect.width - start - end, 0.0f);
    float height = fmaxf(layout->rect.height - top - bottom, 0.0f);
    const muiTreeNode* node = muiTreeAt(&context->tree, slot);
    painter->paint(painter->paintUser, muiTreeIdOf(&context->tree, slot), node->hostKey, width,
                   height, &sink);
}
