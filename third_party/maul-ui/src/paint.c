// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Painting one node (record mui-0005). Records are zeroed before their
// fields are written, so equal trees give equal bytes.

#include "paint.h"

#include "color.h"
#include "layout_node.h"

#include "maul-ui/draw.h"

#include <math.h>
#include <string.h>

// Records with no padding, so that their bytes are their fields'.
static_assert(sizeof(muiDrawBox) == 136 && sizeof(muiDrawShadow) == 68 &&
                  sizeof(muiDrawImage) == 72 && sizeof(muiDrawGlyphRun) == 48 &&
                  sizeof(muiGlyph) == 12 && sizeof(muiDrawCommand) == 152 &&
                  sizeof(muiDrawClip) == 44 && sizeof(muiDrawGradient) == 96,
              "draw records have no padding");

// A color in linear light times opacity, converted from the cache when a
// node before converted the same bits.
muiLinearColor muiPaintColor(muiPainter* painter, muiColor color, float opacity)
{
    uint32_t words[4];
    memcpy(words, &color, sizeof color);
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < 4; i++)
    {
        hash = (hash ^ words[i]) * 16777619u;
    }
    muiCachedColor* entry = &painter->colors[hash % MUI_PAINT_COLOR_CACHE];
    if (memcmp(entry->bits, words, sizeof words) != 0)
    {
        memcpy(entry->bits, words, sizeof words);
        muiColorToLinearRgb(color, entry->rgb);
    }
    return muiPremultiply(entry->rgb, color.a, opacity);
}

// An edge at the nearest device pixel, halves away from the origin's
// left.
float muiSnapEdge(float value, float scale)
{
    return floorf(value * scale + 0.5f) / scale;
}

// A span from start, by its edges; a span that was not empty keeps one
// device pixel.
static void SnapSpan(float* start, float* length, float scale)
{
    float first = muiSnapEdge(*start, scale);
    float last = muiSnapEdge(*start + *length, scale);
    if (*length > 0.0f && last <= first)
    {
        last = first + 1.0f / scale;
    }
    *start = first;
    *length = last - first;
}

muiRect muiSnapRect(muiRect rect, float scale)
{
    SnapSpan(&rect.x, &rect.width, scale);
    SnapSpan(&rect.y, &rect.height, scale);
    return rect;
}

// A border width in whole device pixels, at least one when not 0, as CSS
// draws borders.
static float SnapWidth(float width, float scale)
{
    if (width <= 0.0f)
    {
        return 0.0f;
    }
    float pixels = floorf(width * scale);
    return (pixels < 1.0f ? 1.0f : pixels) / scale;
}

// A radius of the border box: scale times its shorter side plus offset,
// both of which are at least 0, held to half that side.
static float Radius(muiDimension radius, float shorter)
{
    return fminf(radius.scale * shorter + radius.offset, shorter * 0.5f);
}

static muiCorners Radii(const muiCornerRadii* radii, muiRect rect, bool rtl)
{
    float shorter = fminf(rect.width, rect.height);
    float topStart = Radius(radii->topStart, shorter);
    float topEnd = Radius(radii->topEnd, shorter);
    float bottomEnd = Radius(radii->bottomEnd, shorter);
    float bottomStart = Radius(radii->bottomStart, shorter);
    return rtl ? (muiCorners){topEnd, topStart, bottomStart, bottomEnd}
               : (muiCorners){topStart, topEnd, bottomEnd, bottomStart};
}

muiCorners muiCornersOf(const muiCornerRadii* radii, muiRect rect, bool rtl)
{
    return Radii(radii, rect, rtl);
}

muiDrawCommand* muiTakeCommand(muiPainter* painter, muiDrawKind kind, uint32_t clip)
{
    muiDrawTables* out = painter->out;
    if (out->commandCount == painter->commandCapacity)
    {
        painter->full = true;
        return nullptr;
    }
    muiDrawCommand* command = &out->commands[out->commandCount++];
    memset(command, 0, sizeof *command);
    command->kind = kind;
    command->clip = clip;
    return command;
}

