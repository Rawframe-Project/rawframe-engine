// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// COLR version 1 paint graphs (record mui-0006). A paint renders into the
// surface of its level, cleared first: level 0 is the caller's pixels,
// deeper levels are the service's, kept as the stack grows, so a surface
// is found again by its level after any deeper paint. Layers render each
// child a level down and composite it over; a glyph renders its child at
// its own level and masks it by its outline. Transforms follow fontTools'
// reading of the specification: x' = xx x + xy y + dx, y' = yx x + yy y +
// dy; rotations counter-clockwise; a skew of x angle a and y angle b maps
// x to x - tan(a) y and y to y + tan(b) x.
//
// Gradients are colr_gradient.c's and composite modes colr_composite.c's:
// a composite renders its backdrop at its own level and its source a
// level down, and combines them there.

#include "colr_paint.h"

#include "colr_composite.h"
#include "colr_gradient.h"
#include "motion_math.h"
#include "text_block.h"
#include "text_service.h"

#include FT_OUTLINE_H

#include <math.h>
#include <string.h>

#if MUI_COLR_PAINT

// An affine map of font units: x' = xx x + xy y + dx, y' = yx x + yy y + dy.
typedef muiPaintMatrix Matrix;

static const Matrix IDENTITY = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0};

// a after b.
static Matrix Multiply(Matrix a, Matrix b)
{
    return (Matrix){a.xx * b.xx + a.xy * b.yx,        a.xx * b.xy + a.xy * b.yy,
                    a.xx * b.dx + a.xy * b.dy + a.dx, a.yx * b.xx + a.yy * b.yx,
                    a.yx * b.xy + a.yy * b.yy,        a.yx * b.dx + a.yy * b.dy + a.dy};
}

static double Fixed(FT_Fixed value)
{
    return (double)value / 65536.0;
}

// An operation about a centre: moved to the origin, applied, moved back.
static Matrix Around(Matrix operation, FT_Fixed centerX, FT_Fixed centerY)
{
    double x = Fixed(centerX);
    double y = Fixed(centerY);
    Matrix to = {1.0, 0.0, -x, 0.0, 1.0, -y};
    Matrix back = {1.0, 0.0, x, 0.0, 1.0, y};
    return Multiply(back, Multiply(operation, to));
}

// The tangent, infinite at a quarter turn; PlaceOutline refuses it.
static double Tangent(double angle)
{
    double sine = 0.0;
    double cosine = 1.0;
    muiSinCos(angle, &sine, &cosine);
    return sine / cosine;
}

// A transform paint's matrix, and its child.
static Matrix TransformOf(const FT_COLR_Paint* paint, FT_OpaquePaint* childOut)
{
    // Angles are in half turns.
    const double turn = 3.14159265358979323846;
    switch (paint->format)
    {
    case FT_COLR_PAINTFORMAT_TRANSFORM:
    {
        const FT_Affine23* a = &paint->u.transform.affine;
        *childOut = paint->u.transform.paint;
        return (Matrix){Fixed(a->xx), Fixed(a->xy), Fixed(a->dx),
                        Fixed(a->yx), Fixed(a->yy), Fixed(a->dy)};
    }
    case FT_COLR_PAINTFORMAT_TRANSLATE:
        *childOut = paint->u.translate.paint;
        return (Matrix){1.0, 0.0, Fixed(paint->u.translate.dx),
                        0.0, 1.0, Fixed(paint->u.translate.dy)};
    case FT_COLR_PAINTFORMAT_SCALE:
    {
        const FT_PaintScale* s = &paint->u.scale;
        *childOut = s->paint;
        return Around((Matrix){Fixed(s->scale_x), 0.0, 0.0, 0.0, Fixed(s->scale_y), 0.0},
                      s->center_x, s->center_y);
    }
    case FT_COLR_PAINTFORMAT_ROTATE:
    {
        const FT_PaintRotate* r = &paint->u.rotate;
        double sine = 0.0;
        double cosine = 1.0;
        muiSinCos(Fixed(r->angle) * turn, &sine, &cosine);
        *childOut = r->paint;
        return Around((Matrix){cosine, -sine, 0.0, sine, cosine, 0.0}, r->center_x, r->center_y);
    }
    default:
    {
        const FT_PaintSkew* k = &paint->u.skew;
        *childOut = k->paint;
        return Around((Matrix){1.0, -Tangent(Fixed(k->x_skew_angle) * turn), 0.0,
                               Tangent(Fixed(k->y_skew_angle) * turn), 1.0, 0.0},
                      k->center_x, k->center_y);
    }
    }
}

