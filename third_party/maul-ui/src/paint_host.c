// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Host content's glyph runs and rectangles. A run's glyphs are copied into
// the list's glyph table as given; its baseline snaps to a device pixel at
// the identity transform, and its x keeps subpixel precision. A rectangle
// is a box of one fill, snapped as boxes are.

#include "paint_host.h"

#include "draw_transform.h"
#include "fpenv.h"
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
    // Set when the paint function could not paint all it had to.
    bool failed;
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

static MUI_NOINLINE muiResult DrawSinkAddGlyphRunWorker(muiDrawSink* sink, const muiGlyphRun* run,
                                                        const muiGlyph* glyphs, uint32_t glyphCount)
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
    muiDrawCommand* command = muiTakeCommand(painter, mui_drawGlyphRun, sink->state);
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

muiResult muiDrawSink_AddGlyphRun(muiDrawSink* sink, const muiGlyphRun* run, const muiGlyph* glyphs,
                                  uint32_t glyphCount)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = DrawSinkAddGlyphRunWorker(sink, run, glyphs, glyphCount);
    muiFloatEnvLeave(floatEnv);
    return result;
}

// Narrows a span from low up to high to where it meets one.
static void Meet(float* low, float* high, float otherLow, float otherHigh)
{
    *low = fmaxf(*low, otherLow);
    *high = fminf(*high, otherHigh);
}

static MUI_NOINLINE muiResult DrawSinkGetVisibleRectWorker(muiDrawSink* sink, muiRect* rectOut)
{
    if (rectOut != nullptr)
    {
        memset(rectOut, 0, sizeof *rectOut);
    }
    if (sink == nullptr || rectOut == nullptr)
    {
        return mui_errorInvalid;
    }
    muiPainter* painter = sink->painter;
    const muiContext* context = painter->context;
    const muiDrawTables* out = painter->out;
    const muiRect* surface = &context->layout[painter->root - 1].rect;
    // On the surface, through every clip the content is drawn in, each
    // as its rectangle: corners and transforms only take away.
    float left = 0.0f;
    float top = 0.0f;
    float right = surface->width;
    float bottom = surface->height;
    for (uint32_t at = sink->state->clip; at != 0; at = out->clips[at].parent)
    {
        const muiDrawClip* clip = &out->clips[at];
        muiDrawTransform t =
            muiTransformOf(context, painter->root, out, clip->transform, painter->scale);
        Meet(&left, &right, t.a * clip->rect.x + t.e,
             t.a * (clip->rect.x + clip->rect.width) + t.e);
        Meet(&top, &bottom, t.d * clip->rect.y + t.f,
             t.d * (clip->rect.y + clip->rect.height) + t.f);
    }
    // Back through the content's transform, from the content box's top
    // left; scaled to nothing, nothing shows.
    muiDrawTransform t =
        muiTransformOf(context, painter->root, out, sink->state->transform, painter->scale);
    *rectOut = (muiRect){0.0f, 0.0f, 0.0f, 0.0f};
    if (t.a > 0.0f && t.d > 0.0f && right > left && bottom > top)
    {
        float x = (left - t.e) / t.a - sink->x;
        float y = (top - t.f) / t.d - sink->y;
        *rectOut = (muiRect){x, y, (right - left) / t.a, (bottom - top) / t.d};
    }
    painter->culled++;
    return mui_success;
}

muiResult muiDrawSink_Fail(muiDrawSink* sink)
{
    if (sink == nullptr)
    {
        return mui_errorInvalid;
    }
    sink->failed = true;
    return mui_success;
}

muiResult muiDrawSink_GetVisibleRect(muiDrawSink* sink, muiRect* rectOut)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = DrawSinkGetVisibleRectWorker(sink, rectOut);
    muiFloatEnvLeave(floatEnv);
    return result;
}

static MUI_NOINLINE muiResult DrawSinkAddRectWorker(muiDrawSink* sink, muiRect rect, muiColor color)
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
    muiDrawCommand* command = muiTakeCommand(painter, mui_drawBox, sink->state);
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

muiResult muiDrawSink_AddRect(muiDrawSink* sink, muiRect rect, muiColor color)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = DrawSinkAddRectWorker(sink, rect, color);
    muiFloatEnvLeave(floatEnv);
    return result;
}

// The host's paint function, run in the host's floating-point
// environment.
static MUI_NOINLINE void HostPaint(const muiPainter* painter, muiNodeId nodeId, uint64_t hostKey,
                                   float width, float height, muiDrawSink* sink)
{
    muiFloatHost host = muiFloatHostEnter();
    painter->paint(painter->paintUser, nodeId, hostKey, width, height, sink);
    muiFloatHostLeave(host);
}

bool muiPaintHostContent(muiPainter* painter, uint32_t slot, const muiPaintState* state)
{
    const muiContext* context = painter->context;
    const muiLayoutNode* layout = &context->layout[slot - 1];
    if (painter->paint == nullptr || layout->style.content != mui_contentHost)
    {
        return true;
    }
    const muiRect content = muiContentBoxOf(layout, &context->paddings[slot - 1]);
    muiDrawSink sink = {painter, state, state->x + content.x, state->y + content.y, false};
    const muiTreeNode* node = muiTreeAt(&context->tree, slot);
    HostPaint(painter, muiTreeIdOf(&context->tree, slot), node->hostKey, content.width,
              content.height, &sink);
    return !sink.failed;
}