static uint32_t AddGradient(muiPainter* painter, const muiGradient* gradient, float opacity)
{
    muiDrawTables* out = painter->out;
    if (out->gradientCount == painter->gradientCapacity)
    {
        painter->full = true;
        return 0;
    }
    uint32_t index = out->gradientCount++;
    muiDrawGradient* entry = &out->gradients[index];
    memset(entry, 0, sizeof *entry);
    entry->kind = gradient->kind;
    entry->stopCount = gradient->stopCount;
    entry->interpolation = mui_interpolateOklab;
    entry->angle = gradient->angle;
    for (uint32_t i = 0; i < gradient->stopCount; i++)
    {
        entry->colors[i] = muiPaintColor(painter, gradient->stops[i].color, opacity);
        entry->positions[i] = gradient->stops[i].position;
    }
    return index;
}

static void AddShadow(muiPainter* painter, const muiShadow* shadow, muiRect rect, muiCorners radii,
                      bool inset, const muiPaintState* state)
{
    if (shadow->color.a <= 0.0f)
    {
        return;
    }
    muiDrawCommand* command = muiTakeCommand(painter, mui_drawShadow, state->clip);
    if (command == nullptr)
    {
        return;
    }
    command->shadow.rect = rect;
    command->shadow.radii = radii;
    command->shadow.color = muiPaintColor(painter, shadow->color, state->opacity);
    command->shadow.offsetX = shadow->offsetX;
    command->shadow.offsetY = shadow->offsetY;
    command->shadow.blur = shadow->blur;
    command->shadow.spread = shadow->spread;
    command->shadow.inset = inset ? 1u : 0u;
}

// The border widths and colors, physical.
typedef struct Borders
{
    muiSides widths;
    muiColor colors[4];
} Borders;

static Borders BordersOf(const muiLayoutNode* layout, const muiVisualStyle* visual, float scale)
{
    const muiEdges* width = &layout->style.border;
    const muiEdgeColors* color = &visual->borderColor;
    float start = SnapWidth(width->start, scale);
    float end = SnapWidth(width->end, scale);
    Borders borders = {
        .widths = {SnapWidth(width->top, scale), end, SnapWidth(width->bottom, scale), start},
        .colors = {color->top, color->end, color->bottom, color->start},
    };
    if (layout->rtl)
    {
        borders.widths.right = start;
        borders.widths.left = end;
        borders.colors[1] = color->start;
        borders.colors[3] = color->end;
    }
    return borders;
}

static bool HasVisibleBorder(const Borders* borders)
{
    const float widths[4] = {borders->widths.top, borders->widths.right, borders->widths.bottom,
                             borders->widths.left};
    for (uint32_t i = 0; i < 4; i++)
    {
        if (widths[i] > 0.0f && borders->colors[i].a > 0.0f)
        {
            return true;
        }
    }
    return false;
}

static void AddBox(muiPainter* painter, const muiVisualStyle* visual, const Borders* borders,
                   muiRect rect, muiCorners radii, const muiPaintState* state)
{
    bool gradient = visual->gradient.kind != mui_gradientNone;
    if (visual->background.a <= 0.0f && !gradient && !HasVisibleBorder(borders))
    {
        return;
    }
    muiDrawCommand* command = muiTakeCommand(painter, mui_drawBox, state->clip);
    if (command == nullptr)
    {
        return;
    }
    command->box.rect = muiSnapRect(rect, painter->scale);
    command->box.radii = radii;
    command->box.fill = muiPaintColor(painter, visual->background, state->opacity);
    command->box.gradient = gradient ? AddGradient(painter, &visual->gradient, state->opacity) : 0;
    command->box.borderWidths = borders->widths;
    // A side of no width draws no color, so it carries none.
    const float widths[4] = {borders->widths.top, borders->widths.right, borders->widths.bottom,
                             borders->widths.left};
    for (uint32_t i = 0; i < 4; i++)
    {
        if (widths[i] > 0.0f)
        {
            command->box.borderColors[i] =
                muiPaintColor(painter, borders->colors[i], state->opacity);
        }
    }
}