static bool IsTransform(FT_PaintFormat format)
{
    return format == FT_COLR_PAINTFORMAT_TRANSFORM || format == FT_COLR_PAINTFORMAT_TRANSLATE ||
           format == FT_COLR_PAINTFORMAT_SCALE || format == FT_COLR_PAINTFORMAT_ROTATE ||
           format == FT_COLR_PAINTFORMAT_SKEW;
}

bool muiFindColorPaint(FT_Face face, uint32_t glyph, FT_OpaquePaint* rootOut)
{
    *rootOut = (FT_OpaquePaint){nullptr, 0};
    return FT_Get_Color_Glyph_Paint(face, glyph, FT_COLOR_NO_ROOT_TRANSFORM, rootOut) != 0;
}

// How far, in 64ths of a pixel, a placed outline may reach: far past any
// image, and far inside a 32-bit FT_Pos, so boxes of it do not overflow.
static const double FARTHEST = 268435456.0;

// Whether a matrix places a box within FARTHEST, its linear part within
// 16.16: an affine map keeps each point of an outline within the image of
// its box. False for a damaged font's, NaN or infinite parts too.
static bool PlacesNear(Matrix m, FT_BBox box)
{
    const double limit = 32767.0;
    if (!(fabs(m.xx) < limit && fabs(m.xy) < limit && fabs(m.yx) < limit && fabs(m.yy) < limit &&
          fabs(m.dx) < FARTHEST && fabs(m.dy) < FARTHEST))
    {
        return false;
    }
    const double xs[2] = {(double)box.xMin, (double)box.xMax};
    const double ys[2] = {(double)box.yMin, (double)box.yMax};
    for (int i = 0; i < 4; i++)
    {
        double x = xs[i & 1];
        double y = ys[i >> 1];
        if (!(fabs(m.xx * x + m.xy * y + m.dx) < FARTHEST &&
              fabs(m.yx * x + m.yy * y + m.dy) < FARTHEST))
        {
            return false;
        }
    }
    return true;
}

// Loads an outline and places it in pixels by a matrix of font units: its
// linear part as it is, its offset scaled to pixels, then the pen's.
static muiResult PlaceOutline(const muiPaintSource* source, uint32_t glyph, Matrix m)
{
    muiResult result = muiLoadGlyphOutline(source->font, source->key, glyph, source->size, 0);
    if (result != mui_success)
    {
        return result;
    }
    FT_Face face = source->font->face;
    FT_Outline* outline = &face->glyph->outline;
    double scale = (double)source->size / (double)face->units_per_EM;
    Matrix placed = m;
    placed.dx = m.dx * scale + (double)source->offset;
    placed.dy = m.dy * scale;
    FT_BBox box;
    FT_Outline_Get_CBox(outline, &box);
    if (!PlacesNear(placed, box))
    {
        return mui_errorFormat;
    }
    FT_Matrix linear = {(FT_Fixed)lround(m.xx * 65536.0), (FT_Fixed)lround(m.xy * 65536.0),
                        (FT_Fixed)lround(m.yx * 65536.0), (FT_Fixed)lround(m.yy * 65536.0)};
    FT_Outline_Transform(outline, &linear);
    FT_Outline_Translate(outline, (FT_Pos)lround(placed.dx), (FT_Pos)lround(placed.dy));
    return mui_success;
}

// A walk of a graph: its source, how deep it is, and what went wrong.
typedef struct Walk
{
    const muiPaintSource* source;
    uint32_t depth;
    muiResult result;
} Walk;

static bool Fail(Walk* walk, muiResult result)
{
    walk->result = result;
    return false;
}

// Adds the boxes of a paint's outlines, under their transforms, to box.
static bool Bounds(Walk* walk, FT_OpaquePaint opaque, Matrix m, muiPixelBox* box)
{
    FT_Face face = walk->source->font->face;
    FT_COLR_Paint paint;
    if (walk->depth >= MUI_MAX_PAINT_DEPTH || !FT_Get_Paint(face, opaque, &paint))
    {
        return Fail(walk, mui_errorFormat);
    }
    walk->depth++;
    bool ok = true;
    if (paint.format == FT_COLR_PAINTFORMAT_COLR_LAYERS)
    {
        FT_LayerIterator layers = paint.u.colr_layers.layer_iterator;
        FT_OpaquePaint layer = {nullptr, 0};
        while (ok && FT_Get_Paint_Layers(face, &layers, &layer))
        {
            ok = Bounds(walk, layer, m, box);
        }
    }
    else if (paint.format == FT_COLR_PAINTFORMAT_GLYPH)
    {
        muiResult placed = PlaceOutline(walk->source, paint.u.glyph.glyphID, m);
        ok = placed == mui_success || Fail(walk, placed);
        if (ok)
        {
            muiJoinPixelBox(box, muiOutlineBox(&face->glyph->outline));
        }
    }
    else if (paint.format == FT_COLR_PAINTFORMAT_COLR_GLYPH)
    {
        FT_OpaquePaint root;
        ok = !muiFindColorPaint(face, paint.u.colr_glyph.glyphID, &root) ||
             Bounds(walk, root, m, box);
    }
    else if (IsTransform(paint.format))
    {
        FT_OpaquePaint child;
        Matrix own = TransformOf(&paint, &child);
        ok = Bounds(walk, child, Multiply(m, own), box);
    }
    else if (paint.format == FT_COLR_PAINTFORMAT_COMPOSITE)
    {
        ok = Bounds(walk, paint.u.composite.backdrop_paint, m, box) &&
             Bounds(walk, paint.u.composite.source_paint, m, box);
    }
    walk->depth--;
    return ok;
}

// A glyph's clip box in 64ths of a pixel, with the pen's offset, at the
// source's size; false when the font gives the glyph none. Maul sets no
// transform on a face, so the box's corners are its own.
static bool ClipOf(const muiPaintSource* source, uint32_t glyph, FT_BBox* clipOut,
                   muiResult* result)
{
    // The base glyph's outline sets the face to the size the clip box is
    // given at.
    *result = muiLoadGlyphOutline(source->font, source->key, glyph, source->size, 0);
    FT_ClipBox clip;
    if (*result != mui_success || !FT_Get_Color_Glyph_ClipBox(source->font->face, glyph, &clip))
    {
        return false;
    }
    *clipOut = (FT_BBox){clip.bottom_left.x + source->offset, clip.bottom_left.y,
                         clip.top_right.x + source->offset, clip.top_right.y};
    return true;
}

muiResult muiColorPaintBox(const muiPaintSource* source, uint32_t glyph, FT_OpaquePaint root,
                           muiPixelBox* boxOut)
{
    *boxOut = (muiPixelBox){0, 0, 0, 0};
    muiResult result = mui_success;
    FT_BBox clip;
    if (ClipOf(source, glyph, &clip, &result))
    {
        *boxOut = muiPixelBoxOf(clip);
        return mui_success;
    }
    if (result != mui_success)
    {
        return result;
    }
    Walk walk = {source, 0, mui_success};
    return Bounds(&walk, root, IDENTITY, boxOut) ? mui_success : walk.result;
}

// The surfaces: level 0 the caller's, deeper ones the service's.
typedef struct Surfaces
{
    Walk walk;
    const muiPixelBox* box;
    size_t count;
    float* top;
} Surfaces;