static void AddImage(muiPainter* painter, const muiVisualStyle* visual, muiRect rect,
                     const muiPaintState* state)
{
    if (visual->image == 0)
    {
        return;
    }
    muiDrawCommand* command = muiTakeCommand(painter, mui_drawImage, state->clip);
    if (command == nullptr)
    {
        return;
    }
    const muiEdges* slice = &visual->imageSlice;
    command->image.rect = muiSnapRect(rect, painter->scale);
    command->image.image = visual->image;
    command->image.uv = (muiRect){0.0f, 0.0f, 1.0f, 1.0f};
    // An image does not mirror: its slice's start and end are its left and
    // right.
    command->image.slice = (muiSides){slice->top, slice->end, slice->bottom, slice->start};
    command->image.tint = muiPaintColor(painter, visual->imageTint, state->opacity);
}

// The padding box of a border box, and its corners' radii.
static void PaddingBox(muiRect* rect, muiCorners* radii, const muiSides* widths)
{
    rect->x += widths->left;
    rect->y += widths->top;
    rect->width = fmaxf(rect->width - widths->left - widths->right, 0.0f);
    rect->height = fmaxf(rect->height - widths->top - widths->bottom, 0.0f);
    radii->topLeft = fmaxf(radii->topLeft - fmaxf(widths->left, widths->top), 0.0f);
    radii->topRight = fmaxf(radii->topRight - fmaxf(widths->right, widths->top), 0.0f);
    radii->bottomRight = fmaxf(radii->bottomRight - fmaxf(widths->right, widths->bottom), 0.0f);
    radii->bottomLeft = fmaxf(radii->bottomLeft - fmaxf(widths->left, widths->bottom), 0.0f);
}

// A clip of the node's rounded border box for its children, inside the
// clip it is drawn in; that clip when none fits.
static uint32_t AddClip(muiPainter* painter, muiRect rect, muiCorners radii, uint32_t parent)
{
    muiDrawTables* out = painter->out;
    if (out->clipCount == painter->clipCapacity)
    {
        painter->full = true;
        return parent;
    }
    uint32_t index = out->clipCount++;
    muiDrawClip* clip = &out->clips[index];
    memset(clip, 0, sizeof *clip);
    clip->rect = muiSnapRect(rect, painter->scale);
    clip->radii = radii;
    clip->parent = parent;
    return index;
}

bool muiPaintNode(muiPainter* painter, uint32_t slot, muiPaintState* state)
{
    const muiContext* context = painter->context;
    const muiLayoutNode* layout = &context->layout[slot - 1];
    const muiVisualStyle* visual = &context->visual[slot - 1];
    state->opacity = state->inherited * visual->opacity;
    if (state->opacity <= 0.0f)
    {
        return false;
    }
    muiRect rect = {state->x, state->y, layout->rect.width, layout->rect.height};
    muiCorners radii = Radii(&visual->radius, rect, layout->rtl);
    Borders borders = BordersOf(layout, visual, painter->scale);
    AddShadow(painter, &visual->outerShadow, rect, radii, false, state);
    AddBox(painter, visual, &borders, rect, radii, state);
    muiRect inner = rect;
    muiCorners innerRadii = radii;
    PaddingBox(&inner, &innerRadii, &borders.widths);
    AddShadow(painter, &visual->innerShadow, inner, innerRadii, true, state);
    AddImage(painter, visual, rect, state);
    if (visual->clip)
    {
        state->clip = AddClip(painter, rect, radii, state->clip);
    }
    return true;
}