// A level's surface, found again after any deeper level was made; NULL
// when memory runs out.
static float* Surface(Surfaces* surfaces, uint32_t level)
{
    if (level == 0)
    {
        return surfaces->top;
    }
    muiTextService* service = surfaces->walk.source->service;
    size_t bytes = surfaces->count * 4 * sizeof(float);
    if (!muiReserveKeeping(&service->allocator, &service->paintSurfaces, (size_t)level * bytes,
                           (size_t)(level - 1) * bytes))
    {
        return nullptr;
    }
    return (float*)service->paintSurfaces.data + (size_t)(level - 1) * surfaces->count * 4;
}

// Composites a surface over another, premultiplied.
static void Over(const float* source, float* destination, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        const float* s = &source[i * 4];
        float* d = &destination[i * 4];
        float keep = 1.0f - s[3];
        for (int c = 0; c < 4; c++)
        {
            d[c] = s[c] + d[c] * keep;
        }
    }
}

static bool Render(Surfaces* surfaces, FT_OpaquePaint opaque, Matrix m, uint32_t level);

// Renders each layer a level down and composites it over.
static bool Layers(Surfaces* surfaces, FT_LayerIterator layers, Matrix m, uint32_t level)
{
    FT_Face face = surfaces->walk.source->font->face;
    FT_OpaquePaint layer = {nullptr, 0};
    while (FT_Get_Paint_Layers(face, &layers, &layer))
    {
        if (!Render(surfaces, layer, m, level + 1))
        {
            return false;
        }
        Over(Surface(surfaces, level + 1), Surface(surfaces, level), surfaces->count);
    }
    return true;
}

// Masks a level's surface by an outline's coverage.
static bool MaskBy(Surfaces* surfaces, FT_Outline* outline, uint32_t level)
{
    muiTextService* service = surfaces->walk.source->service;
    if (!muiReserve(&service->allocator, &service->colorCoverage, surfaces->count))
    {
        return Fail(&surfaces->walk, mui_errorCapacity);
    }
    unsigned char* coverage = service->colorCoverage.data;
    memset(coverage, 0, surfaces->count);
    muiResult result =
        muiRasterizeOutline(service, outline, surfaces->box, coverage, (int)surfaces->box->width);
    if (result != mui_success)
    {
        return Fail(&surfaces->walk, result);
    }
    float* pixels = Surface(surfaces, level);
    for (size_t i = 0; i < surfaces->count; i++)
    {
        float k = (float)coverage[i] / 255.0f;
        for (int c = 0; c < 4; c++)
        {
            pixels[i * 4 + c] *= k;
        }
    }
    return true;
}

// Masks a level's surface by a glyph's outline under a matrix.
static bool Mask(Surfaces* surfaces, uint32_t glyph, Matrix m, uint32_t level)
{
    const muiPaintSource* source = surfaces->walk.source;
    muiResult result = PlaceOutline(source, glyph, m);
    return result == mui_success ? MaskBy(surfaces, &source->font->face->glyph->outline, level)
                                 : Fail(&surfaces->walk, result);
}

static bool Fill(Surfaces* surfaces, muiLinearColor color, uint32_t level)
{
    float* pixels = Surface(surfaces, level);
    for (size_t i = 0; i < surfaces->count; i++)
    {
        pixels[i * 4] = color.r;
        pixels[i * 4 + 1] = color.g;
        pixels[i * 4 + 2] = color.b;
        pixels[i * 4 + 3] = color.a;
    }
    return true;
}

// Renders a paint of the formats beyond layers, fills and glyphs.
static bool RenderOther(Surfaces* surfaces, const FT_COLR_Paint* paint, Matrix m, uint32_t level)
{
    FT_Face face = surfaces->walk.source->font->face;
    if (paint->format == FT_COLR_PAINTFORMAT_COLR_GLYPH)
    {
        FT_OpaquePaint root;
        return !muiFindColorPaint(face, paint->u.colr_glyph.glyphID, &root) ||
               Render(surfaces, root, m, level);
    }
    if (IsTransform(paint->format))
    {
        FT_OpaquePaint child;
        Matrix own = TransformOf(paint, &child);
        return Render(surfaces, child, Multiply(m, own), level);
    }
    if (paint->format == FT_COLR_PAINTFORMAT_COMPOSITE)
    {
        if (!Render(surfaces, paint->u.composite.backdrop_paint, m, level) ||
            !Render(surfaces, paint->u.composite.source_paint, m, level + 1))
        {
            return false;
        }
        muiComposite((uint32_t)paint->u.composite.composite_mode, Surface(surfaces, level + 1),
                     Surface(surfaces, level), surfaces->count);
    }
    return true;
}

static bool Render(Surfaces* surfaces, FT_OpaquePaint opaque, Matrix m, uint32_t level)
{
    Walk* walk = &surfaces->walk;
    FT_Face face = walk->source->font->face;
    FT_COLR_Paint paint;
    if (walk->depth >= MUI_MAX_PAINT_DEPTH || !FT_Get_Paint(face, opaque, &paint))
    {
        return Fail(walk, mui_errorFormat);
    }
    float* pixels = Surface(surfaces, level);
    if (pixels == nullptr)
    {
        return Fail(walk, mui_errorCapacity);
    }
    memset(pixels, 0, surfaces->count * 4 * sizeof(float));
    walk->depth++;
    bool ok = true;
    switch (paint.format)
    {
    case FT_COLR_PAINTFORMAT_COLR_LAYERS:
        ok = Layers(surfaces, paint.u.colr_layers.layer_iterator, m, level);
        break;
    case FT_COLR_PAINTFORMAT_SOLID:
        ok = Fill(surfaces, muiColrPaintColor(walk->source, paint.u.solid.color), level);
        break;
    case FT_COLR_PAINTFORMAT_LINEAR_GRADIENT:
    case FT_COLR_PAINTFORMAT_RADIAL_GRADIENT:
    case FT_COLR_PAINTFORMAT_SWEEP_GRADIENT:
    {
        muiResult painted =
            muiPaintGradient(walk->source, &paint, m, surfaces->box, Surface(surfaces, level));
        ok = painted == mui_success || Fail(walk, painted);
        break;
    }
    case FT_COLR_PAINTFORMAT_GLYPH:
        ok = Render(surfaces, paint.u.glyph.paint, m, level) &&
             Mask(surfaces, paint.u.glyph.glyphID, m, level);
        break;
    default:
        ok = RenderOther(surfaces, &paint, m, level);
        break;
    }
    walk->depth--;
    return ok;
}

// How much of a pixel's span, from 64ths at to at + 64, lies between low
// and high.
static float Overlap(FT_Pos at, FT_Pos low, FT_Pos high)
{
    FT_Pos from = at > low ? at : low;
    FT_Pos to = at + 64 < high ? at + 64 : high;
    return to > from ? (float)(to - from) / 64.0f : 0.0f;
}

// Scales each pixel by how much of it a box in 64ths covers.
static void ClipTo(const FT_BBox* clip, const muiPixelBox* box, float* pixels)
{
    for (FT_Pos row = 0; row < box->height; row++)
    {
        float up = Overlap((box->bottom + box->height - 1 - row) * 64, clip->yMin, clip->yMax);
        for (FT_Pos column = 0; column < box->width; column++)
        {
            float k = up * Overlap((box->left + column) * 64, clip->xMin, clip->xMax);
            float* pixel = &pixels[((size_t)row * (size_t)box->width + (size_t)column) * 4];
            for (int c = 0; c < 4; c++)
            {
                pixel[c] *= k;
            }
        }
    }
}

muiResult muiPaintColorGlyph(const muiPaintSource* source, uint32_t glyph, FT_OpaquePaint root,
                             const muiPixelBox* box, float* pixels)
{
    Surfaces surfaces = {
        {source, 0, mui_success}, box, (size_t)box->width * (size_t)box->height, pixels};
    if (!Render(&surfaces, root, IDENTITY, 0))
    {
        return surfaces.walk.result;
    }
    // Nothing outside the clip box renders, though its box was rounded out
    // to whole pixels.
    muiResult result = mui_success;
    FT_BBox clip;
    if (ClipOf(source, glyph, &clip, &result))
    {
        ClipTo(&clip, box, pixels);
    }
    return result;
}

#endif
